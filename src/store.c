#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xxhash.h>

/* Object header size: 1 byte type + 8 bytes size */
#define HEADER_SIZE 9

struct splice_store {
    char *path;
};

/* Internal: build the object path from an OID.
 * Returns a malloc'd string that the caller must free, or NULL on error. */
static char *object_path(const splice_store *store, const splice_oid *oid)
{
    size_t path_len = strlen(store->path);
    /* path/objects/xx/xxxxxxxxxxxxxxxx + null */
    size_t total = path_len + 1 + 7 + 1 + 2 + 1 + 14 + 1;
    char *path = malloc(total);
    if (!path) return NULL;

    snprintf(path, total, "%s/objects/%.2s/%s",
             store->path, oid->hex, oid->hex + 2);
    return path;
}

/* Internal: ensure the shard directory exists for the given OID. */
static int ensure_shard_dir(const splice_store *store, const splice_oid *oid)
{
    size_t path_len = strlen(store->path);
    size_t total = path_len + 1 + 7 + 1 + 2 + 1;
    char *dir = malloc(total);
    if (!dir) return -1;

    snprintf(dir, total, "%s/objects/%.2s", store->path, oid->hex);

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

/* Internal: compute hash of serialized object data. */
static uint64_t compute_hash(const void *data, size_t len)
{
    return XXH64(data, len, 0);
}

/* Internal: serialize object header into buffer. */
static void write_header(uint8_t *buf, splice_object_type type, size_t len)
{
    buf[0] = (uint8_t)type;
    /* big-endian size */
    buf[1] = (uint8_t)((len >> 56) & 0xFF);
    buf[2] = (uint8_t)((len >> 48) & 0xFF);
    buf[3] = (uint8_t)((len >> 40) & 0xFF);
    buf[4] = (uint8_t)((len >> 32) & 0xFF);
    buf[5] = (uint8_t)((len >> 24) & 0xFF);
    buf[6] = (uint8_t)((len >> 16) & 0xFF);
    buf[7] = (uint8_t)((len >>  8) & 0xFF);
    buf[8] = (uint8_t)((len >>  0) & 0xFF);
}

/* Internal: deserialize object header from buffer.
 * Returns 0 on success, -1 on error. */
static int read_header(const uint8_t *buf, splice_object_type *type, size_t *len)
{
    *type = (splice_object_type)buf[0];
    *len = ((size_t)buf[1] << 56) |
           ((size_t)buf[2] << 48) |
           ((size_t)buf[3] << 40) |
           ((size_t)buf[4] << 32) |
           ((size_t)buf[5] << 24) |
           ((size_t)buf[6] << 16) |
           ((size_t)buf[7] <<  8) |
           ((size_t)buf[8] <<  0);
    return 0;
}

/* Internal: populate hex string from hash value. */
static void hash_to_hex(uint64_t hash, char *hex)
{
    snprintf(hex, SPLICE_OID_HEXSZ, "%016llx", (unsigned long long)hash);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

splice_store *splice_store_open(const char *path)
{
    splice_store *store = calloc(1, sizeof(*store));
    if (!store) return NULL;

    store->path = strdup(path);
    if (!store->path) {
        free(store);
        return NULL;
    }

    /* Create base directory if it doesn't exist */
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            free(store->path);
            free(store);
            return NULL;
        }
    }

    /* Create objects directory */
    size_t obj_dir_len = strlen(path) + 1 + 7 + 1;
    char *obj_dir = malloc(obj_dir_len);
    if (!obj_dir) {
        free(store->path);
        free(store);
        return NULL;
    }
    snprintf(obj_dir, obj_dir_len, "%s/objects", path);

    if (stat(obj_dir, &st) != 0) {
        if (mkdir(obj_dir, 0755) != 0 && errno != EEXIST) {
            free(obj_dir);
            free(store->path);
            free(store);
            return NULL;
        }
    }

    free(obj_dir);
    return store;
}

void splice_store_close(splice_store *store)
{
    if (!store) return;
    free(store->path);
    free(store);
}

