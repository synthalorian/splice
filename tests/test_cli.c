#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_cli"

static void cleanup(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void test_index_empty(void)
{
    printf("  test_index_empty ... ");
    cleanup();

    splice_index index;
    int ret = splice_index_read(TEST_DIR, &index);
    assert(ret == 0);
    assert(index.count == 0);
    assert(index.entries == NULL);

    splice_index_free(&index);
    cleanup();
    printf("OK\n");
}

static void test_index_add_and_read(void)
{
    printf("  test_index_add_and_read ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_index index;
    memset(&index, 0, sizeof(index));

    splice_oid oid1, oid2;
    memset(&oid1, 0xAB, sizeof(oid1));
    snprintf(oid1.hex, sizeof(oid1.hex), "aaaaaaaaaaaaaaaa");
    memset(&oid2, 0xCD, sizeof(oid2));
    snprintf(oid2.hex, sizeof(oid2.hex), "bbbbbbbbbbbbbbbb");

    assert(splice_index_add(&index, "file1.txt", &oid1, 0100644) == 0);
    assert(splice_index_add(&index, "dir/file2.txt", &oid2, 0100755) == 0);
    assert(index.count == 2);

    assert(splice_index_write(TEST_DIR, &index) == 0);
    splice_index_free(&index);

    splice_index read_back;
    assert(splice_index_read(TEST_DIR, &read_back) == 0);
    assert(read_back.count == 2);

    /* Entries should be sorted by path */
    assert(strcmp(read_back.entries[0].path, "dir/file2.txt") == 0);
    assert(read_back.entries[0].mode == 0100755);
    assert(strcmp(read_back.entries[0].oid.hex, "bbbbbbbbbbbbbbbb") == 0);

    assert(strcmp(read_back.entries[1].path, "file1.txt") == 0);
    assert(read_back.entries[1].mode == 0100644);
    assert(strcmp(read_back.entries[1].oid.hex, "aaaaaaaaaaaaaaaa") == 0);

    splice_index_free(&read_back);
    cleanup();
    printf("OK\n");
}

static void test_index_update_existing(void)
{
    printf("  test_index_update_existing ... ");
    cleanup();

    splice_index index;
    memset(&index, 0, sizeof(index));

    splice_oid oid1, oid2;
    memset(&oid1, 0xAB, sizeof(oid1));
    snprintf(oid1.hex, sizeof(oid1.hex), "aaaaaaaaaaaaaaaa");
    memset(&oid2, 0xCD, sizeof(oid2));
    snprintf(oid2.hex, sizeof(oid2.hex), "bbbbbbbbbbbbbbbb");

    splice_index_add(&index, "file.txt", &oid1, 0100644);
    splice_index_add(&index, "file.txt", &oid2, 0100755);

    assert(index.count == 1);
    assert(strcmp(index.entries[0].oid.hex, "bbbbbbbbbbbbbbbb") == 0);
    assert(index.entries[0].mode == 0100755);

    splice_index_free(&index);
    cleanup();
    printf("OK\n");
}

static void test_index_remove(void)
{
    printf("  test_index_remove ... ");
    cleanup();

    splice_index index;
    memset(&index, 0, sizeof(index));

    splice_oid oid1, oid2;
    memset(&oid1, 0xAB, sizeof(oid1));
    snprintf(oid1.hex, sizeof(oid1.hex), "aaaaaaaaaaaaaaaa");
    memset(&oid2, 0xCD, sizeof(oid2));
    snprintf(oid2.hex, sizeof(oid2.hex), "bbbbbbbbbbbbbbbb");

    splice_index_add(&index, "a.txt", &oid1, 0100644);
    splice_index_add(&index, "b.txt", &oid2, 0100644);
    assert(index.count == 2);

    assert(splice_index_remove(&index, "a.txt") == 0);
    assert(index.count == 1);
    assert(strcmp(index.entries[0].path, "b.txt") == 0);

    assert(splice_index_remove(&index, "nonexistent.txt") == -1);

    splice_index_free(&index);
    cleanup();
    printf("OK\n");
}

static void test_index_clear(void)
{
    printf("  test_index_clear ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_index index;
    memset(&index, 0, sizeof(index));

    splice_oid oid;
    memset(&oid, 0xAB, sizeof(oid));
    snprintf(oid.hex, sizeof(oid.hex), "aaaaaaaaaaaaaaaa");

    splice_index_add(&index, "file.txt", &oid, 0100644);
    splice_index_write(TEST_DIR, &index);
    splice_index_free(&index);

    splice_index cleared;
    memset(&cleared, 0, sizeof(cleared));
    splice_index_write(TEST_DIR, &cleared);

    splice_index read_back;
    splice_index_read(TEST_DIR, &read_back);
    assert(read_back.count == 0);

    splice_index_free(&read_back);
    cleanup();
    printf("OK\n");
}

static void test_cli_workflow(void)
{
    printf("  test_cli_workflow ... ");
    cleanup();

    /* Create test directory */
    mkdir(TEST_DIR, 0755);

    /* Change to test directory */
    char orig_cwd[4096];
    getcwd(orig_cwd, sizeof(orig_cwd));
    chdir(TEST_DIR);

    /* Create a test file */
    FILE *fp = fopen("hello.txt", "w");
    fprintf(fp, "Hello, splice!\n");
    fclose(fp);

    /* Initialize repository */
    splice_store *store = splice_store_open(".splice");
    assert(store != NULL);

    /* Create refs/heads directory */
    mkdir(".splice/refs", 0755);
    mkdir(".splice/refs/heads", 0755);

    /* Set HEAD */
    splice_head_write(store, "refs/heads/main");
    splice_store_close(store);

    /* Add file to index */
    splice_index index;
    memset(&index, 0, sizeof(index));

    store = splice_store_open(".splice");
    void *data = NULL;
    size_t len = 0;

    fp = fopen("hello.txt", "rb");
    fseek(fp, 0, SEEK_END);
    len = (size_t)ftell(fp);
    rewind(fp);
    data = malloc(len);
    fread(data, 1, len, fp);
    fclose(fp);

    splice_oid blob_oid;
    splice_object_write(store, data, len, &blob_oid);
    free(data);

    splice_index_add(&index, "hello.txt", &blob_oid, 0100644);
    splice_index_write(".splice", &index);
    splice_index_free(&index);
    splice_store_close(store);

    /* Verify index */
    splice_index read_index;
    splice_index_read(".splice", &read_index);
    assert(read_index.count == 1);
    assert(strcmp(read_index.entries[0].path, "hello.txt") == 0);
    splice_index_free(&read_index);

    /* Create commit */
    store = splice_store_open(".splice");

    splice_oid tree_oid;
    splice_tree_entry te[1] = {{0100644, "hello.txt", {0}}};
    memcpy(te[0].oid.hex, blob_oid.hex, sizeof(blob_oid.hex));
    te[0].oid.hash = blob_oid.hash;
    splice_tree_build(store, te, 1, &tree_oid);

    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree_oid;
    commit.author = "Test Author";
    commit.timestamp = 1234567890;
    commit.message = "Initial commit";

    splice_oid commit_oid;
    splice_commit_create(store, &commit, &commit_oid);

    /* Update ref */
    splice_ref_write(store, "refs/heads/main", &commit_oid);

    splice_store_close(store);

    /* Verify commit exists */
    store = splice_store_open(".splice");
    splice_oid ref_oid;
    assert(splice_ref_read(store, "refs/heads/main", &ref_oid) == 0);
    assert(splice_oid_cmp(&ref_oid, &commit_oid) == 0);

    splice_commit parsed;
    assert(splice_commit_read(store, &ref_oid, &parsed) == 0);
    assert(strcmp(parsed.message, "Initial commit") == 0);
    assert(strcmp(parsed.author, "Test Author") == 0);
    splice_commit_free(&parsed);

    splice_store_close(store);

    /* Clear index after commit */
    splice_index cleared;
    memset(&cleared, 0, sizeof(cleared));
    splice_index_write(".splice", &cleared);

    splice_index after_commit;
    splice_index_read(".splice", &after_commit);
    assert(after_commit.count == 0);
    splice_index_free(&after_commit);

    chdir(orig_cwd);
    cleanup();
    printf("OK\n");
}

static void test_index_null_args(void)
{
    printf("  test_index_null_args ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_index index;
    splice_oid oid;
    memset(&oid, 0, sizeof(oid));
    snprintf(oid.hex, sizeof(oid.hex), "aaaaaaaaaaaaaaaa");

    assert(splice_index_read(NULL, &index) == -1);
    assert(splice_index_read(TEST_DIR, NULL) == -1);
    assert(splice_index_write(NULL, &index) == -1);
    assert(splice_index_write(TEST_DIR, NULL) == -1);
    assert(splice_index_add(NULL, "file", &oid, 0100644) == -1);
    assert(splice_index_add(&index, NULL, &oid, 0100644) == -1);
    assert(splice_index_add(&index, "file", NULL, 0100644) == -1);
    assert(splice_index_remove(NULL, "file") == -1);
    assert(splice_index_remove(&index, NULL) == -1);

    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running CLI tests...\n");

    test_index_empty();
    test_index_add_and_read();
    test_index_update_existing();
    test_index_remove();
    test_index_clear();
    test_cli_workflow();
    test_index_null_args();

    printf("\nAll CLI tests passed!\n");
    return 0;
}
