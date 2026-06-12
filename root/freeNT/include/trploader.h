#ifndef _TRP_LOADER_H
#define _TRP_LOADER_H

// ==============================================================================
// TRP_LOADER.H - Toriginal Runtime Package Loader Public Interface
// ==============================================================================

#include <stdint.h>
#include "types.h"
#include "fs.h"

// Load and prepare a .trp file for a given process
// Sets proc->context.rip on success for binary payloads
// Returns 0 on success, -1 on failure
int trp_load(const char *filename, pid_t pid);

// Build a TRP binary package and write it to an open file descriptor
// manifest_text  — the manifest string (directives, null terminated)
// payload        — raw bytes of the executable payload
// payload_len    — length of payload in bytes
// Returns total bytes written, or -1 on error
int trp_create_package(fd_t out_fd,
                        const char *manifest_text,
                        const uint8_t *payload,
                        uint32_t payload_len);

#endif /* _TRP_LOADER_H */