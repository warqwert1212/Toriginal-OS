#ifndef _INSTALL_ABI_H
#define _INSTALL_ABI_H

#define SYS_DISK_INFO     70
#define SYS_PART_READ     71
#define SYS_PART_WRITE    72
#define SYS_INSTALL_CTL   73
#define SYS_OOBE_SUBMIT   74
#define SYS_CONSOLE_CTL   75
#define SYS_SYSINFO       76

typedef struct {
    unsigned int       present;
    unsigned int       boot_reserved;
    unsigned long long total_sectors;
    unsigned int       sector_size;
    unsigned int       install_session;
    char               model[48];
} disk_info_t;

typedef struct {
    unsigned long long ram_kb;
    unsigned int       fb_width;
    unsigned int       fb_height;
    unsigned int       has_nic;
    unsigned int       _pad;
} sysinfo_t;

#define INSTALL_CTL_SELECT_TARGET 1
#define INSTALL_CTL_GET_TARGET    2
#define INSTALL_CTL_DONE          3

#define CONSOLE_CTL_CLEAR     1
#define CONSOLE_CTL_SETCOLOR  2

#define INSTALL_PART_ALIGN_LBA 70048u

#endif
