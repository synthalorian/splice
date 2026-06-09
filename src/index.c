#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Index file format (binary):
 *   [version: 4 bytes BE]        -- currently 1
 *   [entry_count: 4 bytes BE]
 *   for each entry (sorted by path for canonical form):
 *     [mode: 4 bytes BE]
 *     [path_len: 2 bytes BE]
 *     [path: path_len bytes]
 *     [oid.hex: 16 bytes]
 */

#define INDEX_VERSION 1

static void write_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >>  8) & 0xFF);
    buf[3] = (uint8_t)((val >>  0) & 0xFF);
}

static void write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)((val >> 0) & 0xFF);
}

static uint32_t read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] <<  8) |
           ((uint32_t)buf[3] <<  0);
}

static uint16_t read_u16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) |
           ((uint16_t)buf[1] << 0);
}

static int entry_cmp(const void *a, const void *b)
{
    const splice_index_entry *ea = a;
    const splice_index_entry *eb = b;
    return strcmp(ea->path, eb->path);
}

static char *index_path(const char *store_path)
{
    size_t len = strlen(store_path);
    size_t total = len + 1 + 5 + 1;
    char *path = malloc(total);
    if (!path) return NULL;
    snprintf(path, total, "%s/index", store_path);
    return path;
}

int splice_index_read(const char *store_path, splice_index *out_index)
{
    if (!store_path || !out_index) return -1;

    memset(out_index, 0, sizeof(*out_index));

    char *path = index_path(store_path);
    if (!path) return -1;

    FILE *fp = fopen(path, "rb");
    free(path);
    if (!fp) {
        /* No index file yet -- return empty index */
        return 0;
    }

    uint8_t header[8];
    if (fread(header, 1, 8, fp) != 8) {
        fclose(fp);
        return -1;
    }

    uint32_t version = read_u32_be(header);
    if (version != INDEX_VERSION) {
        fclose(fp);
        return -1;
    }

    uint32_t count = read_u32_be(header + 4);
    if (count == 0) {
        fclose(fp);
        return 0;
    }

    splice_index_entry *entries = calloc(count, sizeof(*entries));
    if (!entries) {
        fclose(fp);
        return -1;
    }

    uint32_t parsed = 0;
    for (; parsed < count; parsed++) {
        uint8_t mode_buf[4];
        if (fread(mode_buf, 1, 4, fp) != 4) goto fail;
        entries[parsed].mode = read_u32_be(mode_buf);

        uint8_t len_buf[2];
        if (fread(len_buf, 1, 2, fp) != 2) goto fail;
        uint16_t path_len = read_u16_be(len_buf);

        entries[parsed].path = malloc(path_len + 1);
        if (!entries[parsed].path) goto fail;
        if (fread(entries[parsed].path, 1, path_len, fp) != path_len) goto fail;
        entries[parsed].path[path_len] = '\0';

        if (fread(entries[parsed].oid.hex, 1, 16, fp) != 16) goto fail;
        entries[parsed].oid.hex[16] = '\0';
        splice_oid_from_hex(entries[parsed].oid.hex, &entries[parsed].oid);
    }

    fclose(fp);
    out_index->entries = entries;
    out_index->count = count;
    out_index->capacity = count;
    return 0;

fail:
    splice_index_free(&(splice_index){ .entries = entries, .count = parsed });
    fclose(fp);
    return -1;
}

int splice_index_write(const char *store_path, const splice_index *index)
{
    if (!store_path || !index) return -1;

    char *path = index_path(store_path);
    if (!path) return -1;

    size_t tmp_len = strlen(path) + 5;
    char *tmp_path = malloc(tmp_len);
    if (!tmp_path) {
        free(path);
        return -1;
    }
    snprintf(tmp_path, tmp_len, "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(tmp_path);
        free(path);
        return -1;
    }

    uint8_t header[8];
    write_u32_be(header, INDEX_VERSION);
    write_u32_be(header + 4, (uint32_t)index->count);
    if (fwrite(header, 1, 8, fp) != 8) goto fail;

    for (size_t i = 0; i < index->count; i++) {
        const splice_index_entry *e = &index->entries[i];
        size_t path_len = strlen(e->path);
        if (path_len > 65535) goto fail;

        uint8_t mode_buf[4];
        write_u32_be(mode_buf, e->mode);
        if (fwrite(mode_buf, 1, 4, fp) != 4) goto fail;

        uint8_t len_buf[2];
        write_u16_be(len_buf, (uint16_t)path_len);
        if (fwrite(len_buf, 1, 2, fp) != 2) goto fail;

        if (fwrite(e->path, 1, path_len, fp) != path_len) goto fail;
        if (fwrite(e->oid.hex, 1, 16, fp) != 16) goto fail;
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

fail:
    fclose(fp);
    unlink(tmp_path);
    free(tmp_path);
    free(path);
    return -1;
}

int splice_index_add(splice_index *index, const char *path,
                     const splice_oid *oid, uint32_t mode)
{
    if (!index || !path || !oid) return -1;

    /* Check if path already exists -- update in place */
    for (size_t i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            index->entries[i].oid = *oid;
            index->entries[i].mode = mode;
            return 0;
        }
    }

    /* Grow capacity if needed */
    if (index->count >= index->capacity) {
        size_t new_cap = index->capacity == 0 ? 8 : index->capacity * 2;
        splice_index_entry *new_entries = realloc(index->entries,
                                                   new_cap * sizeof(*new_entries));
        if (!new_entries) return -1;
        index->entries = new_entries;
        index->capacity = new_cap;
    }

    /* Append new entry */
    index->entries[index->count].path = strdup(path);
    if (!index->entries[index->count].path) return -1;
    index->entries[index->count].oid = *oid;
    index->entries[index->count].mode = mode;
    index->count++;

    /* Sort to maintain canonical order */
    qsort(index->entries, index->count, sizeof(*index->entries), entry_cmp);

    return 0;
}

int splice_index_remove(splice_index *index, const char *path)
{
    if (!index || !path) return -1;

    for (size_t i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            free(index->entries[i].path);
            /* Shift remaining entries down */
            memmove(&index->entries[i], &index->entries[i + 1],
                    (index->count - i - 1) * sizeof(*index->entries));
            index->count--;
            return 0;
        }
    }

    return -1; /* not found */
}

void splice_index_free(splice_index *index)
{
    if (!index) return;
    for (size_t i = 0; i < index->count; i++) {
        free(index->entries[i].path);
    }
    free(index->entries);
    memset(index, 0, sizeof(*index));
}
