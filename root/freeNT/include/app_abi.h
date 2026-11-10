#ifndef APP_ABI_H
#define APP_ABI_H

#define SYS_GUI      64
#define SYS_APPSVC   65
#define SYS_APPCALL  77

#define GUI_FILL      1
#define GUI_TEXT      2
#define GUI_IMAGE     3
#define GUI_TITLE     4
#define GUI_PIXELS    5

#define GUI_F_TRANSPARENT 1u
#define GUI_F_SCALE_SHIFT 8

typedef struct {
    unsigned int  win;
    int           x, y, w, h;
    unsigned int  fg, bg, flags;
    unsigned long ptr;
} gui_cmd_t;

#define SVC_READDIR   1
#define SVC_CFG_GET   2
#define SVC_CFG_SET   3
#define SVC_UPTIME_MS 4
#define SVC_SYSINFO   5
#define SVC_STAT      6
#define SVC_FONT_W    7
#define SVC_FONT_H    8

#define DIRENT_FILE 1
#define DIRENT_DIR  2

typedef struct {
    unsigned long long size;
    unsigned int       type;
    unsigned int       _pad;
    unsigned long long mtime;
} app_stat_t;

typedef struct {
    unsigned long long ram_total_kb;
    unsigned long long ram_free_kb;
    unsigned int       fb_width;
    unsigned int       fb_height;
    unsigned long long uptime_ms;
    unsigned int       ip;
    unsigned int       has_nic;
    unsigned char      mac[6];
    unsigned char      _pad[2];
    char               os_name[32];
    char               os_version[32];
    char               kernel_name[32];
    char               arch[16];
} app_sysinfo_t;

#define CALL_YIELD  0
#define CALL_SHELL  1
#define CALL_SPAWN  2

typedef struct {
    const char    *cwd;
    const char    *cmd;
    char          *out;
    unsigned long  outmax;
    long           len;
} shell_req_t;

#define WM_EVENT_CLOSE_VALUE 6

#endif