int splice_object_write_typed(splice_store *store,
                              splice_object_type type,
                              const void *data,
                              size_t len,
                              splice_oid *out_oid)
{
    if (!store || !data || !out_oid) return -1;

    /* Build serialized object in memory to compute hash */
    size_t total_size = HEADER_SIZE + len;
    uint8_t *obj_buf = malloc(total_size);
    if (!obj_buf) return -1;

    write_header(obj_buf, type, len);
    memcpy(obj_buf + HEADER_SIZE, data, len);

    /* Compute hash */
    uint64_t hash = compute_hash(obj_buf, total_size);
    out_oid->hash = hash;
    hash_to_hex(hash, out_oid->hex);

    /* Build object path */
    char *opath = object_path(store, out_oid);
    if (!opath) {
        free(obj_buf);
        return -1;
    }

    /* If object already exists, we're done (content-addressable dedup) */
    struct stat st;
    if (stat(opath, &st) == 0) {
        free(opath);
        free(obj_buf);
        return 0;
    }

    /* Ensure shard directory exists */
    if (ensure_shard_dir(store, out_oid) != 0) {
        free(opath);
        free(obj_buf);
        return -1;
    }

    /* Atomic write: write to temp file, then rename */
    size_t tmp_len = strlen(opath) + 5;
    char *tmp_path = malloc(tmp_len);
    if (!tmp_path) {
        free(opath);
        free(obj_buf);
        return -1;
    }
    snprintf(tmp_path, tmp_len, "%s.tmp", opath);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(tmp_path);
        free(opath);
        free(obj_buf);
        return -1;
    }

    ssize_t written = write(fd, obj_buf, total_size);
    close(fd);

    if ((size_t)written != total_size) {
        unlink(tmp_path);
        free(tmp_path);
        free(opath);
        free(obj_buf);
        return -1;
    }

    if (rename(tmp_path, opath) != 0) {
        unlink(tmp_path);
        free(tmp_path);
        free(opath);
        free(obj_buf);
        return -1;
    }

    free(tmp_path);
    free(opath);
    free(obj_buf);
    return 0;
}

int splice_object_write(splice_store *store,
                        const void *data,
                        size_t len,
                        splice_oid *out_oid)
{
    return splice_object_write_typed(store, SPLICE_OBJ_BLOB, data, len, out_oid);
}

int splice_object_read(splice_store *store,
                       const splice_oid *oid,
                       void **out_data,
                       size_t *out_len)
{
    if (!store || !oid || !out_data || !out_len) return -1;

    char *opath = object_path(store, oid);
    if (!opath) return -1;

    FILE *fp = fopen(opath, "rb");
    free(opath);
    if (!fp) return -1;

    /* Read header */
    uint8_t header[HEADER_SIZE];
    if (fread(header, 1, HEADER_SIZE, fp) != HEADER_SIZE) {
        fclose(fp);
        return -1;
    }

    splice_object_type type;
    size_t len;
    if (read_header(header, &type, &len) != 0) {
        fclose(fp);
        return -1;
    }

    /* Allocate and read data */
    void *data = malloc(len);
    if (!data) {
        fclose(fp);
        return -1;
    }

    if (fread(data, 1, len, fp) != len) {
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_data = data;
    *out_len = len;
    return 0;
}

int splice_object_exists(splice_store *store, const splice_oid *oid)
{
    if (!store || !oid) return -1;

    char *opath = object_path(store, oid);
    if (!opath) return -1;

    struct stat st;
    int ret = (stat(opath, &st) == 0) ? 1 : 0;
    free(opath);
    return ret;
}

void splice_oid_to_hex(const splice_oid *oid, char *out_hex)
{
    if (!oid || !out_hex) return;
    memcpy(out_hex, oid->hex, SPLICE_OID_HEXSZ);
}

int splice_oid_from_hex(const char *hex, splice_oid *out_oid)
{
    if (!hex || !out_oid) return -1;
    if (strlen(hex) != 16) return -1;

    /* Parse hex string to uint64_t */
    unsigned long long hash = 0;
    for (int i = 0; i < 16; i++) {
        char c = hex[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return -1;
        hash = (hash << 4) | digit;
    }

    out_oid->hash = (uint64_t)hash;
    memcpy(out_oid->hex, hex, 16);
    out_oid->hex[16] = '\0';
    return 0;
}

int splice_oid_cmp(const splice_oid *a, const splice_oid *b)
{
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    if (a->hash < b->hash) return -1;
    if (a->hash > b->hash) return 1;
    return 0;
}
