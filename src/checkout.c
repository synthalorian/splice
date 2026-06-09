#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>

/* Internal: create all directories in a path */
static int mkdir_all(const char *path)
{
    char *tmp = strdup(path);
    if (!tmp) return -1;
    char *p = tmp;
    if (*p == '/') p++;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(tmp, 0755);
    free(tmp);
    return 0;
}

/* Internal: write data to file at path, creating parent dirs */
static int write_file(const char *path, const void *data, size_t len, uint32_t mode)
{
    char *dir = strdup(path);
    if (!dir) return -1;
    char *d = dirname(dir);
    if (strcmp(d, ".") != 0 && strcmp(d, "/") != 0) {
        mkdir_all(d);
    }
    free(dir);

    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    chmod(path, mode & 0777);
    return 0;
}

int splice_checkout(splice_store *store, const splice_oid *tree_oid, const char *target_path)
{
    if (!store || !tree_oid || !target_path) return -1;

    splice_tree_entry *entries = NULL;
    size_t count = 0;
    if (splice_tree_parse(store, tree_oid, &entries, &count) != 0)
        return -1;

    int ret = 0;
    for (size_t i = 0; i < count; i++) {
        size_t path_len = strlen(target_path) + 1 + strlen(entries[i].name) + 1;
        char *file_path = malloc(path_len);
        if (!file_path) {
            ret = -1;
            break;
        }
        snprintf(file_path, path_len, "%s/%s", target_path, entries[i].name);

        void *data = NULL;
        size_t len = 0;
        if (splice_object_read(store, &entries[i].oid, &data, &len) != 0) {
            free(file_path);
            ret = -1;
            continue;
        }

        if (write_file(file_path, data, len, entries[i].mode) != 0) {
            free(data);
            free(file_path);
            ret = -1;
            continue;
        }

        free(data);
        free(file_path);
    }

    splice_tree_entries_free(entries, count);
    return ret;
}

int splice_checkout_lazy(splice_store *store, const splice_oid *tree_oid, const char *target_path)
{
    if (!store || !tree_oid || !target_path) return -1;

    splice_tree_entry *entries = NULL;
    size_t count = 0;
    if (splice_tree_parse(store, tree_oid, &entries, &count) != 0)
        return -1;

    int ret = 0;
    for (size_t i = 0; i < count; i++) {
        size_t path_len = strlen(target_path) + 1 + strlen(entries[i].name) + 1;
        char *file_path = malloc(path_len);
        if (!file_path) {
            ret = -1;
            break;
        }
        snprintf(file_path, path_len, "%s/%s", target_path, entries[i].name);

        char placeholder[64];
        snprintf(placeholder, sizeof(placeholder), "SPLICE_LAZY:%.16s\n", entries[i].oid.hex);

        if (write_file(file_path, placeholder, strlen(placeholder), entries[i].mode) != 0) {
            free(file_path);
            ret = -1;
            continue;
        }

        free(file_path);
    }

    splice_tree_entries_free(entries, count);
    return ret;
}

int splice_materialize(splice_store *store, const char *path)
{
    if (!store || !path) return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    char buf[64];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (strncmp(buf, "SPLICE_LAZY:", 12) != 0)
        return -1;

    char hex[17];
    strncpy(hex, buf + 12, 16);
    hex[16] = '\0';

    size_t len = strlen(hex);
    if (len > 0 && hex[len - 1] == '\n')
        hex[len - 1] = '\0';

    splice_oid oid;
    if (splice_oid_from_hex(hex, &oid) != 0)
        return -1;

    void *data = NULL;
    size_t data_len = 0;
    if (splice_object_read(store, &oid, &data, &data_len) != 0)
        return -1;

    struct stat st;
    uint32_t mode = 0100644;
    if (stat(path, &st) == 0) {
        mode = st.st_mode;
    }

    int ret = write_file(path, data, data_len, mode);
    free(data);
    return ret;
}
