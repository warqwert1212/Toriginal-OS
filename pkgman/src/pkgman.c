#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "../include/pkgman.h"

/* Package manager database */
Package packages[MAX_PACKAGES];
int package_count = 0;

/* Package repository paths */
const char *REPO_PATH = "/sys/userpc/repo";
const char *PACKAGES_PATH = "/sys/userpc/packages";
const char *PKG_DB = "/sys/userpc/packages/packages.db";

/**
 * Initialize package manager
 */
int pkg_init() {
    mkdir(REPO_PATH, 0755);
    mkdir(PACKAGES_PATH, 0755);
    
    printf("Package Manager initialized\n");
    printf("Repository: %s\n", REPO_PATH);
    printf("Packages: %s\n", PACKAGES_PATH);
    
    return 0;
}

/**
 * Create a package
 */
Package* pkg_create(const char *name, const char *version, const char *description) {
    if (package_count >= MAX_PACKAGES) {
        fprintf(stderr, "Error: Maximum packages reached\n");
        return NULL;
    }
    
    Package *pkg = &packages[package_count++];
    strncpy(pkg->name, name, MAX_NAME_LEN - 1);
    strncpy(pkg->version, version, MAX_VERSION_LEN - 1);
    strncpy(pkg->description, description, MAX_DESC_LEN - 1);
    pkg->installed = 0;
    pkg->size = 0;
    
    return pkg;
}

/**
 * Add file to package
 */
int pkg_add_file(Package *pkg, const char *filepath, const char *filename) {
    if (pkg->file_count >= MAX_FILES) {
        fprintf(stderr, "Error: Maximum files in package reached\n");
        return -1;
    }
    
    strncpy(pkg->files[pkg->file_count].filepath, filepath, MAX_PATH_LEN - 1);
    strncpy(pkg->files[pkg->file_count].filename, filename, MAX_NAME_LEN - 1);
    pkg->file_count++;
    
    return 0;
}

/**
 * Install a package
 */
int pkg_install(const char *pkg_name) {
    Package *pkg = NULL;
    
    /* Find package */
    for (int i = 0; i < package_count; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            pkg = &packages[i];
            break;
        }
    }
    
    if (!pkg) {
        fprintf(stderr, "Error: Package '%s' not found\n", pkg_name);
        return -1;
    }
    
    if (pkg->installed) {
        printf("Package '%s' already installed\n", pkg_name);
        return 0;
    }
    
    printf("Installing package: %s (v%s)\n", pkg->name, pkg->version);
    printf("Description: %s\n", pkg->description);
    
    /* Create package directory */
    char pkg_dir[MAX_PATH_LEN];
    snprintf(pkg_dir, MAX_PATH_LEN, "%s/%s", PACKAGES_PATH, pkg->name);
    mkdir(pkg_dir, 0755);
    
    /* Copy files */
    for (int i = 0; i < pkg->file_count; i++) {
        printf("  Installing: %s\n", pkg->files[i].filename);
        /* File installation would happen here */
    }
    
    pkg->installed = 1;
    printf("Package '%s' installed successfully\n", pkg_name);
    
    return 0;
}

/**
 * Uninstall a package
 */
int pkg_uninstall(const char *pkg_name) {
    Package *pkg = NULL;
    
    for (int i = 0; i < package_count; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            pkg = &packages[i];
            break;
        }
    }
    
    if (!pkg) {
        fprintf(stderr, "Error: Package '%s' not found\n", pkg_name);
        return -1;
    }
    
    if (!pkg->installed) {
        printf("Package '%s' not installed\n", pkg_name);
        return 0;
    }
    
    printf("Uninstalling package: %s\n", pkg->name);
    pkg->installed = 0;
    
    return 0;
}

/**
 * List all packages
 */
void pkg_list_all() {
    printf("\nAvailable Packages:\n");
    printf("%-20s %-10s %-40s Status\n", "Name", "Version", "Description");
    printf("================================================================================\n");
    
    for (int i = 0; i < package_count; i++) {
        printf("%-20s %-10s %-40s %s\n",
               packages[i].name,
               packages[i].version,
               packages[i].description,
               packages[i].installed ? "[INSTALLED]" : "");
    }
}

/**
 * Search for packages
 */
void pkg_search(const char *query) {
    printf("\nSearch results for '%s':\n", query);
    printf("%-20s %-10s %-40s\n", "Name", "Version", "Description");
    printf("================================================================================\n");
    
    int found = 0;
    for (int i = 0; i < package_count; i++) {
        if (strstr(packages[i].name, query) || strstr(packages[i].description, query)) {
            printf("%-20s %-10s %-40s\n",
                   packages[i].name,
                   packages[i].version,
                   packages[i].description);
            found++;
        }
    }
    
    if (found == 0) {
        printf("No packages found matching '%s'\n", query);
    }
}

/**
 * Get package info
 */
Package* pkg_info(const char *pkg_name) {
    for (int i = 0; i < package_count; i++) {
        if (strcmp(packages[i].name, pkg_name) == 0) {
            return &packages[i];
        }
    }
    return NULL;
}
