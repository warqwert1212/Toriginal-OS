#ifndef _TCP_H
#define _TCP_H

#include "types.h"

/* tcp.h — client-side (active-open only) TCP.
 *
 * Real honest limitations, stated up front rather than discovered
 * later: ONE connection at a time (matches the rest of this stack —
 * one NIC, one ARP resolve path, one DNS query in flight). Stop-and-wait
 * sending with a single send attempt — no retransmission queue, no
 * congestion control, no window scaling. Fine for a short-lived local
 * HTTP request/response; not a production TCP/IP stack.
 */

void tcp_init(void);

/* Blocking connect (polls with a timeout). Returns 1 on success. */
int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms);

/* Sends data on the current connection. Returns 1 on success (remote
 * ACKed it within timeout_ms), 0 on failure/timeout. Single attempt —
 * no retransmission if the ACK is lost. */
int tcp_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms);

/* Copies up to maxlen bytes of received data into buf, removing them
 * from the internal buffer. Returns bytes copied (0 if none available
 * yet and the connection isn't closed — caller should retry after a
 * short sleep; 0 AND connection-closed means real EOF). */
uint32_t tcp_recv(uint8_t *buf, uint32_t maxlen);

/* True once the remote side has sent FIN and there's nothing left
 * buffered to read — i.e. real end of stream. */
int tcp_is_closed_and_drained(void);

void tcp_close(void);
void tcp_handle_packet(uint32_t src_ip, const uint8_t *packet, uint16_t len);

#endif /* _TCP_H */
