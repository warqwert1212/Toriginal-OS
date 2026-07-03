#ifndef _INSTALLER_H
#define _INSTALLER_H

void installer_run(void);
void installer_run_unattended(void);
int installer_copy_file(const char *src, const char *dst);

/* Prints whether Toriginal OS is installed on the currently mounted TRPFS
 * volume (or that no volume is mounted yet). */
void installer_print_status(void);

/* Called once at boot. Mounts an existing on-disk filesystem if a real
 * ATA disk is present and already has a valid TRPFS volume on it (i.e.
 * 'install' was run in a previous boot). Returns 0 on success, -1 if no
 * disk is present or no filesystem was found. */
int installer_try_automount(void);

#endif /* _INSTALLER_H */
