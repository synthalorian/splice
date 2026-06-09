#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "splice.h"

#define TEST_DIR "/tmp/splice_test_lib"

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

static void test_version(void)
{
    printf("  test_version ... ");

    assert(strcmp(splice_version_string(), "0.1.0") == 0);

    int major = -1, minor = -1, patch = -1;
    splice_version(&major, &minor, &patch);
    assert(major == 0);
    assert(minor == 1);
    assert(patch == 0);

    /* NULL args should be safe */
    splice_version(NULL, NULL, NULL);

    printf("OK\n");
}

static void test_strerror(void)
{
    printf("  test_strerror ... ");

    assert(strcmp(splice_strerror(SPLICE_OK), "success") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_NOMEM), "out of memory") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_NOTFOUND), "not found") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_EXISTS), "already exists") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_INVALID), "invalid argument") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_IO), "I/O error") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_CORRUPTED), "data corrupted") == 0);
    assert(strcmp(splice_strerror(SPLICE_ERROR_UNKNOWN), "unknown error") == 0);

    printf("OK\n");
}

static void test_repo_init(void)
{
    printf("  test_repo_init ... ");
    cleanup();

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    assert(splice_repo_path(repo) != NULL);
    assert(strstr(splice_repo_path(repo), ".splice") != NULL);

    assert(splice_repo_workdir(repo) != NULL);

    splice_store *store = splice_repo_store(repo);
    assert(store != NULL);

    /* Should fail if repo already exists */
    splice_repo *repo2 = splice_repo_init(TEST_DIR);
    assert(repo2 == NULL);

    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_open(void)
{
    printf("  test_repo_open ... ");
    cleanup();

    /* Should fail if repo doesn't exist */
    splice_repo *repo = splice_repo_open(TEST_DIR);
    assert(repo == NULL);

    /* Create repo first */
    repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);
    splice_repo_close(repo);

    /* Now open should work */
    repo = splice_repo_open(TEST_DIR);
    assert(repo != NULL);
    assert(splice_repo_store(repo) != NULL);

    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_discover(void)
{
    printf("  test_repo_discover ... ");
    cleanup();

    /* Create nested directory structure */
    mkdir(TEST_DIR, 0755);
    mkdir(TEST_DIR "/subdir", 0755);

    /* No repo yet */
    splice_repo *repo = splice_repo_discover(TEST_DIR "/subdir");
    assert(repo == NULL);

    /* Initialize repo */
    repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);
    splice_repo_close(repo);

    /* Discover from subdir */
    repo = splice_repo_discover(TEST_DIR "/subdir");
    assert(repo != NULL);
    assert(splice_repo_store(repo) != NULL);

    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_add(void)
{
    printf("  test_repo_add ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    make_file(TEST_DIR "/hello.txt", "Hello, splice!");

    const char *paths[] = { "hello.txt" };
    /* Note: add paths relative to working directory, but we're in the same dir */
    /* Actually splice_repo_add uses read_file which needs absolute or cwd-relative */
    /* Let's change to the workdir */
    char orig_cwd[4096];
    getcwd(orig_cwd, sizeof(orig_cwd));
    chdir(TEST_DIR);

    int ret = splice_repo_add(repo, paths, 1);
    assert(ret == 0);

    /* Verify index */
    splice_index index;
    ret = splice_index_read(splice_repo_path(repo), &index);
    assert(ret == 0);
    assert(index.count == 1);
    assert(strcmp(index.entries[0].path, "hello.txt") == 0);
    splice_index_free(&index);

    chdir(orig_cwd);
    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_commit(void)
{
    printf("  test_repo_commit ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    make_file(TEST_DIR "/hello.txt", "Hello, splice!");

    char orig_cwd[4096];
    getcwd(orig_cwd, sizeof(orig_cwd));
    chdir(TEST_DIR);

    const char *paths[] = { "hello.txt" };
    assert(splice_repo_add(repo, paths, 1) == 0);

    int ret = splice_repo_commit(repo, "Initial commit", "Test Author");
    assert(ret == 0);

    /* Index should be cleared */
    splice_index index;
    splice_index_read(splice_repo_path(repo), &index);
    assert(index.count == 0);
    splice_index_free(&index);

    /* HEAD should point to a valid commit */
    char ref_name[256];
    ret = splice_head_read(splice_repo_store(repo), ref_name, sizeof(ref_name));
    assert(ret == 0);

    splice_oid commit_oid;
    const char *ref = ref_name;
    if (strncmp(ref, "refs/", 5) == 0)
        ref += 5;
    ret = splice_ref_read(splice_repo_store(repo), ref, &commit_oid);
    assert(ret == 0);

    splice_commit commit;
    ret = splice_commit_read(splice_repo_store(repo), &commit_oid, &commit);
    assert(ret == 0);
    assert(strcmp(commit.message, "Initial commit") == 0);
    assert(strcmp(commit.author, "Test Author") == 0);
    splice_commit_free(&commit);

    /* Commit with empty index should fail */
    ret = splice_repo_commit(repo, "Empty commit", NULL);
    assert(ret == -1);

    chdir(orig_cwd);
    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_checkout(void)
{
    printf("  test_repo_checkout ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    make_file(TEST_DIR "/hello.txt", "Hello, splice!");

    char orig_cwd[4096];
    getcwd(orig_cwd, sizeof(orig_cwd));
    chdir(TEST_DIR);

    const char *paths[] = { "hello.txt" };
    assert(splice_repo_add(repo, paths, 1) == 0);
    assert(splice_repo_commit(repo, "Initial commit", NULL) == 0);

    /* Remove the file and checkout */
    unlink("hello.txt");
    assert(access("hello.txt", F_OK) != 0);

    int ret = splice_repo_checkout(repo, "main", 0);
    assert(ret == 0);
    assert(access("hello.txt", F_OK) == 0);

    /* Lazy checkout */
    unlink("hello.txt");
    ret = splice_repo_checkout(repo, "main", 1);
    assert(ret == 0);
    assert(access("hello.txt", F_OK) == 0);

    chdir(orig_cwd);
    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_log(void)
{
    printf("  test_repo_log ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    make_file(TEST_DIR "/hello.txt", "Hello, splice!");

    char orig_cwd[4096];
    getcwd(orig_cwd, sizeof(orig_cwd));
    chdir(TEST_DIR);

    const char *paths[] = { "hello.txt" };
    assert(splice_repo_add(repo, paths, 1) == 0);
    assert(splice_repo_commit(repo, "First", "A") == 0);

    make_file(TEST_DIR "/hello.txt", "Hello again!");
    assert(splice_repo_add(repo, paths, 1) == 0);
    assert(splice_repo_commit(repo, "Second", "B") == 0);

    int ret = splice_repo_log(repo, NULL);
    assert(ret == 0);

    chdir(orig_cwd);
    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_diff(void)
{
    printf("  test_repo_diff ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    make_file(TEST_DIR "/hello.txt", "Hello, splice!");

    char orig_cwd[4096];
    getcwd(orig_cwd, sizeof(orig_cwd));
    chdir(TEST_DIR);

    const char *paths[] = { "hello.txt" };
    assert(splice_repo_add(repo, paths, 1) == 0);
    assert(splice_repo_commit(repo, "Initial", "A") == 0);

    /* HEAD vs working directory */
    make_file(TEST_DIR "/hello.txt", "Modified!");
    int ret = splice_repo_diff(repo, NULL, NULL);
    assert(ret == 0);

    /* commit vs working directory */
    ret = splice_repo_diff(repo, "main", NULL);
    assert(ret == 0);

    chdir(orig_cwd);
    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_sparse(void)
{
    printf("  test_repo_sparse ... ");
    cleanup();
    mkdir(TEST_DIR, 0755);

    splice_repo *repo = splice_repo_init(TEST_DIR);
    assert(repo != NULL);

    splice_sparse_checkout sc;
    memset(&sc, 0, sizeof(sc));

    int ret = splice_repo_sparse_load(repo, &sc);
    assert(ret == 0);
    assert(sc.count == 0);

    assert(splice_sparse_add(&sc, "*.txt") == 0);
    assert(splice_sparse_add(&sc, "!secret.txt") == 0);

    ret = splice_repo_sparse_save(repo, &sc);
    assert(ret == 0);
    splice_sparse_free(&sc);

    /* Load back */
    ret = splice_repo_sparse_load(repo, &sc);
    assert(ret == 0);
    assert(sc.count == 2);
    assert(strcmp(sc.patterns[0], "*.txt") == 0);
    assert(strcmp(sc.patterns[1], "!secret.txt") == 0);

    splice_sparse_free(&sc);
    splice_repo_close(repo);
    cleanup();
    printf("OK\n");
}

static void test_repo_null_args(void)
{
    printf("  test_repo_null_args ... ");
    cleanup();

    assert(splice_repo_init(NULL) == NULL);
    assert(splice_repo_open(NULL) == NULL);
    assert(splice_repo_discover(NULL) == NULL);

    splice_repo_close(NULL);
    assert(splice_repo_store(NULL) == NULL);
    assert(splice_repo_path(NULL) == NULL);
    assert(splice_repo_workdir(NULL) == NULL);

    assert(splice_repo_add(NULL, NULL, 0) == -1);
    assert(splice_repo_commit(NULL, "msg", NULL) == -1);
    assert(splice_repo_checkout(NULL, "main", 0) == -1);
    assert(splice_repo_log(NULL, NULL) == -1);
    assert(splice_repo_diff(NULL, NULL, NULL) == -1);
    assert(splice_repo_sparse_load(NULL, NULL) == -1);
    assert(splice_repo_sparse_save(NULL, NULL) == -1);

    cleanup();
    printf("OK\n");
}

int main(void)
{
    printf("Running library API tests...\n");

    test_version();
    test_strerror();
    test_repo_init();
    test_repo_open();
    test_repo_discover();
    test_repo_add();
    test_repo_commit();
    test_repo_checkout();
    test_repo_log();
    test_repo_diff();
    test_repo_sparse();
    test_repo_null_args();

    printf("\nAll library API tests passed!\n");
    return 0;
}
