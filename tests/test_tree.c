#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_tree"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void test_tree_empty(void)
{
    printf("  test_tree_empty ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid tree_oid;
    int ret = splice_tree_build(store, NULL, 0, &tree_oid);
    assert(ret == 0);
    assert(strlen(tree_oid.hex) == 16);

    splice_tree_entry *entries = NULL;
    size_t count = 0;
    ret = splice_tree_parse(store, &tree_oid, &entries, &count);
    assert(ret == 0);
    assert(count == 0);
    assert(entries == NULL);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_tree_single_entry(void)
{
    printf("  test_tree_single_entry ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data = "file content";
    splice_oid blob_oid;
    int ret = splice_object_write(store, data, strlen(data), &blob_oid);
    assert(ret == 0);

    splice_tree_entry entries[1];
    entries[0].mode = 0100644;
    entries[0].name = "hello.txt";
    entries[0].oid = blob_oid;

    splice_oid tree_oid;
    ret = splice_tree_build(store, entries, 1, &tree_oid);
    assert(ret == 0);

    splice_tree_entry *parsed = NULL;
    size_t count = 0;
    ret = splice_tree_parse(store, &tree_oid, &parsed, &count);
    assert(ret == 0);
    assert(count == 1);
    assert(parsed[0].mode == 0100644);
    assert(strcmp(parsed[0].name, "hello.txt") == 0);
    assert(splice_oid_cmp(&parsed[0].oid, &blob_oid) == 0);

    splice_tree_entries_free(parsed, count);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_tree_multiple_entries_sorted(void)
{
    printf("  test_tree_multiple_entries_sorted ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data1 = "content1";
    const char *data2 = "content2";
    const char *data3 = "content3";

    splice_oid oid1, oid2, oid3;
    splice_object_write(store, data1, strlen(data1), &oid1);
    splice_object_write(store, data2, strlen(data2), &oid2);
    splice_object_write(store, data3, strlen(data3), &oid3);

    /* Insert in non-sorted order */
    splice_tree_entry entries[3];
    entries[0].mode = 0100644; entries[0].name = "zebra.txt"; entries[0].oid = oid3;
    entries[1].mode = 0100755; entries[1].name = "apple.txt"; entries[1].oid = oid1;
    entries[2].mode = 0100644; entries[2].name = "mango.txt"; entries[2].oid = oid2;

    splice_oid tree_oid;
    int ret = splice_tree_build(store, entries, 3, &tree_oid);
    assert(ret == 0);

    splice_tree_entry *parsed = NULL;
    size_t count = 0;
    ret = splice_tree_parse(store, &tree_oid, &parsed, &count);
    assert(ret == 0);
    assert(count == 3);

    /* Should be sorted alphabetically */
    assert(strcmp(parsed[0].name, "apple.txt") == 0);
    assert(parsed[0].mode == 0100755);
    assert(strcmp(parsed[1].name, "mango.txt") == 0);
    assert(strcmp(parsed[2].name, "zebra.txt") == 0);

    splice_tree_entries_free(parsed, count);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_tree_roundtrip_binary_names(void)
{
    printf("  test_tree_roundtrip_binary_names ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data = "binary content";
    splice_oid blob_oid;
    splice_object_write(store, data, strlen(data), &blob_oid);

    splice_tree_entry entries[1];
    entries[0].mode = 0100644;
    entries[0].name = "file\x01\x02\x03.bin";
    entries[0].oid = blob_oid;

    splice_oid tree_oid;
    int ret = splice_tree_build(store, entries, 1, &tree_oid);
    assert(ret == 0);

    splice_tree_entry *parsed = NULL;
    size_t count = 0;
    ret = splice_tree_parse(store, &tree_oid, &parsed, &count);
    assert(ret == 0);
    assert(count == 1);
    assert(strcmp(parsed[0].name, "file\x01\x02\x03.bin") == 0);

    splice_tree_entries_free(parsed, count);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_tree_content_addressable(void)
{
    printf("  test_tree_content_addressable ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    const char *data = "same content";
    splice_oid blob_oid;
    splice_object_write(store, data, strlen(data), &blob_oid);

    splice_tree_entry entries[2];
    entries[0].mode = 0100644; entries[0].name = "a.txt"; entries[0].oid = blob_oid;
    entries[1].mode = 0100644; entries[1].name = "b.txt"; entries[1].oid = blob_oid;

    splice_oid tree_oid1, tree_oid2;
    int ret = splice_tree_build(store, entries, 2, &tree_oid1);
    assert(ret == 0);

    /* Build again with same entries (different order but same content after sort) */
    splice_tree_entry entries2[2];
    entries2[0].mode = 0100644; entries2[0].name = "a.txt"; entries2[0].oid = blob_oid;
    entries2[1].mode = 0100644; entries2[1].name = "b.txt"; entries2[1].oid = blob_oid;

    ret = splice_tree_build(store, entries2, 2, &tree_oid2);
    assert(ret == 0);

    /* Same tree content should produce same OID */
    assert(splice_oid_cmp(&tree_oid1, &tree_oid2) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_tree_null_args(void)
{
    printf("  test_tree_null_args ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    assert(splice_tree_build(NULL, NULL, 0, &oid) == -1);
    assert(splice_tree_build(store, NULL, 0, NULL) == -1);

    splice_tree_entry *entries = NULL;
    size_t count = 0;
    assert(splice_tree_parse(NULL, &oid, &entries, &count) == -1);
    assert(splice_tree_parse(store, NULL, &entries, &count) == -1);
    assert(splice_tree_parse(store, &oid, NULL, &count) == -1);
    assert(splice_tree_parse(store, &oid, &entries, NULL) == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running tree tests...\n");

    test_tree_empty();
    test_tree_single_entry();
    test_tree_multiple_entries_sorted();
    test_tree_roundtrip_binary_names();
    test_tree_content_addressable();
    test_tree_null_args();

    printf("\nAll tree tests passed!\n");
    return 0;
}
