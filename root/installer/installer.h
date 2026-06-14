#ifndef _INSTALLER_H
#define _INSTALLER_H

void installer_run(void);

/* Prints whether Toriginal OS is installed on the currently mounted TRPFS
 * volume (or that no volume is mounted yet). */
void installer_print_status(void);

#endif /* _INSTALLER_H */