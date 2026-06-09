#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_diff_log"

static void cleanup(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void make_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "%s", content);
    fclose(fp);
}

static void test_diff_trees_add_remove_modify(void)
{
    printf("  test_diff_trees_add_remove_modify ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob_a, blob_b, blob_c, blob_c2;
    splice_object_write(store, "a content", 9, &blob_a);
    splice_object_write(store, "b content", 9, &blob_b);
    splice_object_write(store, "c content", 9, &blob_c);
    splice_object_write(store, "c modified", 10, &blob_c2);

    splice_tree_entry old_te[3] = {
        {0100644, "a.txt", {0}},
        {0100644, "b.txt", {0}},
        {0100644, "c.txt", {0}},
    };
    old_te[0].oid = blob_a;
    old_te[1].oid = blob_b;
    old_te[2].oid = blob_c;

    splice_oid old_tree;
    splice_tree_build(store, old_te, 3, &old_tree);

    splice_tree_entry new_te[3] = {
        {0100644, "a.txt", {0}},
        {0100644, "c.txt", {0}},
        {0100644, "d.txt", {0}},
    };
    new_te[0].oid = blob_a;
    new_te[1].oid = blob_c2;
    new_te[2].oid = blob_b;

    splice_oid new_tree;
    splice_tree_build(store, new_te, 3, &new_tree);

    int ret = splice_diff_trees(store, &old_tree, &new_tree);
    assert(ret == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_diff_commits(void)
{
    printf("  test_diff_commits ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob1, blob2;
    splice_object_write(store, "v1", 2, &blob1);
    splice_object_write(store, "v2", 2, &blob2);

    splice_tree_entry te1[1] = {{0100644, "file.txt", {0}}};
    te1[0].oid = blob1;
    splice_oid tree1;
    splice_tree_build(store, te1, 1, &tree1);

    splice_commit commit1;
    memset(&commit1, 0, sizeof(commit1));
    commit1.tree_oid = tree1;
    commit1.author = "A";
    commit1.timestamp = 1000;
    commit1.message = "First";

    splice_oid commit_oid1;
    splice_commit_create(store, &commit1, &commit_oid1);

    splice_tree_entry te2[2] = {
        {0100644, "file.txt", {0}},
        {0100644, "new.txt", {0}},
    };
    te2[0].oid = blob2;
    te2[1].oid = blob1;
    splice_oid tree2;
    splice_tree_build(store, te2, 2, &tree2);

    splice_commit commit2;
    memset(&commit2, 0, sizeof(commit2));
    commit2.tree_oid = tree2;
    commit2.parent_oids = &commit_oid1;
    commit2.parent_count = 1;
    commit2.author = "A";
    commit2.timestamp = 2000;
    commit2.message = "Second";

    splice_oid commit_oid2;
    splice_commit_create(store, &commit2, &commit_oid2);

    int ret = splice_diff_commits(store, &commit_oid1, &commit_oid2);
    assert(ret == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_diff_tree_workdir(void)
{
    printf("  test_diff_tree_workdir ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);
    mkdir(TEST_DIR "/work", 0755);

    splice_store *store = splice_store_open(TEST_DIR "/store");
    assert(store != NULL);

    splice_oid blob;
    splice_object_write(store, "original", 8, &blob);

    splice_tree_entry te[1] = {{0100644, "file.txt", {0}}};
    te[0].oid = blob;
    splice_oid tree;
    splice_tree_build(store, te, 1, &tree);

    make_file(TEST_DIR "/work/file.txt", "modified");

    int ret = splice_diff_tree_workdir(store, &tree, TEST_DIR "/work");
    assert(ret == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_log_walk(void)
{
    printf("  test_log_walk ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob;
    splice_object_write(store, "data", 4, &blob);

    splice_tree_entry te[1] = {{0100644, "file.txt", {0}}};
    te[0].oid = blob;
    splice_oid tree;
    splice_tree_build(store, te, 1, &tree);

    splice_commit commit1;
    memset(&commit1, 0, sizeof(commit1));
    commit1.tree_oid = tree;
    commit1.author = "Author One";
    commit1.timestamp = 1000;
    commit1.message = "First commit";

    splice_oid commit_oid1;
    splice_commit_create(store, &commit1, &commit_oid1);

    splice_commit commit2;
    memset(&commit2, 0, sizeof(commit2));
    commit2.tree_oid = tree;
    commit2.parent_oids = &commit_oid1;
    commit2.parent_count = 1;
    commit2.author = "Author Two";
    commit2.timestamp = 2000;
    commit2.message = "Second commit";

    splice_oid commit_oid2;
    splice_commit_create(store, &commit2, &commit_oid2);

    splice_ref_write(store, "refs/heads/main", &commit_oid2);

    int ret = splice_log(store, "refs/heads/main");
    assert(ret == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_log_oid(void)
{
    printf("  test_log_oid ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid blob;
    splice_object_write(store, "data", 4, &blob);

    splice_tree_entry te[1] = {{0100644, "file.txt", {0}}};
    te[0].oid = blob;
    splice_oid tree;
    splice_tree_build(store, te, 1, &tree);

    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree;
    commit.author = "Solo";
    commit.timestamp = 500;
    commit.message = "Only commit";

    splice_oid commit_oid;
    splice_commit_create(store, &commit, &commit_oid);

    int ret = splice_log_oid(store, &commit_oid);
    assert(ret == 0);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

static void test_diff_null_args(void)
{
    printf("  test_diff_null_args ... ");
    cleanup();

    splice_store *store = splice_store_open(TEST_DIR);
    assert(store != NULL);

    splice_oid oid;
    memset(&oid, 0, sizeof(oid));
    strcpy(oid.hex, "0000000000000001");
    oid.hash = 1;

    assert(splice_diff_trees(NULL, &oid, &oid) == -1);
    assert(splice_diff_trees(store, NULL, &oid) == -1);
    assert(splice_diff_trees(store, &oid, NULL) == -1);

    assert(splice_diff_tree_workdir(NULL, &oid, "/tmp") == -1);
    assert(splice_diff_tree_workdir(store, NULL, "/tmp") == -1);
    assert(splice_diff_tree_workdir(store, &oid, NULL) == -1);

    assert(splice_diff_commits(NULL, &oid, &oid) == -1);
    assert(splice_diff_commits(store, NULL, &oid) == -1);
    assert(splice_diff_commits(store, &oid, NULL) == -1);

    assert(splice_log(NULL, "refs/heads/main") == -1);
    assert(splice_log(store, NULL) == -1);

    assert(splice_log_oid(NULL, &oid) == -1);
    assert(splice_log_oid(store, NULL) == -1);

    splice_store_close(store);
    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running diff/log tests...\n");

    test_diff_trees_add_remove_modify();
    test_diff_commits();
    test_diff_tree_workdir();
    test_log_walk();
    test_log_oid();
    test_diff_null_args();

    printf("\nAll diff/log tests passed!\n");
    return 0;
}
