#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* Access store internals directly (same project) */
struct splice_store {
    char *path;
};

static char *build_ref_path(const splice_store *store, const char *name)
{
    size_t base_len = strlen(store->path);
    size_t name_len = strlen(name);
    /* path/refs/<name> + null */
    size_t total = base_len + 1 + 4 + 1 + name_len + 1;
    char *path = malloc(total);
    if (!path)
        return NULL;
    snprintf(path, total, "%s/refs/%s", store->path, name);
    return path;
}

static int ensure_refs_dir(const splice_store *store)
{
    size_t base_len = strlen(store->path);
    size_t total = base_len + 1 + 4 + 1;
    char *dir = malloc(total);
    if (!dir)
        return -1;
    snprintf(dir, total, "%s/refs", store->path);

    struct stat st;
    if (stat(dir, &st) == 0) {
        free(dir);
        return 0;
    }

    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        free(dir);
        return -1;
    }

    free(dir);
    return 0;
}

static int ensure_ref_dir_for_name(const splice_store *store, const char *name)
{
    /* Find last slash in name to create parent dirs */
    const char *slash = strrchr(name, '/');
    if (!slash)
        return 0;

    size_t base_len = strlen(store->path);
    size_t dir_len = slash - name;
    size_t total = base_len + 1 + 4 + 1 + dir_len + 1;
    char *dir = malloc(total);
    if (!dir)
        return -1;

    snprintf(dir, total, "%s/refs/", store->path);
    size_t prefix_len = strlen(dir);
    memcpy(dir + prefix_len, name, dir_len);
    dir[prefix_len + dir_len] = '\0';

    /* Create all intermediate directories */
    char *p = dir + prefix_len;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(dir, 0755);

    free(dir);
    return 0;
}

int splice_ref_write(splice_store *store, const char *name, const splice_oid *oid)
{
    if (!store || !name || !oid)
        return -1;

    if (ensure_refs_dir(store) != 0)
        return -1;

    if (ensure_ref_dir_for_name(store, name) != 0)
        return -1;

    char *path = build_ref_path(store, name);
    if (!path)
        return -1;

    FILE *fp = fopen(path, "w");
    free(path);
    if (!fp)
        return -1;

    fprintf(fp, "%.16s\n", oid->hex);
    fclose(fp);
    return 0;
}

int splice_ref_read(splice_store *store, const char *name, splice_oid *out_oid)
{
    if (!store || !name || !out_oid)
        return -1;

    char *path = build_ref_path(store, name);
    if (!path)
        return -1;

    FILE *fp = fopen(path, "r");
    free(path);
    if (!fp)
        return -1;

    char hex[64];
    if (!fgets(hex, sizeof(hex), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Strip newline */
    size_t len = strlen(hex);
    if (len > 0 && hex[len - 1] == '\n')
        hex[len - 1] = '\0';

    return splice_oid_from_hex(hex, out_oid);
}

int splice_ref_exists(splice_store *store, const char *name)
{
    if (!store || !name)
        return -1;

    char *path = build_ref_path(store, name);
    if (!path)
        return -1;

    struct stat st;
    int ret = (stat(path, &st) == 0) ? 1 : 0;
    free(path);
    return ret;
}

int splice_ref_delete(splice_store *store, const char *name)
{
    if (!store || !name)
        return -1;

    char *path = build_ref_path(store, name);
    if (!path)
        return -1;

    int ret = (unlink(path) == 0) ? 0 : -1;
    free(path);
    return ret;
}

int splice_head_write(splice_store *store, const char *ref_name)
{
    if (!store || !ref_name)
        return -1;

    size_t base_len = strlen(store->path);
    size_t total = base_len + 1 + 4 + 1;
    char *path = malloc(total);
    if (!path)
        return -1;
    snprintf(path, total, "%s/HEAD", store->path);

    FILE *fp = fopen(path, "w");
    free(path);
    if (!fp)
        return -1;

    fprintf(fp, "ref: %s\n", ref_name);
    fclose(fp);
    return 0;
}

int splice_head_read(splice_store *store, char *out_ref_name, size_t max_len)
{
    if (!store || !out_ref_name || max_len == 0)
        return -1;

    size_t base_len = strlen(store->path);
    size_t total = base_len + 1 + 4 + 1;
    char *path = malloc(total);
    if (!path)
        return -1;
    snprintf(path, total, "%s/HEAD", store->path);

    FILE *fp = fopen(path, "r");
    free(path);
    if (!fp)
        return -1;

    char buf[256];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Strip newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    /* Parse "ref: <refname>" format */
    if (strncmp(buf, "ref: ", 5) != 0)
        return -1;

    const char *ref = buf + 5;
    size_t ref_len = strlen(ref);
    if (ref_len >= max_len)
        return -1;

    memcpy(out_ref_name, ref, ref_len + 1);
    return 0;
}
