#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tree serialization format (binary):
 *   [count: 4 bytes BE]
 *   for each entry (sorted by name):
 *     [mode: 4 bytes BE]
 *     [name_len: 2 bytes BE]
 *     [name: name_len bytes]
 *     [oid.hex: 16 bytes]
 */

/* Internal: compare two tree entries by name for qsort. */
static int entry_cmp(const void *a, const void *b)
{
    const splice_tree_entry *ea = a;
    const splice_tree_entry *eb = b;
    return strcmp(ea->name, eb->name);
}

/* Internal: serialize uint32_t as big-endian. */
static void write_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >>  8) & 0xFF);
    buf[3] = (uint8_t)((val >>  0) & 0xFF);
}

/* Internal: serialize uint16_t as big-endian. */
static void write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)((val >> 0) & 0xFF);
}

/* Internal: deserialize uint32_t from big-endian. */
static uint32_t read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] <<  8) |
           ((uint32_t)buf[3] <<  0);
}

/* Internal: deserialize uint16_t from big-endian. */
static uint16_t read_u16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) |
           ((uint16_t)buf[1] << 0);
}

int splice_tree_build(splice_store *store,
                      splice_tree_entry *entries,
                      size_t count,
                      splice_oid *out_tree_oid)
{
    if (!store || !out_tree_oid)
        return -1;
    if (count > 0 && !entries)
        return -1;

    /* Sort entries by name to ensure canonical form */
    if (count > 1) {
        qsort(entries, count, sizeof(*entries), entry_cmp);
    }

    /* Compute serialized size */
    size_t data_size = 4; /* count */
    for (size_t i = 0; i < count; i++) {
        size_t name_len = strlen(entries[i].name);
        if (name_len > 65535)
            return -1; /* name too long */
        data_size += 4 + 2 + name_len + 16;
    }

    uint8_t *data = malloc(data_size);
    if (!data)
        return -1;

    uint8_t *p = data;
    write_u32_be(p, (uint32_t)count);
    p += 4;

    for (size_t i = 0; i < count; i++) {
        write_u32_be(p, entries[i].mode);
        p += 4;

        size_t name_len = strlen(entries[i].name);
        write_u16_be(p, (uint16_t)name_len);
        p += 2;

        memcpy(p, entries[i].name, name_len);
        p += name_len;

        memcpy(p, entries[i].oid.hex, 16);
        p += 16;
    }

    int ret = splice_object_write_typed(store, SPLICE_OBJ_TREE, data, data_size, out_tree_oid);
    free(data);
    return ret;
}

int splice_tree_parse(splice_store *store,
                      const splice_oid *tree_oid,
                      splice_tree_entry **out_entries,
                      size_t *out_count)
{
    if (!store || !tree_oid || !out_entries || !out_count)
        return -1;

    void *data = NULL;
    size_t data_size = 0;
    if (splice_object_read(store, tree_oid, &data, &data_size) != 0)
        return -1;

    if (data_size < 4) {
        free(data);
        return -1;
    }

    const uint8_t *p = data;
    uint32_t count = read_u32_be(p);
    p += 4;

    if (count == 0) {
        free(data);
        *out_entries = NULL;
        *out_count = 0;
        return 0;
    }

    splice_tree_entry *entries = calloc(count, sizeof(*entries));
    if (!entries) {
        free(data);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        if ((size_t)(p - (uint8_t *)data) + 4 + 2 > data_size) {
            splice_tree_entries_free(entries, i);
            free(data);
            return -1;
        }

        entries[i].mode = read_u32_be(p);
        p += 4;

        uint16_t name_len = read_u16_be(p);
        p += 2;

        if ((size_t)(p - (uint8_t *)data) + name_len + 16 > data_size) {
            splice_tree_entries_free(entries, i);
            free(data);
            return -1;
        }

        entries[i].name = malloc(name_len + 1);
        if (!entries[i].name) {
            splice_tree_entries_free(entries, i);
            free(data);
            return -1;
        }
        memcpy(entries[i].name, p, name_len);
        entries[i].name[name_len] = '\0';
        p += name_len;

        memcpy(entries[i].oid.hex, p, 16);
        entries[i].oid.hex[16] = '\0';
        p += 16;

        /* Parse hash from hex */
        splice_oid_from_hex(entries[i].oid.hex, &entries[i].oid);
    }

    free(data);
    *out_entries = entries;
    *out_count = count;
    return 0;
}

void splice_tree_entries_free(splice_tree_entry *entries, size_t count)
{
    if (!entries)
        return;
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
    }
    free(entries);
}
