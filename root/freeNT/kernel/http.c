#include "http.h"
#include "tcp.h"
#include "string.h"
#include "serial.h"
#include "pit.h"

static int str_find(const uint8_t *haystack, uint32_t haystack_len, const char *needle)
{
    uint32_t needle_len = (uint32_t)strlen(needle);
    if (needle_len == 0 || needle_len > haystack_len) return -1;
    for (uint32_t i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) return (int)i;
    }
    return -1;
}

int http_get(uint32_t host_ip, uint16_t port, const char *host_header, const char *path,
             uint8_t *out_buf, uint32_t out_buf_size,
             uint8_t **out_body, uint32_t *out_body_len,
             int *out_status_code, uint32_t timeout_ms)
{
    char request[512];
    int n = 0;
    const char *parts[] = { "GET ", path, " HTTP/1.1\r\nHost: ", host_header,
                            "\r\nConnection: close\r\nUser-Agent: ToriginalOS-trpm/1.0\r\n\r\n" };
    for (int i = 0; i < 5; i++) {
        int l = (int)strlen(parts[i]);
        if (n + l >= (int)sizeof(request)) { serial_puts("[HTTP] request too long\n"); return 0; }
        memcpy(request + n, parts[i], (size_t)l);
        n += l;
    }

    if (!tcp_connect(host_ip, port, timeout_ms)) {
        serial_puts("[HTTP] TCP connect failed\n");
        return 0;
    }

    if (!tcp_send((const uint8_t *)request, (uint32_t)n, timeout_ms)) {
        serial_puts("[HTTP] failed to send request\n");
        tcp_close();
        return 0;
    }

    uint32_t total = 0;
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        uint32_t got = tcp_recv(out_buf + total, out_buf_size - total);
        if (got > 0) {
            total += got;
            waited = 0; /* reset idle timer on forward progress */
            if (total >= out_buf_size) {
                serial_puts("[HTTP] WARNING: response exceeds buffer size — truncating\n");
                break;
            }
            continue;
        }
        if (tcp_is_closed_and_drained()) break;
        pit_sleep(50);
        waited += 50;
    }
    tcp_close();

    if (total == 0) { serial_puts("[HTTP] empty response\n"); return 0; }

    int header_end = str_find(out_buf, total, "\r\n\r\n");
    if (header_end < 0) { serial_puts("[HTTP] malformed response (no header terminator)\n"); return 0; }

    /* Status line: "HTTP/1.1 200 OK" */
    int status = 0;
    {
        const char *p = (const char *)out_buf;
        while (*p && *p != ' ') p++;
        if (*p == ' ') { p++; status = 0; while (*p >= '0' && *p <= '9') { status = status * 10 + (*p - '0'); p++; } }
    }
    if (out_status_code) *out_status_code = status;

    if (str_find(out_buf, (uint32_t)header_end, "Transfer-Encoding: chunked") >= 0 ||
        str_find(out_buf, (uint32_t)header_end, "transfer-encoding: chunked") >= 0) {
        serial_puts("[HTTP] WARNING: server used chunked transfer-encoding — not supported, body may be wrong\n");
    }

    *out_body = out_buf + header_end + 4;
    *out_body_len = total - (uint32_t)(header_end + 4);
    return 1;
}
