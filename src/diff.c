#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Internal: read a file from the working directory for comparison.
 * Returns 0 on success, -1 on error. On success, out_data is malloc'd. */
static int read_workdir_file(const char *path, void **out_data, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }

    rewind(fp);

    void *data = malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return -1;
    }

    if (size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_data = data;
    *out_len = (size_t)size;
    return 0;
}

/* Internal: compute blob OID for a working directory file. */
static int workdir_file_oid(splice_store *store, const char *path, splice_oid *out_oid)
{
    void *data = NULL;
    size_t len = 0;
    if (read_workdir_file(path, &data, &len) != 0)
        return -1;

    int ret = splice_object_write(store, data, len, out_oid);
    free(data);
    return ret;
}

/* Print a single diff status line to stdout. */
static void print_diff_status(char status, const char *path)
{
    printf("%-4c%s\n", status, path);
}

int splice_diff_trees(splice_store *store,
                      const splice_oid *old_tree_oid,
                      const splice_oid *new_tree_oid)
{
    if (!store || !old_tree_oid || !new_tree_oid)
        return -1;

    splice_tree_entry *old_entries = NULL;
    size_t old_count = 0;
    splice_tree_entry *new_entries = NULL;
    size_t new_count = 0;

    if (splice_tree_parse(store, old_tree_oid, &old_entries, &old_count) != 0)
        return -1;
    if (splice_tree_parse(store, new_tree_oid, &new_entries, &new_count) != 0) {
        splice_tree_entries_free(old_entries, old_count);
        return -1;
    }

    /* Both trees are sorted by name. Walk them together. */
    size_t i = 0, j = 0;
    while (i < old_count && j < new_count) {
        int cmp = strcmp(old_entries[i].name, new_entries[j].name);
        if (cmp < 0) {
            /* Deleted in new */
            print_diff_status('D', old_entries[i].name);
            i++;
        } else if (cmp > 0) {
            /* Added in new */
            print_diff_status('A', new_entries[j].name);
            j++;
        } else {
            /* Same name: check OID/mode */
            if (splice_oid_cmp(&old_entries[i].oid, &new_entries[j].oid) != 0 ||
                old_entries[i].mode != new_entries[j].mode) {
                print_diff_status('M', old_entries[i].name);
            }
            i++;
            j++;
        }
    }

    while (i < old_count) {
        print_diff_status('D', old_entries[i].name);
        i++;
    }

    while (j < new_count) {
        print_diff_status('A', new_entries[j].name);
        j++;
    }

    splice_tree_entries_free(old_entries, old_count);
    splice_tree_entries_free(new_entries, new_count);
    return 0;
}

int splice_diff_tree_workdir(splice_store *store,
                             const splice_oid *tree_oid,
                             const char *base_path)
{
    if (!store || !tree_oid || !base_path)
        return -1;

    splice_tree_entry *entries = NULL;
    size_t count = 0;
    if (splice_tree_parse(store, tree_oid, &entries, &count) != 0)
        return -1;

    for (size_t i = 0; i < count; i++) {
        const char *name = entries[i].name;

        /* Build full path */
        size_t base_len = strlen(base_path);
        size_t name_len = strlen(name);
        size_t total = base_len + 1 + name_len + 1;
        char *full_path = malloc(total);
        if (!full_path) {
            splice_tree_entries_free(entries, count);
            return -1;
        }
        snprintf(full_path, total, "%s/%s", base_path, name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            /* File exists in tree but not in workdir */
            print_diff_status('D', name);
            free(full_path);
            continue;
        }

        splice_oid file_oid;
        if (workdir_file_oid(store, full_path, &file_oid) != 0) {
            free(full_path);
            continue;
        }

        if (splice_oid_cmp(&entries[i].oid, &file_oid) != 0) {
            print_diff_status('M', name);
        }

        free(full_path);
    }

    splice_tree_entries_free(entries, count);
    return 0;
}

int splice_diff_commits(splice_store *store,
                        const splice_oid *old_commit_oid,
                        const splice_oid *new_commit_oid)
{
    if (!store || !old_commit_oid || !new_commit_oid)
        return -1;

    splice_commit old_commit, new_commit;
    memset(&old_commit, 0, sizeof(old_commit));
    memset(&new_commit, 0, sizeof(new_commit));

    if (splice_commit_read(store, old_commit_oid, &old_commit) != 0)
        return -1;
    if (splice_commit_read(store, new_commit_oid, &new_commit) != 0) {
        splice_commit_free(&old_commit);
        return -1;
    }

    int ret = splice_diff_trees(store, &old_commit.tree_oid, &new_commit.tree_oid);

    splice_commit_free(&old_commit);
    splice_commit_free(&new_commit);
    return ret;
}
