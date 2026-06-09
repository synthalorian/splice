#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>

/* Internal: build path to sparse-checkout file */
static char *sparse_path(const char *store_path)
{
    size_t len = strlen(store_path);
    size_t total = len + 1 + 14 + 1;
    char *path = malloc(total);
    if (!path) return NULL;
    snprintf(path, total, "%s/sparse-checkout", store_path);
    return path;
}

/* Internal: build path to promised-objects file */
static char *promised_path(const char *store_path)
{
    size_t len = strlen(store_path);
    size_t total = len + 1 + 16 + 1;
    char *path = malloc(total);
    if (!path) return NULL;
    snprintf(path, total, "%s/promised-objects", store_path);
    return path;
}

int splice_sparse_load(const char *store_path, splice_sparse_checkout *out_sc)
{
    if (!store_path || !out_sc) return -1;

    memset(out_sc, 0, sizeof(*out_sc));

    char *path = sparse_path(store_path);
    if (!path) return -1;

    FILE *fp = fopen(path, "r");
    free(path);
    if (!fp) {
        /* No sparse-checkout file yet -- return empty */
        return 0;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) > 0) {
        /* Strip trailing newline */
        if (line[linelen - 1] == '\n')
            line[linelen - 1] = '\0';

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (splice_sparse_add(out_sc, line) != 0) {
            free(line);
            fclose(fp);
            splice_sparse_free(out_sc);
            return -1;
        }
    }

    free(line);
    fclose(fp);
    return 0;
}

int splice_sparse_save(const char *store_path, const splice_sparse_checkout *sc)
{
    if (!store_path || !sc) return -1;

    char *path = sparse_path(store_path);
    if (!path) return -1;

    size_t tmp_len = strlen(path) + 5;
    char *tmp_path = malloc(tmp_len);
    if (!tmp_path) {
        free(path);
        return -1;
    }
    snprintf(tmp_path, tmp_len, "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        free(tmp_path);
        free(path);
        return -1;
    }

    for (size_t i = 0; i < sc->count; i++) {
        if (fprintf(fp, "%s\n", sc->patterns[i]) < 0) {
            fclose(fp);
            unlink(tmp_path);
            free(tmp_path);
            free(path);
            return -1;
        }
    }

    fclose(fp);

    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        free(tmp_path);
        free(path);
        return -1;
    }

    free(tmp_path);
    free(path);
    return 0;
}

int splice_sparse_add(splice_sparse_checkout *sc, const char *pattern)
{
    if (!sc || !pattern) return -1;

    if (sc->count >= sc->capacity) {
        size_t new_cap = sc->capacity == 0 ? 8 : sc->capacity * 2;
        char **new_patterns = realloc(sc->patterns, new_cap * sizeof(*new_patterns));
        if (!new_patterns) return -1;
        sc->patterns = new_patterns;
        sc->capacity = new_cap;
    }

    sc->patterns[sc->count] = strdup(pattern);
    if (!sc->patterns[sc->count]) return -1;
    sc->count++;
    return 0;
}

int splice_sparse_remove(splice_sparse_checkout *sc, size_t index)
{
    if (!sc || index >= sc->count) return -1;

    free(sc->patterns[index]);
    memmove(&sc->patterns[index], &sc->patterns[index + 1],
            (sc->count - index - 1) * sizeof(*sc->patterns));
    sc->count--;
    return 0;
}

void splice_sparse_free(splice_sparse_checkout *sc)
{
    if (!sc) return;
    for (size_t i = 0; i < sc->count; i++) {
        free(sc->patterns[i]);
    }
    free(sc->patterns);
    memset(sc, 0, sizeof(*sc));
}

int splice_sparse_matches(const splice_sparse_checkout *sc, const char *path)
{
    if (!path) return 0;
    if (!sc || sc->count == 0) return 1;

    int included = 0;
    for (size_t i = 0; i < sc->count; i++) {
        const char *pattern = sc->patterns[i];
        int negate = 0;

        if (pattern[0] == '!') {
            negate = 1;
            pattern++;
        }

        int match = (fnmatch(pattern, path, 0) == 0);
        if (match) {
            included = negate ? 0 : 1;
        }
    }

    return included;
}

int splice_checkout_sparse(splice_store *store,
                           const splice_oid *tree_oid,
                           const char *target_path,
                           const splice_sparse_checkout *sc)
{
    if (!store || !tree_oid || !target_path) return -1;

    splice_tree_entry *entries = NULL;
    size_t count = 0;
    if (splice_tree_parse(store, tree_oid, &entries, &count) != 0)
        return -1;

    int ret = 0;
    for (size_t i = 0; i < count; i++) {
        /* Check if this entry matches sparse patterns */
        if (sc && sc->count > 0) {
            if (!splice_sparse_matches(sc, entries[i].name)) {
                continue;
            }
        }

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

        /* Reuse write_file from checkout.c - declared static there,
         * so we duplicate the minimal functionality here */
        char *dir = strdup(file_path);
        if (dir) {
            char *last_slash = strrchr(dir, '/');
            if (last_slash) {
                *last_slash = '\0';
                if (strcmp(dir, ".") != 0 && strcmp(dir, "/") != 0) {
                    char *tmp = strdup(dir);
                    if (tmp) {
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
                    }
                }
            }
            free(dir);
        }

        FILE *fp = fopen(file_path, "wb");
        if (!fp) {
            free(data);
            free(file_path);
            ret = -1;
            continue;
        }

        if (len > 0 && fwrite(data, 1, len, fp) != len) {
            fclose(fp);
            free(data);
            free(file_path);
            ret = -1;
            continue;
        }
        fclose(fp);

        chmod(file_path, entries[i].mode & 0777);

        free(data);
        free(file_path);
    }

    splice_tree_entries_free(entries, count);
    return ret;
}

int splice_object_is_local(splice_store *store, const splice_oid *oid)
{
    return splice_object_exists(store, oid);
}

int splice_object_promise(splice_store *store, const splice_oid *oid)
{
    if (!store || !oid) return -1;

    /* Don't promise if already local */
    if (splice_object_exists(store, oid) == 1)
        return 0;

    const char *store_path = splice_store_path(store);
    if (!store_path) return -1;
    char *path = promised_path(store_path);
    if (!path) return -1;

    FILE *fp = fopen(path, "a");
    if (!fp) {
        /* Try creating the file */
        fp = fopen(path, "w");
        if (!fp) {
            free(path);
            return -1;
        }
    }

    int ret = fprintf(fp, "%s\n", oid->hex);
    fclose(fp);
    free(path);
    return (ret > 0) ? 0 : -1;
}

int splice_object_is_promised(splice_store *store, const splice_oid *oid)
{
    if (!store || !oid) return -1;

    /* If it's local, it's not promised */
    if (splice_object_exists(store, oid) == 1)
        return 0;

    const char *store_path = splice_store_path(store);
    if (!store_path) return -1;
    char *path = promised_path(store_path);
    if (!path) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        free(path);
        return 0;
    }

    char line[32];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (strcmp(line, oid->hex) == 0) {
            found = 1;
            break;
        }
    }

    fclose(fp);
    free(path);
    return found;
}
