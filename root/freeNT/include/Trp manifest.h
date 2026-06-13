#ifndef _TRP_MANIFEST_H
#define _TRP_MANIFEST_H


#include "types.h"

/* ── Limits ─────────────────────────────────────────────────────────────── */
#define TRP_MANIFEST_MAX_ERRORS       16
#define TRP_MANIFEST_ERROR_LEN       256
#define TRP_MANIFEST_MAX_FALLBACKS     8

/* ── Parsed manifest result ──────────────────────────────────────────────── */
typedef struct {
    /* Required — Execute at */
    char execute_at[256];             /* primary entry-point filename         */
    int  execute_at_found;            /* 1 if the directive appeared at all   */
    int  execute_at_line;             /* line number of primary directive      */

    /* Additional Execute at entries become ordered fallbacks */
    char fallback_execute_at[TRP_MANIFEST_MAX_FALLBACKS][256];
    int  fallback_count;

    /* Optional stuff */
    char window_name[128];          
    char priority[16];              
    char execute_if[256];         
    int  mark_executable;           
    char language[32];               
    char version[64];               
    char assets[256];               
    int  debug_mode;                  
    int  resource_request_high;       
    int  resource_request_line;       

    /* Error list — all errors collected before reporting */
    char errors[TRP_MANIFEST_MAX_ERRORS][TRP_MANIFEST_ERROR_LEN];
    int  error_count;
} trp_manifest_t;

int trp_manifest_validate(trp_manifest_t *out);


void trp_manifest_print_errors(const trp_manifest_t *m, int gui_mode);


 
int trp_manifest_resource_prompt(int gui_mode);



int trp_manifest_run_gate(const char  *manifest_text,
                           uint32_t    len,
                           char       *entry_out,
                           int         entry_out_len,
                           int         gui_mode);

#endif /* _TRP_MANIFEST_H */