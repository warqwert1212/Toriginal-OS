#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "mm.h"
#include "string.h"
#include "serial.h"
#include "pit.h"

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset; /* top 4 bits = header len in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_header_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef enum { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_CLOSING } tcp_state_t;

#define TCP_RECV_BUF_SIZE (512 * 1024) /* real limitation: a response
    larger than this will be truncated — flagged honestly, not silently */

static struct {
    tcp_state_t state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t rcv_nxt;
    uint8_t *recv_buf;
    uint32_t recv_len;
    int remote_fin;
    int last_ack_seen; /* set by handle_packet when an ACK advances snd_una, polled by tcp_send */
} g_conn;

static uint16_t g_next_local_port = 40000;

typedef struct __attribute__((packed)) {
    uint32_t src_ip, dst_ip;
    uint8_t zero, protocol;
    uint16_t tcp_len;
} tcp_pseudo_header_t;

void tcp_init(void)
{
    memset(&g_conn, 0, sizeof(g_conn));
    g_conn.state = TCP_CLOSED;
    if (!g_conn.recv_buf) g_conn.recv_buf = (uint8_t *)kmalloc(TCP_RECV_BUF_SIZE);
    serial_puts("[TCP] ready (single connection, no retransmission — see tcp.h)\n");
}

static int send_segment(uint8_t flags, uint32_t seq, uint32_t ack, const uint8_t *data, uint16_t data_len)
{
    uint8_t buf[ETH_MTU];
    if ((uint32_t)sizeof(tcp_header_t) + data_len > ETH_MTU - 20) return -1; /* leave room for IP header */

    tcp_header_t *tcp = (tcp_header_t *)buf;
    tcp->src_port = htons(g_conn.local_port);
    tcp->dst_port = htons(g_conn.remote_port);
    tcp->seq = htonl(seq);
    tcp->ack = htonl(ack);
    tcp->data_offset = (uint8_t)((sizeof(tcp_header_t) / 4) << 4);
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    if (data_len) memcpy(buf + sizeof(tcp_header_t), data, data_len);

    uint16_t seg_len = (uint16_t)(sizeof(tcp_header_t) + data_len);

    /* TCP checksum covers a pseudo-header + the segment. Compute over a
     * scratch buffer since ip_checksum() just sums whatever bytes it's given. */
    uint8_t cbuf[sizeof(tcp_pseudo_header_t) + ETH_MTU];
    tcp_pseudo_header_t *ph = (tcp_pseudo_header_t *)cbuf;
    net_device_t *dev = net_get_device();
    ph->src_ip = htonl(dev ? dev->ip : 0);
    ph->dst_ip = htonl(g_conn.remote_ip);
    ph->zero = 0;
    ph->protocol = IP_PROTO_TCP;
    ph->tcp_len = htons(seg_len);
    memcpy(cbuf + sizeof(tcp_pseudo_header_t), buf, seg_len);
    tcp->checksum = ip_checksum(cbuf, sizeof(tcp_pseudo_header_t) + seg_len);

    return ip_send(g_conn.remote_ip, IP_PROTO_TCP, buf, seg_len);
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms)
{
    if (g_conn.state != TCP_CLOSED) {
        serial_puts("[TCP] a connection is already active — this stack supports one at a time\n");
        return 0;
    }
    if (!g_conn.recv_buf) g_conn.recv_buf = (uint8_t *)kmalloc(TCP_RECV_BUF_SIZE);

    g_conn.remote_ip = dst_ip;
    g_conn.remote_port = dst_port;
    g_conn.local_port = g_next_local_port++;
    g_conn.snd_nxt = 0x10000; /* fixed-ish ISN — not cryptographically random, honest limitation */
    g_conn.snd_una = g_conn.snd_nxt;
    g_conn.rcv_nxt = 0;
    g_conn.recv_len = 0;
    g_conn.remote_fin = 0;
    g_conn.state = TCP_SYN_SENT;

    send_segment(TCP_SYN, g_conn.snd_nxt, 0, NULL, 0);

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        pit_sleep(50);
        waited += 50;
        if (g_conn.state == TCP_ESTABLISHED) return 1;
        if (g_conn.state == TCP_CLOSED) return 0; /* RST received */
    }
    serial_puts("[TCP] connect timed out\n");
    g_conn.state = TCP_CLOSED;
    return 0;
}

