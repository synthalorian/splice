#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_delta"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void test_delta_roundtrip(void)
{
    printf("  test_delta_roundtrip ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *base_data = "Hello, world! This is the base content.";
    size_t base_len = strlen(base_data);
    splice_oid base_oid;

    int ret = splice_object_write(store, base_data, base_len, &base_oid);
    assert(ret == 0);

    const char *new_data = "Hello, world! This is the modified content.";
    size_t new_len = strlen(new_data);
    splice_oid delta_oid;

    ret = splice_delta_create(store, &base_oid, new_data, new_len, &delta_oid);
    assert(ret == 0);

    void *reconstructed = NULL;
    size_t reconstructed_len = 0;
    ret = splice_delta_apply(store, &delta_oid, &reconstructed, &reconstructed_len);
    assert(ret == 0);
    assert(reconstructed_len == new_len);
    assert(memcmp(reconstructed, new_data, new_len) == 0);

    free(reconstructed);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_delta_binary_data(void)
{
    printf("  test_delta_binary_data ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    size_t base_len = 4096;
    uint8_t *base_data = malloc(base_len);
    assert(base_data != NULL);
    for (size_t i = 0; i < base_len; i++) {
        base_data[i] = (uint8_t)(i * 3 + 17);
    }

    splice_oid base_oid;
    int ret = splice_object_write(store, base_data, base_len, &base_oid);
    assert(ret == 0);

    uint8_t *new_data = malloc(base_len);
    assert(new_data != NULL);
    memcpy(new_data, base_data, base_len);
    new_data[100] ^= 0xFF;
    new_data[500] ^= 0xFF;
    new_data[1000] ^= 0xFF;

    splice_oid delta_oid;
    ret = splice_delta_create(store, &base_oid, new_data, base_len, &delta_oid);
    assert(ret == 0);

    void *reconstructed = NULL;
    size_t reconstructed_len = 0;
    ret = splice_delta_apply(store, &delta_oid, &reconstructed, &reconstructed_len);
    assert(ret == 0);
    assert(reconstructed_len == base_len);
    assert(memcmp(reconstructed, new_data, base_len) == 0);

    free(reconstructed);
    free(new_data);
    free(base_data);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_delta_compression_ratio(void)
{
    printf("  test_delta_compression_ratio ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    size_t base_len = 8192;
    char *base_data = malloc(base_len);
    assert(base_data != NULL);
    memset(base_data, 'A', base_len);

    splice_oid base_oid;
    int ret = splice_object_write(store, base_data, base_len, &base_oid);
    assert(ret == 0);

    char *new_data = malloc(base_len);
    assert(new_data != NULL);
    memcpy(new_data, base_data, base_len);
    memcpy(new_data + 100, "MODIFIED SECTION HERE", 21);

    splice_oid delta_oid;
    ret = splice_delta_create(store, &base_oid, new_data, base_len, &delta_oid);
    assert(ret == 0);

    void *delta_raw = NULL;
    size_t delta_raw_len = 0;
    ret = splice_object_read(store, &delta_oid, &delta_raw, &delta_raw_len);
    assert(ret == 0);

    assert(delta_raw_len < base_len);

    free(delta_raw);
    free(new_data);
    free(base_data);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_delta_invalid_base(void)
{
    printf("  test_delta_invalid_base ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid bad_oid;
    memset(&bad_oid, 0, sizeof(bad_oid));
    strcpy(bad_oid.hex, "0000000000000000");
    bad_oid.hash = 0;

    const char *new_data = "some data";
    splice_oid delta_oid;

    int ret = splice_delta_create(store, &bad_oid, new_data, strlen(new_data), &delta_oid);
    assert(ret == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_delta_invalid_delta_oid(void)
{
    printf("  test_delta_invalid_delta_oid ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid bad_oid;
    memset(&bad_oid, 0, sizeof(bad_oid));
    strcpy(bad_oid.hex, "0000000000000000");
    bad_oid.hash = 0;

    void *data = NULL;
    size_t len = 0;
    int ret = splice_delta_apply(store, &bad_oid, &data, &len);
    assert(ret == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_delta_null_args(void)
{
    printf("  test_delta_null_args ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    memset(&oid, 0, sizeof(oid));

    assert(splice_delta_create(NULL, &oid, "x", 1, &oid) == -1);
    assert(splice_delta_create(store, NULL, "x", 1, &oid) == -1);
    assert(splice_delta_create(store, &oid, NULL, 1, &oid) == -1);
    assert(splice_delta_create(store, &oid, "x", 1, NULL) == -1);

    void *data = NULL;
    size_t len = 0;
    assert(splice_delta_apply(NULL, &oid, &data, &len) == -1);
    assert(splice_delta_apply(store, NULL, &data, &len) == -1);
    assert(splice_delta_apply(store, &oid, NULL, &len) == -1);
    assert(splice_delta_apply(store, &oid, &data, NULL) == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_delta_object_type(void)
{
    printf("  test_delta_object_type ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *base_data = "base content for type test";
    splice_oid base_oid;
    int ret = splice_object_write(store, base_data, strlen(base_data), &base_oid);
    assert(ret == 0);

    const char *new_data = "modified content for type test";
    splice_oid delta_oid;
    ret = splice_delta_create(store, &base_oid, new_data, strlen(new_data), &delta_oid);
    assert(ret == 0);

    void *delta_raw = NULL;
    size_t delta_raw_len = 0;
    ret = splice_object_read(store, &delta_oid, &delta_raw, &delta_raw_len);
    assert(ret == 0);

    assert(delta_raw_len >= 16);
    assert(memcmp(delta_raw, base_oid.hex, 16) == 0);

    free(delta_raw);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running delta tests...\n");

    test_delta_roundtrip();
    test_delta_binary_data();
    test_delta_compression_ratio();
    test_delta_invalid_base();
    test_delta_invalid_delta_oid();
    test_delta_null_args();
    test_delta_object_type();

    printf("\nAll delta tests passed!\n");
    return 0;
}
