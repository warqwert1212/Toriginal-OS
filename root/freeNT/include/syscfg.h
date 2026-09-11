#ifndef _SYSCFG_H
#define _SYSCFG_H

#include <stddef.h>

/* =============================================================================
 * SYSCFG.H — generic key=value store backed by /toriginal_os/config.ini
 *
 * This is the one real place that reads/writes config.ini. Before this,
 * shell.c's load_username(), installer.c's finalize_install(), and
 * the trpm repo-host code each hand-rolled their own tiny parser/writer
 * over a handful of specific keys - fine while there were three of them,
 * not fine once settings (resolution/colors/etc) need the same file.
 *
 * Format on disk: one "key=value\n" pair per line, no sections, no
 * escaping - matches what's already there today so existing installs
 * keep working unmodified.
 * ========================================================================= */

/* Reads the value for `key` into out (NUL-terminated, truncated to fit).
 * Returns 1 if found, 0 if the key or the file itself doesn't exist yet. */
int syscfg_get(const char *key, char *out, size_t out_len);

/* Sets key=value, creating /toriginal_os/config.ini if it doesn't exist
 * yet. If the key is already present its line is replaced in place
 * (value length may differ - the whole file is rewritten); otherwise
 * the pair is appended. Returns 0 on success, -1 on failure (e.g. no
 * filesystem mounted). Does not itself call trpfs_sync() - callers
 * doing several syscfg_set() calls in a row should sync once at the end. */
int syscfg_set(const char *key, const char *value);

/* Convenience: syscfg_get() but returns a default value string when the
 * key is missing, straight into out - callers don't need their own
 * "found ? use it : use default" branch at every call site. */
void syscfg_get_or(const char *key, char *out, size_t out_len, const char *dflt);

#endif /* _SYSCFG_H */
