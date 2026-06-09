#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_checkout"
#define CHECKOUT_DIR "/tmp/splice_test_checkout_worktree"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s", TEST_DIR, CHECKOUT_DIR);
    system(cmd);
}

static void test_checkout_basic(void)
{
    printf("  test_checkout_basic ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(CHECKOUT_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    /* Create two blobs */
    splice_oid blob1_oid, blob2_oid;
    assert(splice_object_write(store, "hello", 5, &blob1_oid) == 0);
    assert(splice_object_write(store, "world", 5, &blob2_oid) == 0);

    /* Build tree */
    splice_tree_entry entries[2] = {
        {0100644, "a.txt", {0}},
        {0100644, "dir/b.txt", {0}}
    };
    memcpy(entries[0].oid.hex, blob1_oid.hex, sizeof(blob1_oid.hex));
    entries[0].oid.hash = blob1_oid.hash;
    memcpy(entries[1].oid.hex, blob2_oid.hex, sizeof(blob2_oid.hex));
    entries[1].oid.hash = blob2_oid.hash;

    splice_oid tree_oid;
    assert(splice_tree_build(store, entries, 2, &tree_oid) == 0);

    /* Checkout */
    assert(splice_checkout(store, &tree_oid, CHECKOUT_DIR) == 0);

    /* Verify files */
    FILE *fp = fopen(CHECKOUT_DIR "/a.txt", "rb");
    assert(fp != NULL);
    char buf[16];
    size_t n = fread(buf, 1, 16, fp);
    assert(n == 5);
    assert(memcmp(buf, "hello", 5) == 0);
    fclose(fp);

    fp = fopen(CHECKOUT_DIR "/dir/b.txt", "rb");
    assert(fp != NULL);
    n = fread(buf, 1, 16, fp);
    assert(n == 5);
    assert(memcmp(buf, "world", 5) == 0);
    fclose(fp);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_checkout_lazy(void)
{
    printf("  test_checkout_lazy ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(CHECKOUT_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob_oid;
    assert(splice_object_write(store, "real content", 12, &blob_oid) == 0);

    splice_tree_entry entry = {0100644, "lazy.txt", {0}};
    memcpy(entry.oid.hex, blob_oid.hex, sizeof(blob_oid.hex));
    entry.oid.hash = blob_oid.hash;

    splice_oid tree_oid;
    assert(splice_tree_build(store, &entry, 1, &tree_oid) == 0);

    assert(splice_checkout_lazy(store, &tree_oid, CHECKOUT_DIR) == 0);

    /* Verify placeholder */
    FILE *fp = fopen(CHECKOUT_DIR "/lazy.txt", "r");
    assert(fp != NULL);
    char buf[64];
    assert(fgets(buf, sizeof(buf), fp) != NULL);
    assert(strncmp(buf, "SPLICE_LAZY:", 12) == 0);
    fclose(fp);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_materialize(void)
{
    printf("  test_materialize ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(CHECKOUT_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob_oid;
    assert(splice_object_write(store, "real content", 12, &blob_oid) == 0);

    splice_tree_entry entry = {0100644, "mat.txt", {0}};
    memcpy(entry.oid.hex, blob_oid.hex, sizeof(blob_oid.hex));
    entry.oid.hash = blob_oid.hash;

    splice_oid tree_oid;
    assert(splice_tree_build(store, &entry, 1, &tree_oid) == 0);

    assert(splice_checkout_lazy(store, &tree_oid, CHECKOUT_DIR) == 0);
    assert(splice_materialize(store, CHECKOUT_DIR "/mat.txt") == 0);

    /* Verify actual content */
    FILE *fp = fopen(CHECKOUT_DIR "/mat.txt", "rb");
    assert(fp != NULL);
    char buf[16];
    size_t n = fread(buf, 1, 16, fp);
    assert(n == 12);
    assert(memcmp(buf, "real content", 12) == 0);
    fclose(fp);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_checkout_null_args(void)
{
    printf("  test_checkout_null_args ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    splice_oid oid;
    memset(&oid, 0xAB, sizeof(oid));
    snprintf(oid.hex, sizeof(oid.hex), "aaaaaaaaaaaaaaaa");

    assert(splice_checkout(NULL, &oid, CHECKOUT_DIR) == -1);
    assert(splice_checkout(store, NULL, CHECKOUT_DIR) == -1);
    assert(splice_checkout(store, &oid, NULL) == -1);
    assert(splice_checkout_lazy(NULL, &oid, CHECKOUT_DIR) == -1);
    assert(splice_materialize(NULL, CHECKOUT_DIR "/x") == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running checkout tests...\n");

    test_checkout_basic();
    test_checkout_lazy();
    test_materialize();
    test_checkout_null_args();

    printf("\nAll checkout tests passed!\n");
    return 0;
}
