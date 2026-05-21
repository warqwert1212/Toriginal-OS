#ifndef PKGMAN_H
#define PKGMAN_H

#define MAX_PACKAGES 100
#define MAX_FILES 50
#define MAX_NAME_LEN 64
#define MAX_VERSION_LEN 32
#define MAX_DESC_LEN 256
#define MAX_PATH_LEN 256

typedef struct {
    char filepath[MAX_PATH_LEN];
    char filename[MAX_NAME_LEN];
} PackageFile;

typedef struct {
    char name[MAX_NAME_LEN];
    char version[MAX_VERSION_LEN];
    char description[MAX_DESC_LEN];
    int installed;
    int size;
    int file_count;
    PackageFile files[MAX_FILES];
} Package;

/* Function declarations */
int pkg_init();
Package* pkg_create(const char *name, const char *version, const char *description);
int pkg_add_file(Package *pkg, const char *filepath, const char *filename);
int pkg_install(const char *pkg_name);
int pkg_uninstall(const char *pkg_name);
void pkg_list_all();
void pkg_search(const char *query);
Package* pkg_info(const char *pkg_name);

#endif /* PKGMAN_H */
