#ifndef _TRP_MANIFEST_H
#define _TRP_MANIFEST_H

#include "types.h"

/* ── Limits ─────────────────────────────────────────────────────────────── */
#define TRP_MANIFEST_MAX_ERRORS    16
#define TRP_MANIFEST_ERROR_LEN    256
#define TRP_MANIFEST_MAX_FALLBACKS  8
#define TRP_MANIFEST_MAX_DEPENDS   8
#define TRP_MANIFEST_MAX_CONFLICTS 8

/* ── Parsed manifest result ──────────────────────────────────────────────── */
typedef struct {
    /* Required */
    char execute_at[256];
    int  execute_at_found;
    int  execute_at_line;

    /* Fallbacks */
    char fallback_execute_at[TRP_MANIFEST_MAX_FALLBACKS][256];
    int  fallback_count;

    /* Optional */
    char window_name[128];
    char icon[256];           /* FIX: was missing, used in trp_manifest.c */
    char priority[16];
    char execute_if[256];
    int  mark_executable;
    char language[32];
    char version[64];
    char assets[256];
    int  debug_mode;
    int  resource_request_high;
    int  resource_request_line;

    /* NEW directives (v1.2) */
    char description[256];  /* Description/:  human-readable summary, shown by trpinfo/trpm */
    char args[256];         /* Args/:         default args a launcher may pass to the payload */
    char checksum[65];      /* Checksum/:     lowercase hex sha256 of the embedded payload;
                              * 64 hex chars + NUL. Verified against the actual payload bytes
                              * at load time when present - see trp_manifest_verify_checksum(). */
    char depends[TRP_MANIFEST_MAX_DEPENDS][256]; /* Depends/:  package(s) required by this package */
    int  depends_count;
    char conflicts[TRP_MANIFEST_MAX_CONFLICTS][256]; /* Conflicts/: packages that must not co-exist */
    int  conflicts_count;
    char repository[256];  /* Repository/: optional trust target / origin label */
    char signer[128];       /* Signer/: optional signed-by identity */
    char signature[256];    /* Signature/: detached repo attestation fingerprint */

    /* Appstat/: terminal|gui - tells the OS (the shell/launcher and the
     * desktop's own launch path - see desktop.c's launch_trp()) which
     * kind of program this is, WITHOUT anything having to run the
     * binary first to find out. Not a change to how the program is
     * loaded or executed (trploader.c doesn't read this at all - a
     * "gui" program is still just a normal TRP binary that happens to
     * talk to the compositor/wm once it's running), purely a launch-
     * time routing hint: e.g. so a terminal-only launcher can warn
     * ("this needs the GUI, which isn't running") instead of silently
     * starting a program that will hang waiting for a compositor
     * connection that will never come, and so the desktop's own
     * launcher/start-menu can show terminal apps in an actual
     * terminal window rather than as a bare, silent process.
     * appstat_is_gui is 1 only when Appstat/: gui was explicitly
     * given; ANY other value (missing, "terminal", or an unrecognized
     * typo) leaves it 0 - terminal is the safer default to fall back
     * to, since a terminal-capable environment can still launch
     * something that was merely MISLABELED as terminal, but launching
     * something as "gui" when that was a typo, on a headless/serial-
     * only boot with no compositor running at all, has no such safe
     * fallback. */
    char appstat[16];       /* Appstat/: raw value as written, lowercased */
    int  appstat_is_gui;    /* 1 iff appstat is exactly "gui"; else 0 (terminal) */

    /* Error list */
    char errors[TRP_MANIFEST_MAX_ERRORS][TRP_MANIFEST_ERROR_LEN];
    int  error_count;
} trp_manifest_t;

/* Parse manifest text into *out. Returns 0 on success, -1 if errors found. */
int trp_manifest_parse(const char *text, uint32_t len, trp_manifest_t *out);

/* Validate that Execute at file exists on the VFS.
 * skip_file_check should be non-zero when the package carries its own
 * embedded payload (a self-contained .trp), since "Execute at" then just
 * documents/labels the entry point rather than naming a file that has
 * to independently already exist somewhere else on the VFS. */
int trp_manifest_validate_ex(trp_manifest_t *out, int skip_file_check);

/* Back-compat wrapper: always does the on-disk existence check
 * (skip_file_check = 0). */
int trp_manifest_validate(trp_manifest_t *out);

/* Verify m->checksum (if present) against the sha256 of payload/payload_len.
 * Returns 1 if the checksum matches (or none was specified - nothing to
 * check), 0 if a checksum was specified and does NOT match. */
int trp_manifest_verify_checksum(const trp_manifest_t *m,
                                  const uint8_t *payload, uint32_t payload_len);

/* Print all collected errors to VGA + serial. */
void trp_manifest_print_errors(const trp_manifest_t *m, int gui_mode);

/* Prompt user to allow a high-resource request. Returns 1=yes, 0=no. */
int trp_manifest_resource_prompt(int gui_mode);

/*
 * Full execution gate: parse → validate → resource check → return entry.
 * On success (0) entry_out holds the execute_at filename.
 * On failure (-1) errors have already been printed.
 *
 * manifest_out, if non-NULL, receives the fully parsed manifest so the
 * caller doesn't have to re-parse the text a second time to read
 * anything beyond the entry filename (language, checksum, etc.).
 *
 * has_embedded_payload should be non-zero when the caller already has a
 * payload blob bundled in the same .trp file (see trp_manifest_validate_ex).
 */
int trp_manifest_run_gate2(const char *manifest_text,
                            uint32_t    len,
                            trp_manifest_t *manifest_out,
                            char       *entry_out,
                            int         entry_out_len,
                            int         gui_mode,
                            int         has_embedded_payload);

/* Back-compat wrapper: has_embedded_payload = 0, manifest_out discarded. */
int trp_manifest_run_gate(const char *manifest_text,
                           uint32_t    len,
                           char       *entry_out,
                           int         entry_out_len,
                           int         gui_mode);

#endif /* _TRP_MANIFEST_H */
