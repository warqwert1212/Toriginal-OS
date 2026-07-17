#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

/* Architecture */
#define ARCH_X86_64 1

/* Memory configuration */
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* Maximum number of processes */
#define MAX_PROCESSES 1024

/* Maximum number of open file descriptors per process */
#define MAX_FD_PER_PROCESS 1024

/* Maximum number of memory pages */
#define MAX_PAGES 1048576  /* 4GB with 4K pages */

/* Process priority levels */
#define PROCESS_PRIORITY_MIN 0
#define PROCESS_PRIORITY_MAX 255
#define PROCESS_PRIORITY_DEFAULT 128

/* Timer frequency (Hz) */
#define TIMER_FREQUENCY 1000

/* Debug flags */
#define DEBUG_MEMORY 0
#define DEBUG_PROCESS 0
#define DEBUG_FS 0

/* trpm package integrity secret.
 *
 * trpm fetches .trp packages over plain HTTP (see shell.c's trpm_fetch_remote
 * comment for why: the real repo is HTTPS-only and this network stack
 * doesn't speak TLS). Plain HTTP means anyone on-path between the proxy
 * and this OS can substitute a malicious .trp in transit, which - since
 * trp_exec_bin() memcpy's the payload straight into executable memory and
 * jumps to it with no other verification - is a direct route to arbitrary
 * code execution.
 *
 * This secret is used to verify an HMAC-SHA256 tag (sent as the
 * "X-Trp-Hmac" response header by trpm-proxy.py) over every downloaded
 * package body before it's trusted. It is NOT a substitute for real
 * transport security or package signing - it's a shared secret between
 * you and your own proxy, so:
 *   - CHANGE THIS before you rely on it for anything. The proxy computes
 *     the same value from its --secret flag / TRPM_HMAC_SECRET env var -
 *     the two must match.
 *   - Anyone who has this string can forge a valid tag. It stops an
 *     on-path network attacker who *doesn't* have it; it does not stop
 *     someone who has compromised your proxy or extracted this binary.
 *   - A real fix eventually is asymmetric signing (proxy signs with a
 *     private key, kernel verifies with a public key baked in at build
 *     time) so the secret itself never has to be network- or
 *     binary-adjacent. That's a bigger lift than this file wants to be. */
#define TRP_HMAC_SECRET "CHANGE-ME-toriginal-trpm-shared-secret"

#endif /* _KERNEL_CONFIG_H */
