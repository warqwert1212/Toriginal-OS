#ifndef _TRP_MANIFEST_H
#define _TRP_MANIFEST_H

#include "types.h"

/* ── Limits ─────────────────────────────────────────────────────────────── */
#define TRP_MANIFEST_MAX_ERRORS    16
#define TRP_MANIFEST_ERROR_LEN    256
#define TRP_MANIFEST_MAX_FALLBACKS  8

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

    /* Error list */
    char errors[TRP_MANIFEST_MAX_ERRORS][TRP_MANIFEST_ERROR_LEN];
    int  error_count;
} trp_manifest_t;

/* Parse manifest text into *out. Returns 0 on success, -1 if errors found. */
int trp_manifest_parse(const char *text, uint32_t len, trp_manifest_t *out);

/* Validate that Execute at file exists on the VFS. */
int trp_manifest_validate(trp_manifest_t *out);

/* Print all collected errors to VGA + serial. */
void trp_manifest_print_errors(const trp_manifest_t *m, int gui_mode);

/* Prompt user to allow a high-resource request. Returns 1=yes, 0=no. */
int trp_manifest_resource_prompt(int gui_mode);

/*
 * Full execution gate: parse → validate → resource check → return entry.
 * On success (0) entry_out holds the execute_at filename.
 * On failure (-1) errors have already been printed.
 */
int trp_manifest_run_gate(const char *manifest_text,
                           uint32_t    len,
                           char       *entry_out,
                           int         entry_out_len,
                           int         gui_mode);

#endif /* _TRP_MANIFEST_H */