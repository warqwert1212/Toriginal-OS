#ifndef _HTTP_H
#define _HTTP_H

#include "types.h"

/* http.h — plain HTTP/1.1 GET client over the TCP stack above.
 *
 * REAL LIMITATION, stated up front: this is HTTP only, no TLS. GitHub
 * (raw.githubusercontent.com, api.github.com, github.com itself) is
 * HTTPS-only — implementing real TLS in a freestanding kernel is its
 * own separate, large undertaking (certificate handling, the TLS
 * handshake, real crypto primitives) that this does not include. To
 * actually reach a GitHub-hosted package repo, you need something on
 * the other end speaking plain HTTP — e.g. a small host-side proxy that
 * fetches the real HTTPS URL and re-serves it over plain HTTP to this
 * OS's network. See the trpm documentation for the configured repo host.
 *
 * Also doesn't handle chunked transfer-encoding — only servers/proxies
 * that send a plain Content-Length body will parse correctly.
 */

/* Fetches http://<host_ip>:<port><path> with a Host: header of
 * host_header. Response (status line + headers + body) is written into
 * out_buf (out_buf_size capacity). On success, *out_body and *out_body_len
 * point at the body within out_buf. Returns 1 on success, 0 on failure. */
int http_get(uint32_t host_ip, uint16_t port, const char *host_header, const char *path,
             uint8_t *out_buf, uint32_t out_buf_size,
             uint8_t **out_body, uint32_t *out_body_len,
             int *out_status_code, uint32_t timeout_ms);

#endif /* _HTTP_H */
