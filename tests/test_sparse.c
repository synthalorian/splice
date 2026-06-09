#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_sparse"
#define CHECKOUT_DIR "/tmp/splice_test_sparse_worktree"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s", TEST_DIR, CHECKOUT_DIR);
    system(cmd);
}

static void test_sparse_pattern_matching(void)
{
    printf("  test_sparse_pattern_matching ... ");

    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));

    /* Empty patterns include everything */
    assert(splice_sparse_matches(&sc, "foo.txt") == 1);
    assert(splice_sparse_matches(&sc, "bar/baz.txt") == 1);

    /* Simple pattern */
    assert(splice_sparse_add(&sc, "*.txt") == 0);
    assert(splice_sparse_matches(&sc, "foo.txt") == 1);
    assert(splice_sparse_matches(&sc, "foo.bin") == 0);
    assert(splice_sparse_matches(&sc, "dir/bar.txt") == 1);

    /* Directory prefix pattern */
    assert(splice_sparse_add(&sc, "assets/*") == 0);
    assert(splice_sparse_matches(&sc, "assets/image.png") == 1);
    assert(splice_sparse_matches(&sc, "other/image.png") == 0);

    /* Negation pattern */
    assert(splice_sparse_add(&sc, "!*.tmp") == 0);
    assert(splice_sparse_matches(&sc, "foo.txt") == 1);
    assert(splice_sparse_matches(&sc, "scratch.tmp") == 0);

    splice_sparse_free(&sc);
    printf("OK\n");
}