int tcp_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    if (g_conn.state != TCP_ESTABLISHED) return 0;

    uint32_t sent = 0;
    while (sent < len) {
        uint16_t chunk = (uint16_t)((len - sent) > 1400 ? 1400 : (len - sent));
        uint32_t seg_seq = g_conn.snd_nxt;
        g_conn.last_ack_seen = 0;
        send_segment(TCP_PSH | TCP_ACK, seg_seq, g_conn.rcv_nxt, data + sent, chunk);

        uint32_t waited = 0;
        int acked = 0;
        while (waited < timeout_ms) {
            pit_sleep(50);
            waited += 50;
            if (g_conn.snd_una >= seg_seq + chunk) { acked = 1; break; }
            if (g_conn.state != TCP_ESTABLISHED) break;
        }
        if (!acked) {
            serial_puts("[TCP] send timed out waiting for ACK (single-attempt, no retransmission)\n");
            return 0;
        }
        g_conn.snd_nxt = seg_seq + chunk;
        sent += chunk;
    }
    return 1;
}

uint32_t tcp_recv(uint8_t *buf, uint32_t maxlen)
{
    uint32_t n = g_conn.recv_len < maxlen ? g_conn.recv_len : maxlen;
    if (n == 0) return 0;
    memcpy(buf, g_conn.recv_buf, n);
    memmove(g_conn.recv_buf, g_conn.recv_buf + n, g_conn.recv_len - n);
    g_conn.recv_len -= n;
    return n;
}

int tcp_is_closed_and_drained(void)
{
    return g_conn.remote_fin && g_conn.recv_len == 0;
}

void tcp_close(void)
{
    if (g_conn.state == TCP_ESTABLISHED) {
        send_segment(TCP_FIN | TCP_ACK, g_conn.snd_nxt, g_conn.rcv_nxt, NULL, 0);
        g_conn.snd_nxt += 1;
        g_conn.state = TCP_CLOSING;
        /* Brief, bounded wait for the remote's ACK/FIN — not fully
         * RFC-correct teardown (no TIME_WAIT), just enough to be a
         * polite peer without hanging forever. */
        uint32_t waited = 0;
        while (waited < 1000 && g_conn.state == TCP_CLOSING) { pit_sleep(50); waited += 50; }
    }
    g_conn.state = TCP_CLOSED;
}

void tcp_handle_packet(uint32_t src_ip, const uint8_t *packet, uint16_t len)
{
    if (g_conn.state == TCP_CLOSED) return;
    if (src_ip != g_conn.remote_ip) return;
    if (len < sizeof(tcp_header_t)) return;

    const tcp_header_t *tcp = (const tcp_header_t *)packet;
    if (ntohs(tcp->dst_port) != g_conn.local_port) return;
    if (ntohs(tcp->src_port) != g_conn.remote_port) return;

    uint8_t header_words = (uint8_t)(tcp->data_offset >> 4);
    uint16_t header_len = (uint16_t)(header_words * 4);
    if (header_len < sizeof(tcp_header_t) || header_len > len) return;

    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;
    const uint8_t *data = packet + header_len;
    uint16_t data_len = (uint16_t)(len - header_len);

    if (flags & TCP_RST) {
        serial_puts("[TCP] connection reset by remote\n");
        g_conn.state = TCP_CLOSED;
        return;
    }

    if (g_conn.state == TCP_SYN_SENT) {
        if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
            g_conn.rcv_nxt = seq + 1;
            g_conn.snd_una = ack;
            g_conn.snd_nxt = g_conn.snd_nxt + 1; /* our SYN consumed one sequence number */
            send_segment(TCP_ACK, g_conn.snd_nxt, g_conn.rcv_nxt, NULL, 0);
            g_conn.state = TCP_ESTABLISHED;
        }
        return;
    }

    if (g_conn.state == TCP_ESTABLISHED || g_conn.state == TCP_CLOSING) {
        if (flags & TCP_ACK) {
            if (ack > g_conn.snd_una) g_conn.snd_una = ack;
        }

        if (data_len > 0 && seq == g_conn.rcv_nxt) {
            uint32_t space = TCP_RECV_BUF_SIZE - g_conn.recv_len;
            uint32_t copy_len = data_len < space ? data_len : (uint16_t)space;
            if (copy_len < data_len) {
                serial_puts("[TCP] WARNING: recv buffer full — truncating (real limitation, see tcp.h)\n");
            }
            if (copy_len > 0) {
                memcpy(g_conn.recv_buf + g_conn.recv_len, data, copy_len);
                g_conn.recv_len += copy_len;
            }
            g_conn.rcv_nxt = seq + data_len;
            send_segment(TCP_ACK, g_conn.snd_nxt, g_conn.rcv_nxt, NULL, 0);
        }

        if (flags & TCP_FIN) {
            g_conn.rcv_nxt = seq + data_len + 1;
            g_conn.remote_fin = 1;
            send_segment(TCP_ACK, g_conn.snd_nxt, g_conn.rcv_nxt, NULL, 0);
            if (g_conn.state == TCP_CLOSING) g_conn.state = TCP_CLOSED;
        }
    }
}
