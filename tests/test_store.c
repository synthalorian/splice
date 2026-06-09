#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_store"

static void cleanup(void) {
    /* Best-effort cleanup of test directory */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void test_store_open_close(void)
{
    printf("  test_store_open_close ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);
    splice_store_close(store);

    /* Verify directories were created */
    struct stat st;
    assert(stat(TEST_DIR, &st) == 0);
    assert(S_ISDIR(st.st_mode));
    assert(stat(TEST_DIR "/objects", &st) == 0);
    assert(S_ISDIR(st.st_mode));

    cleanup();
    printf("OK\n");
}

static void test_object_write_read(void)
{
    printf("  test_object_write_read ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data = "Hello, splice!";
    size_t len = strlen(data);
    splice_oid oid;

    int ret = splice_object_write(store, data, len, &oid);
    assert(ret == 0);
    assert(strlen(oid.hex) == 16);

    /* Read back */
    void *out_data = NULL;
    size_t out_len = 0;
    ret = splice_object_read(store, &oid, &out_data, &out_len);
    assert(ret == 0);
    assert(out_len == len);
    assert(memcmp(out_data, data, len) == 0);

    free(out_data);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_object_exists(void)
{
    printf("  test_object_exists ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data = "test data for exists";
    splice_oid oid;
    int ret = splice_object_write(store, data, strlen(data), &oid);
    assert(ret == 0);

    assert(splice_object_exists(store, &oid) == 1);

    splice_oid bad_oid;
    memset(&bad_oid, 0, sizeof(bad_oid));
    strcpy(bad_oid.hex, "0000000000000000");
    bad_oid.hash = 0;
    assert(splice_object_exists(store, &bad_oid) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_content_dedup(void)
{
    printf("  test_content_dedup ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data = "deduplicated content";
    size_t len = strlen(data);
    splice_oid oid1, oid2;

    int ret = splice_object_write(store, data, len, &oid1);
    assert(ret == 0);

    ret = splice_object_write(store, data, len, &oid2);
    assert(ret == 0);

    /* Same content must produce same OID */
    assert(splice_oid_cmp(&oid1, &oid2) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_binary_data(void)
{
    printf("  test_binary_data ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    /* Create binary data with null bytes */
    size_t len = 256;
    uint8_t *data = malloc(len);
    for (size_t i = 0; i < len; i++) {
        data[i] = (uint8_t)(i * 7 + 13);
    }

    splice_oid oid;
    int ret = splice_object_write(store, data, len, &oid);
    assert(ret == 0);

    void *out_data = NULL;
    size_t out_len = 0;
    ret = splice_object_read(store, &oid, &out_data, &out_len);
    assert(ret == 0);
    assert(out_len == len);
    assert(memcmp(out_data, data, len) == 0);

    free(out_data);
    free(data);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_oid_roundtrip(void)
{
    printf("  test_oid_roundtrip ... ");

    const char *hex = "deadbeef12345678";
    splice_oid oid;
    int ret = splice_oid_from_hex(hex, &oid);
    assert(ret == 0);
    assert(oid.hash == 0xdeadbeef12345678ULL);
    assert(strcmp(oid.hex, hex) == 0);

    char out_hex[SPLICE_OID_HEXSZ];
    splice_oid_to_hex(&oid, out_hex);
    assert(strcmp(out_hex, hex) == 0);

    printf("OK\n");
}

static void test_empty_blob(void)
{
    printf("  test_empty_blob ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    int ret = splice_object_write(store, "", 0, &oid);
    assert(ret == 0);

    assert(splice_object_exists(store, &oid) == 1);

    void *out_data = NULL;
    size_t out_len = 1;  /* should be overwritten to 0 */
    ret = splice_object_read(store, &oid, &out_data, &out_len);
    assert(ret == 0);
    assert(out_len == 0);
    assert(out_data != NULL);  /* malloc(0) may return NULL or non-NULL */

    free(out_data);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running store tests...\n");

    test_store_open_close();
    test_object_write_read();
    test_object_exists();
    test_content_dedup();
    test_binary_data();
    test_oid_roundtrip();
    test_empty_blob();

    printf("\nAll tests passed!\n");
    return 0;
}