static void test_sparse_load_save(void)
{
    printf("  test_sparse_load_save ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));

    assert(splice_sparse_add(&sc, "*.psd") == 0);
    assert(splice_sparse_add(&sc, "assets/*") == 0);
    assert(splice_sparse_add(&sc, "!*.tmp") == 0);

    assert(splice_sparse_save(TEST_DIR, &sc) == 0);
    splice_sparse_free(&sc);

    splice_sparse_checkout sc2;
    assert(splice_sparse_load(TEST_DIR, &sc2) == 0);
    assert(sc2.count == 3);
    assert(strcmp(sc2.patterns[0], "*.psd") == 0);
    assert(strcmp(sc2.patterns[1], "assets/*") == 0);
    assert(strcmp(sc2.patterns[2], "!*.tmp") == 0);

    assert(splice_sparse_matches(&sc2, "art.psd") == 1);
    assert(splice_sparse_matches(&sc2, "assets/model.fbx") == 1);
    assert(splice_sparse_matches(&sc2, "scratch.tmp") == 0);

    splice_sparse_free(&sc2);
    cleanup();
    printf("OK\n");
}

static void test_sparse_checkout(void)
{
    printf("  test_sparse_checkout ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(CHECKOUT_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    /* Create blobs */
    splice_oid blob1_oid, blob2_oid, blob3_oid;
    assert(splice_object_write(store, "hello", 5, &blob1_oid) == 0);
    assert(splice_object_write(store, "world", 5, &blob2_oid) == 0);
    assert(splice_object_write(store, "ignored", 7, &blob3_oid) == 0);

    /* Build tree with 3 entries */
    splice_tree_entry entries[3] = {
        {0100644, "a.txt", {0}},
        {0100644, "dir/b.txt", {0}},
        {0100644, "ignored.bin", {0}}
    };
    memcpy(entries[0].oid.hex, blob1_oid.hex, sizeof(blob1_oid.hex));
    entries[0].oid.hash = blob1_oid.hash;
    memcpy(entries[1].oid.hex, blob2_oid.hex, sizeof(blob2_oid.hex));
    entries[1].oid.hash = blob2_oid.hash;
    memcpy(entries[2].oid.hex, blob3_oid.hex, sizeof(blob3_oid.hex));
    entries[2].oid.hash = blob3_oid.hash;

    splice_oid tree_oid;
    assert(splice_tree_build(store, entries, 3, &tree_oid) == 0);

    /* Sparse checkout: only *.txt files */
    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));
    assert(splice_sparse_add(&sc, "*.txt") == 0);

    assert(splice_checkout_sparse(store, &tree_oid, CHECKOUT_DIR, &sc) == 0);

    /* Verify a.txt exists */
    FILE *fp = fopen(CHECKOUT_DIR "/a.txt", "rb");
    assert(fp != NULL);
    fclose(fp);

    /* Verify dir/b.txt exists (matches *.txt) */
    fp = fopen(CHECKOUT_DIR "/dir/b.txt", "rb");
    assert(fp != NULL);
    fclose(fp);

    /* Verify ignored.bin does NOT exist */
    fp = fopen(CHECKOUT_DIR "/ignored.bin", "rb");
    assert(fp == NULL);

    splice_sparse_free(&sc);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_sparse_checkout_negation(void)
{
    printf("  test_sparse_checkout_negation ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(CHECKOUT_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob1_oid, blob2_oid;
    assert(splice_object_write(store, "keep", 4, &blob1_oid) == 0);
    assert(splice_object_write(store, "skip", 4, &blob2_oid) == 0);

    splice_tree_entry entries[2] = {
        {0100644, "keep.txt", {0}},
        {0100644, "skip.txt", {0}}
    };
    memcpy(entries[0].oid.hex, blob1_oid.hex, sizeof(blob1_oid.hex));
    entries[0].oid.hash = blob1_oid.hash;
    memcpy(entries[1].oid.hex, blob2_oid.hex, sizeof(blob2_oid.hex));
    entries[1].oid.hash = blob2_oid.hash;

    splice_oid tree_oid;
    assert(splice_tree_build(store, entries, 2, &tree_oid) == 0);

    /* Include all txt, then exclude skip.txt */
    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));
    assert(splice_sparse_add(&sc, "*.txt") == 0);
    assert(splice_sparse_add(&sc, "!skip.txt") == 0);

    assert(splice_checkout_sparse(store, &tree_oid, CHECKOUT_DIR, &sc) == 0);

    FILE *fp = fopen(CHECKOUT_DIR "/keep.txt", "rb");
    assert(fp != NULL);
    fclose(fp);

    fp = fopen(CHECKOUT_DIR "/skip.txt", "rb");
    assert(fp == NULL);

    splice_sparse_free(&sc);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_sparse_null_args(void)
{
    printf("  test_sparse_null_args ... ");

    assert(splice_sparse_load(NULL, NULL) == -1);
    assert(splice_sparse_save(NULL, NULL) == -1);
    assert(splice_sparse_add(NULL, NULL) == -1);
    assert(splice_sparse_remove(NULL, 0) == -1);

    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));
    assert(splice_sparse_matches(&sc, NULL) == 0);
    assert(splice_sparse_matches(NULL, "foo") == 1);

    splice_store *store = splice_store_open(TEST_DIR);
    splice_oid oid;
    memset(&oid, 0xAB, sizeof(oid));
    snprintf(oid.hex, sizeof(oid.hex), "aaaaaaaaaaaaaaaa");

    assert(splice_checkout_sparse(NULL, &oid, CHECKOUT_DIR, &sc) == -1);
    assert(splice_checkout_sparse(store, NULL, CHECKOUT_DIR, &sc) == -1);
    assert(splice_checkout_sparse(store, &oid, NULL, &sc) == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_partial_clone_promise(void)
{
    printf("  test_partial_clone_promise ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    /* Create a local object */
    splice_oid local_oid;
    assert(splice_object_write(store, "local data", 10, &local_oid) == 0);

    /* Create a "remote" OID that doesn't exist locally */
    splice_oid remote_oid;
    memset(&remote_oid, 0, sizeof(remote_oid));
    snprintf(remote_oid.hex, sizeof(remote_oid.hex), "deadbeefdeadbeef");
    remote_oid.hash = 0xdeadbeefdeadbeefULL;

    /* Local object checks */
    assert(splice_object_is_local(store, &local_oid) == 1);
    assert(splice_object_is_local(store, &remote_oid) == 0);

    /* Promise the remote object */
    assert(splice_object_is_promised(store, &remote_oid) == 0);
    assert(splice_object_promise(store, &remote_oid) == 0);
    assert(splice_object_is_promised(store, &remote_oid) == 1);

    /* Local object is not promised */
    assert(splice_object_is_promised(store, &local_oid) == 0);

    /* Promising a local object is a no-op */
    assert(splice_object_promise(store, &local_oid) == 0);
    assert(splice_object_is_promised(store, &local_oid) == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_sparse_remove(void)
{
    printf("  test_sparse_remove ... ");

    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));

    assert(splice_sparse_add(&sc, "*.psd") == 0);
    assert(splice_sparse_add(&sc, "*.fbx") == 0);
    assert(splice_sparse_add(&sc, "*.tmp") == 0);
    assert(sc.count == 3);

    assert(splice_sparse_remove(&sc, 1) == 0);
    assert(sc.count == 2);
    assert(strcmp(sc.patterns[0], "*.psd") == 0);
    assert(strcmp(sc.patterns[1], "*.tmp") == 0);

    assert(splice_sparse_remove(&sc, 5) == -1);
    assert(splice_sparse_remove(&sc, (size_t)-1) == -1);

    splice_sparse_free(&sc);
    printf("OK\n");
}

int main(void)
{
    printf("Running sparse checkout tests...\n");

    test_sparse_pattern_matching();
    test_sparse_load_save();
    test_sparse_checkout();
    test_sparse_checkout_negation();
    test_sparse_null_args();
    test_partial_clone_promise();
    test_sparse_remove();

    printf("\nAll sparse checkout tests passed!\n");
    return 0;
}
