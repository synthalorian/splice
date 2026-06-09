#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#define DELTA_BASE_OID_SIZE 16

int splice_delta_create(splice_store *store,
                        const splice_oid *base_oid,
                        const void *new_data,
                        size_t new_len,
                        splice_oid *out_delta_oid)
{
    if (!store || !base_oid || !new_data || !out_delta_oid)
        return -1;

    void *base_data = NULL;
    size_t base_len = 0;
    if (splice_object_read(store, base_oid, &base_data, &base_len) != 0)
        return -1;

    size_t max_compressed = ZSTD_compressBound(new_len);
    void *compressed = malloc(max_compressed);
    if (!compressed) {
        free(base_data);
        return -1;
    }

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) {
        free(compressed);
        free(base_data);
        return -1;
    }

    size_t compressed_size = ZSTD_compress_usingDict(
        cctx,
        compressed, max_compressed,
        new_data, new_len,
        base_data, base_len,
        3
    );

    ZSTD_freeCCtx(cctx);

    if (ZSTD_isError(compressed_size)) {
        free(compressed);
        free(base_data);
        return -1;
    }

    free(base_data);

    size_t delta_payload_size = DELTA_BASE_OID_SIZE + compressed_size;
    uint8_t *delta_payload = malloc(delta_payload_size);
    if (!delta_payload) {
        free(compressed);
        return -1;
    }

    memcpy(delta_payload, base_oid->hex, DELTA_BASE_OID_SIZE);
    memcpy(delta_payload + DELTA_BASE_OID_SIZE, compressed, compressed_size);
    free(compressed);

    int ret = splice_object_write_typed(store, SPLICE_OBJ_DELTA,
                                        delta_payload, delta_payload_size,
                                        out_delta_oid);
    free(delta_payload);
    return ret;
}

int splice_delta_apply(splice_store *store,
                       const splice_oid *delta_oid,
                       void **out_data,
                       size_t *out_len)
{
    if (!store || !delta_oid || !out_data || !out_len)
        return -1;

    void *delta_payload = NULL;
    size_t delta_payload_size = 0;
    if (splice_object_read(store, delta_oid, &delta_payload, &delta_payload_size) != 0)
        return -1;

    if (delta_payload_size < DELTA_BASE_OID_SIZE) {
        free(delta_payload);
        return -1;
    }

    splice_oid base_oid;
    char base_hex[DELTA_BASE_OID_SIZE + 1];
    memcpy(base_hex, delta_payload, DELTA_BASE_OID_SIZE);
    base_hex[DELTA_BASE_OID_SIZE] = '\0';

    if (splice_oid_from_hex(base_hex, &base_oid) != 0) {
        free(delta_payload);
        return -1;
    }

    void *base_data = NULL;
    size_t base_len = 0;
    if (splice_object_read(store, &base_oid, &base_data, &base_len) != 0) {
        free(delta_payload);
        return -1;
    }

    void *compressed = (uint8_t *)delta_payload + DELTA_BASE_OID_SIZE;
    size_t compressed_size = delta_payload_size - DELTA_BASE_OID_SIZE;

    unsigned long long uncompressed_size = ZSTD_getFrameContentSize(compressed, compressed_size);
    if (uncompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        uncompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        free(base_data);
        free(delta_payload);
        return -1;
    }

    void *result = malloc((size_t)uncompressed_size);
    if (!result) {
        free(base_data);
        free(delta_payload);
        return -1;
    }

    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx) {
        free(result);
        free(base_data);
        free(delta_payload);
        return -1;
    }

    size_t decompressed_size = ZSTD_decompress_usingDict(
        dctx,
        result, (size_t)uncompressed_size,
        compressed, compressed_size,
        base_data, base_len
    );

    ZSTD_freeDCtx(dctx);
    free(base_data);
    free(delta_payload);

    if (ZSTD_isError(decompressed_size)) {
        free(result);
        return -1;
    }

    *out_data = result;
    *out_len = decompressed_size;
    return 0;
}
