#ifndef _DNS_H
#define _DNS_H

#include "types.h"

/* dns_resolve — real DNS-over-UDP A-record lookup, blocking (polls with
 * a timeout) since nothing above this layer is async yet. Returns 1 and
 * fills *out_ip (host byte order) on success, 0 on failure/timeout. */
int dns_resolve(const char *hostname, uint32_t *out_ip, uint32_t timeout_ms);

#endif /* _DNS_H */
