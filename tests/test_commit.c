#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_commit"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void test_commit_create_read(void)
{
    printf("  test_commit_create_read ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    /* Create a tree */
    const char *data = "file content";
    splice_oid blob_oid;
    splice_object_write(store, data, strlen(data), &blob_oid);

    splice_tree_entry tree_entries[1];
    tree_entries[0].mode = 0100644;
    tree_entries[0].name = "file.txt";
    tree_entries[0].oid = blob_oid;

    splice_oid tree_oid;
    int ret = splice_tree_build(store, tree_entries, 1, &tree_oid);
    assert(ret == 0);

    /* Create a commit */
    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree_oid;
    commit.author = "Test Author";
    commit.timestamp = 1234567890;
    commit.message = "Initial commit";

    splice_oid commit_oid;
    ret = splice_commit_create(store, &commit, &commit_oid);
    assert(ret == 0);
    assert(strlen(commit_oid.hex) == 16);

    /* Read it back */
    splice_commit parsed;
    ret = splice_commit_read(store, &commit_oid, &parsed);
    assert(ret == 0);
    assert(splice_oid_cmp(&parsed.tree_oid, &tree_oid) == 0);
    assert(strcmp(parsed.author, "Test Author") == 0);
    assert(parsed.timestamp == 1234567890);
    assert(strcmp(parsed.message, "Initial commit") == 0);
    assert(parsed.parent_count == 0);

    splice_commit_free(&parsed);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_commit_with_parent(void)
{
    printf("  test_commit_with_parent ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    /* First commit (root) */
    splice_oid tree_oid1;
    splice_tree_entry te1[1] = {{0100644, "a.txt", {0}}};
    splice_object_write(store, "v1", 2, &te1[0].oid);
    splice_tree_build(store, te1, 1, &tree_oid1);

    splice_commit commit1;
    memset(&commit1, 0, sizeof(commit1));
    commit1.tree_oid = tree_oid1;
    commit1.author = "Author";
    commit1.timestamp = 1000;
    commit1.message = "First";

    splice_oid commit_oid1;
    splice_commit_create(store, &commit1, &commit_oid1);

    /* Second commit (with parent) */
    splice_oid tree_oid2;
    splice_tree_entry te2[1] = {{0100644, "a.txt", {0}}};
    splice_object_write(store, "v2", 2, &te2[0].oid);
    splice_tree_build(store, te2, 1, &tree_oid2);

    splice_oid parents[1];
    parents[0] = commit_oid1;

    splice_commit commit2;
    memset(&commit2, 0, sizeof(commit2));
    commit2.tree_oid = tree_oid2;
    commit2.parent_oids = parents;
    commit2.parent_count = 1;
    commit2.author = "Author";
    commit2.timestamp = 2000;
    commit2.message = "Second";

    splice_oid commit_oid2;
    int ret = splice_commit_create(store, &commit2, &commit_oid2);
    assert(ret == 0);

    splice_commit parsed;
    ret = splice_commit_read(store, &commit_oid2, &parsed);
    assert(ret == 0);
    assert(parsed.parent_count == 1);
    assert(splice_oid_cmp(&parsed.parent_oids[0], &commit_oid1) == 0);
    assert(strcmp(parsed.message, "Second") == 0);

    splice_commit_free(&parsed);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_commit_root_no_parents(void)
{
    printf("  test_commit_root_no_parents ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid tree_oid;
    splice_tree_entry te[1] = {{0100644, "root.txt", {0}}};
    splice_object_write(store, "root", 4, &te[0].oid);
    splice_tree_build(store, te, 1, &tree_oid);

    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree_oid;
    commit.author = "Root";
    commit.timestamp = 0;
    commit.message = "";

    splice_oid commit_oid;
    splice_commit_create(store, &commit, &commit_oid);

    splice_commit parsed;
    splice_commit_read(store, &commit_oid, &parsed);
    assert(parsed.parent_count == 0);
    assert(parsed.parent_oids == NULL);
    assert(strcmp(parsed.message, "") == 0);

    splice_commit_free(&parsed);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_commit_multiline_message(void)
{
    printf("  test_commit_multiline_message ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid tree_oid;
    splice_tree_entry te[1] = {{0100644, "f.txt", {0}}};
    splice_object_write(store, "data", 4, &te[0].oid);
    splice_tree_build(store, te, 1, &tree_oid);

    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree_oid;
    commit.author = "A";
    commit.timestamp = 1;
    commit.message = "Line one\nLine two\nLine three";

    splice_oid commit_oid;
    splice_commit_create(store, &commit, &commit_oid);

    splice_commit parsed;
    splice_commit_read(store, &commit_oid, &parsed);
    assert(strcmp(parsed.message, "Line one\nLine two\nLine three") == 0);

    splice_commit_free(&parsed);
    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_commit_null_args(void)
{
    printf("  test_commit_null_args ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    splice_commit commit;
    memset(&commit, 0, sizeof(commit));

    assert(splice_commit_create(NULL, &commit, &oid) == -1);
    assert(splice_commit_create(store, NULL, &oid) == -1);
    assert(splice_commit_create(store, &commit, NULL) == -1);

    assert(splice_commit_read(NULL, &oid, &commit) == -1);
    assert(splice_commit_read(store, NULL, &commit) == -1);
    assert(splice_commit_read(store, &oid, NULL) == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_commit_invalid_oid(void)
{
    printf("  test_commit_invalid_oid ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid bad_oid;
    memset(&bad_oid, 0, sizeof(bad_oid));
    strcpy(bad_oid.hex, "0000000000000000");
    bad_oid.hash = 0;

    splice_commit parsed;
    int ret = splice_commit_read(store, &bad_oid, &parsed);
    assert(ret == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running commit tests...\n");

    test_commit_create_read();
    test_commit_with_parent();
    test_commit_root_no_parents();
    test_commit_multiline_message();
    test_commit_null_args();
    test_commit_invalid_oid();

    printf("\nAll commit tests passed!\n");
    return 0;
}
