#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_refs"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void test_ref_write_read(void)
{
    printf("  test_ref_write_read ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    splice_oid_from_hex("deadbeef12345678", &oid);

    int ret = splice_ref_write(store, "heads/main", &oid);
    assert(ret == 0);

    splice_oid read_oid;
    ret = splice_ref_read(store, "heads/main", &read_oid);
    assert(ret == 0);
    assert(splice_oid_cmp(&read_oid, &oid) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_ref_exists(void)
{
    printf("  test_ref_exists ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    splice_oid_from_hex("deadbeef12345678", &oid);

    assert(splice_ref_exists(store, "heads/main") == 0);

    splice_ref_write(store, "heads/main", &oid);
    assert(splice_ref_exists(store, "heads/main") == 1);
    assert(splice_ref_exists(store, "heads/other") == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_ref_delete(void)
{
    printf("  test_ref_delete ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    splice_oid_from_hex("deadbeef12345678", &oid);

    splice_ref_write(store, "heads/main", &oid);
    assert(splice_ref_exists(store, "heads/main") == 1);

    int ret = splice_ref_delete(store, "heads/main");
    assert(ret == 0);
    assert(splice_ref_exists(store, "heads/main") == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_head_symbolic_ref(void)
{
    printf("  test_head_symbolic_ref ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    int ret = splice_head_write(store, "refs/heads/main");
    assert(ret == 0);

    char ref_name[128];
    ret = splice_head_read(store, ref_name, sizeof(ref_name));
    assert(ret == 0);
    assert(strcmp(ref_name, "refs/heads/main") == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_ref_multiple_branches(void)
{
    printf("  test_ref_multiple_branches ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid1, oid2;
    splice_oid_from_hex("1111111111111111", &oid1);
    splice_oid_from_hex("2222222222222222", &oid2);

    splice_ref_write(store, "heads/main", &oid1);
    splice_ref_write(store, "heads/feature", &oid2);

    splice_oid read1, read2;
    splice_ref_read(store, "heads/main", &read1);
    splice_ref_read(store, "heads/feature", &read2);

    assert(splice_oid_cmp(&read1, &oid1) == 0);
    assert(splice_oid_cmp(&read2, &oid2) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_ref_null_args(void)
{
    printf("  test_ref_null_args ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    splice_oid_from_hex("deadbeef12345678", &oid);

    assert(splice_ref_write(NULL, "heads/main", &oid) == -1);
    assert(splice_ref_write(store, NULL, &oid) == -1);
    assert(splice_ref_write(store, "heads/main", NULL) == -1);

    splice_oid out;
    assert(splice_ref_read(NULL, "heads/main", &out) == -1);
    assert(splice_ref_read(store, NULL, &out) == -1);
    assert(splice_ref_read(store, "heads/main", NULL) == -1);

    assert(splice_ref_exists(NULL, "heads/main") == -1);
    assert(splice_ref_exists(store, NULL) == -1);

    assert(splice_ref_delete(NULL, "heads/main") == -1);
    assert(splice_ref_delete(store, NULL) == -1);

    assert(splice_head_write(NULL, "refs/heads/main") == -1);
    assert(splice_head_write(store, NULL) == -1);

    char buf[64];
    assert(splice_head_read(NULL, buf, sizeof(buf)) == -1);
    assert(splice_head_read(store, NULL, sizeof(buf)) == -1);
    assert(splice_head_read(store, buf, 0) == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_ref_nested_dirs(void)
{
    printf("  test_ref_nested_dirs ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    splice_oid_from_hex("deadbeef12345678", &oid);

    /* Write to a nested path */
    int ret = splice_ref_write(store, "remotes/origin/main", &oid);
    assert(ret == 0);

    splice_oid read_oid;
    ret = splice_ref_read(store, "remotes/origin/main", &read_oid);
    assert(ret == 0);
    assert(splice_oid_cmp(&read_oid, &oid) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running refs tests...\n");

    test_ref_write_read();
    test_ref_exists();
    test_ref_delete();
    test_head_symbolic_ref();
    test_ref_multiple_branches();
    test_ref_null_args();
    test_ref_nested_dirs();

    printf("\nAll refs tests passed!\n");
    return 0;
}
