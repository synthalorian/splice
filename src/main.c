#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

#define SPLICE_DIR ".splice"

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <command> [args]\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  init [path]       Initialize a new splice repository\n");
    fprintf(stderr, "  add <file>        Add a file to the staging area\n");
    fprintf(stderr, "  commit -m <msg>   Create a commit from staged files\n");
    fprintf(stderr, "  checkout [--lazy] <ref>  Checkout a commit to the working directory\n");
    fprintf(stderr, "  sparse-checkout (set|add|remove|list) [pattern]  Manage sparse-checkout patterns\n");
}

/* Walk up from start_dir looking for a .splice directory.
 * Returns malloc'd path to the .splice directory, or NULL if not found. */
static char *find_splice_dir(const char *start_dir)
{
    size_t start_len = strlen(start_dir);
    char *path = malloc(start_len + 1);
    if (!path) return NULL;
    strcpy(path, start_dir);

    while (1) {
        size_t len = strlen(path);
        size_t total = len + 1 + strlen(SPLICE_DIR) + 1;
        char *candidate = malloc(total);
        if (!candidate) {
            free(path);
            return NULL;
        }
        snprintf(candidate, total, "%s/%s", path, SPLICE_DIR);

        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
            free(path);
            return candidate;
        }

        free(candidate);

        /* Try parent directory */
        char *slash = strrchr(path, '/');
        if (!slash || slash == path) {
            free(path);
            return NULL;
        }
        *slash = '\0';
    }
}

static int cmd_init(int argc, char **argv)
{
    const char *path = (argc > 0) ? argv[0] : ".";

    size_t total = strlen(path) + 1 + strlen(SPLICE_DIR) + 1;
    char *splice_path = malloc(total);
    if (!splice_path) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }
    snprintf(splice_path, total, "%s/%s", path, SPLICE_DIR);

    struct stat st;
    if (stat(splice_path, &st) == 0) {
        fprintf(stderr, "error: repository already exists at %s\n", splice_path);
        free(splice_path);
        return 1;
    }

    splice_store *store = splice_store_open(splice_path);
    if (!store) {
        fprintf(stderr, "error: failed to create repository at %s\n", splice_path);
        free(splice_path);
        return 1;
    }
    splice_store_close(store);

    /* Create refs/heads directory */
    size_t heads_len = strlen(splice_path) + 1 + 5 + 1 + 5 + 1;
    char *heads_path = malloc(heads_len);
    if (heads_path) {
        snprintf(heads_path, heads_len, "%s/refs/heads", splice_path);
        mkdir(heads_path, 0755);
        free(heads_path);
    }

    /* Write HEAD pointing to refs/heads/main */
    store = splice_store_open(splice_path);
    if (store) {
        splice_head_write(store, "refs/heads/main");
        splice_store_close(store);
    }

    printf("Initialized empty splice repository in %s\n", splice_path);
    free(splice_path);
    return 0;
}

static int read_file(const char *path, void **out_data, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }

    rewind(fp);

    void *data = malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return -1;
    }

    if (size > 0 && fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_data = data;
    *out_len = (size_t)size;
    return 0;
}

static int cmd_add(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "error: no file specified\n");
        return 1;
    }

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "error: cannot get current directory\n");
        return 1;
    }

    char *splice_dir = find_splice_dir(cwd);
    if (!splice_dir) {
        fprintf(stderr, "error: not a splice repository\n");
        return 1;
    }

    splice_store *store = splice_store_open(splice_dir);
    if (!store) {
        fprintf(stderr, "error: failed to open repository\n");
        free(splice_dir);
        return 1;
    }

    splice_index index;
    if (splice_index_read(splice_dir, &index) != 0) {
        fprintf(stderr, "error: failed to read index\n");
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    int ret = 0;
    for (int i = 0; i < argc; i++) {
        const char *filepath = argv[i];

        void *data = NULL;
        size_t len = 0;
        if (read_file(filepath, &data, &len) != 0) {
            fprintf(stderr, "error: cannot read '%s': %s\n", filepath, strerror(errno));
            ret = 1;
            continue;
        }

        splice_oid oid;
        if (splice_object_write(store, data, len, &oid) != 0) {
            fprintf(stderr, "error: failed to store '%s'\n", filepath);
            free(data);
            ret = 1;
            continue;
        }
        free(data);

        struct stat st;
        uint32_t mode = 0100644;
        if (stat(filepath, &st) == 0) {
            if (st.st_mode & S_IXUSR) {
                mode = 0100755;
            }
        }

        if (splice_index_add(&index, filepath, &oid, mode) != 0) {
            fprintf(stderr, "error: failed to add '%s' to index\n", filepath);
            ret = 1;
            continue;
        }

        printf("added %s\n", filepath);
    }

    if (splice_index_write(splice_dir, &index) != 0) {
        fprintf(stderr, "error: failed to write index\n");
        ret = 1;
    }

    splice_index_free(&index);
    splice_store_close(store);
    free(splice_dir);
    return ret;
}

static char *get_author(void)
{
    const char *name = getenv("SPLICE_AUTHOR");
    if (name) return strdup(name);

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
        return strdup(pw->pw_name);
    }

    return strdup("unknown");
}

static int cmd_commit(int argc, char **argv)
{
    const char *message = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--message") == 0) {
            if (i + 1 < argc) {
                message = argv[i + 1];
                i++;
            }
        }
    }

    if (!message) {
        fprintf(stderr, "error: no commit message given (use -m)\n");
        return 1;
    }

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "error: cannot get current directory\n");
        return 1;
    }

    char *splice_dir = find_splice_dir(cwd);
    if (!splice_dir) {
        fprintf(stderr, "error: not a splice repository\n");
        return 1;
    }

    splice_store *store = splice_store_open(splice_dir);
    if (!store) {
        fprintf(stderr, "error: failed to open repository\n");
        free(splice_dir);
        return 1;
    }

    /* Read index */
    splice_index index;
    if (splice_index_read(splice_dir, &index) != 0) {
        fprintf(stderr, "error: failed to read index\n");
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    if (index.count == 0) {
        fprintf(stderr, "error: nothing to commit\n");
        splice_index_free(&index);
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    /* Build tree from index */
    splice_tree_entry *tree_entries = calloc(index.count, sizeof(*tree_entries));
    if (!tree_entries) {
        fprintf(stderr, "error: out of memory\n");
        splice_index_free(&index);
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    for (size_t i = 0; i < index.count; i++) {
        tree_entries[i].name = strdup(index.entries[i].path);
        tree_entries[i].oid = index.entries[i].oid;
        tree_entries[i].mode = index.entries[i].mode;
    }

    splice_oid tree_oid;
    if (splice_tree_build(store, tree_entries, index.count, &tree_oid) != 0) {
        fprintf(stderr, "error: failed to build tree\n");
        for (size_t i = 0; i < index.count; i++) free(tree_entries[i].name);
        free(tree_entries);
        splice_index_free(&index);
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    for (size_t i = 0; i < index.count; i++) free(tree_entries[i].name);
    free(tree_entries);

    /* Get current branch from HEAD */
    char ref_name[256];
    splice_oid parent_oid;
    int has_parent = 0;

    if (splice_head_read(store, ref_name, sizeof(ref_name)) == 0) {
        if (splice_ref_read(store, ref_name, &parent_oid) == 0) {
            has_parent = 1;
        }
    }

    /* Create commit */
    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree_oid;
    commit.author = get_author();
    commit.timestamp = (int64_t)time(NULL);
    commit.message = strdup(message);

    if (has_parent) {
        commit.parent_oids = malloc(sizeof(*commit.parent_oids));
        if (commit.parent_oids) {
            commit.parent_oids[0] = parent_oid;
            commit.parent_count = 1;
        }
    }

    splice_oid commit_oid;
    if (splice_commit_create(store, &commit, &commit_oid) != 0) {
        fprintf(stderr, "error: failed to create commit\n");
        free(commit.author);
        free(commit.message);
        free(commit.parent_oids);
        splice_index_free(&index);
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    free(commit.author);
    free(commit.message);
    free(commit.parent_oids);

    if (splice_head_read(store, ref_name, sizeof(ref_name)) == 0) {
        const char *ref_path = ref_name;
        if (strncmp(ref_path, "refs/", 5) == 0) {
            ref_path += 5;
        }
        if (splice_ref_write(store, ref_path, &commit_oid) != 0) {
            fprintf(stderr, "error: failed to update ref\n");
            splice_index_free(&index);
            splice_store_close(store);
            free(splice_dir);
            return 1;
        }
    }

    /* Clear index after successful commit */
    splice_index cleared;
    memset(&cleared, 0, sizeof(cleared));
    splice_index_write(splice_dir, &cleared);

    printf("[%s] %.16s %s\n",
           has_parent ? ref_name + strlen("refs/heads/") : "main",
           commit_oid.hex,
           message);

    splice_index_free(&index);
    splice_store_close(store);
    free(splice_dir);
    return 0;
}

static int cmd_checkout(int argc, char **argv)
{
    int lazy = 0;
    const char *ref = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--lazy") == 0) {
            lazy = 1;
        } else {
            ref = argv[i];
        }
    }

    if (!ref) {
        fprintf(stderr, "error: no ref specified\n");
        return 1;
    }

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "error: cannot get current directory\n");
        return 1;
    }

    char *splice_dir = find_splice_dir(cwd);
    if (!splice_dir) {
        fprintf(stderr, "error: not a splice repository\n");
        return 1;
    }

    splice_store *store = splice_store_open(splice_dir);
    if (!store) {
        fprintf(stderr, "error: failed to open repository\n");
        free(splice_dir);
        return 1;
    }

    /* Resolve ref to commit OID */
    splice_oid commit_oid;
    char ref_path[256];
    snprintf(ref_path, sizeof(ref_path), "refs/heads/%s", ref);

    if (splice_ref_read(store, ref_path, &commit_oid) != 0) {
        /* Try as full ref name */
        if (splice_ref_read(store, ref, &commit_oid) != 0) {
            fprintf(stderr, "error: ref not found: %s\n", ref);
            splice_store_close(store);
            free(splice_dir);
            return 1;
        }
    }

    /* Read commit to get tree */
    splice_commit commit;
    if (splice_commit_read(store, &commit_oid, &commit) != 0) {
        fprintf(stderr, "error: failed to read commit\n");
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    /* Checkout */
    int ret;
    if (lazy) {
        ret = splice_checkout_lazy(store, &commit.tree_oid, cwd);
    } else {
        ret = splice_checkout(store, &commit.tree_oid, cwd);
    }

    if (ret != 0) {
        fprintf(stderr, "error: checkout failed\n");
        splice_commit_free(&commit);
        splice_store_close(store);
        free(splice_dir);
        return 1;
    }

    printf("Checked out %.16s (%s)\n", commit_oid.hex, lazy ? "lazy" : "full");

    splice_commit_free(&commit);
    splice_store_close(store);
    free(splice_dir);
    return 0;
}

static int cmd_sparse_checkout(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "error: no sparse-checkout subcommand given\n");
        fprintf(stderr, "Usage: splice sparse-checkout (set|add|remove|list) [pattern]\n");
        return 1;
    }

    const char *subcmd = argv[0];

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "error: cannot get current directory\n");
        return 1;
    }

    char *splice_dir = find_splice_dir(cwd);
    if (!splice_dir) {
        fprintf(stderr, "error: not a splice repository\n");
        return 1;
    }

    splice_sparse_checkout sc;
    if (splice_sparse_load(splice_dir, &sc) != 0) {
        fprintf(stderr, "error: failed to load sparse-checkout patterns\n");
        free(splice_dir);
        return 1;
    }

    int ret = 0;

    if (strcmp(subcmd, "set") == 0) {
        if (argc < 2) {
            fprintf(stderr, "error: no pattern given\n");
            splice_sparse_free(&sc);
            free(splice_dir);
            return 1;
        }
        splice_sparse_free(&sc);
        memset(&sc, 0, sizeof(sc));
        for (int i = 1; i < argc; i++) {
            if (splice_sparse_add(&sc, argv[i]) != 0) {
                fprintf(stderr, "error: failed to add pattern\n");
                ret = 1;
                break;
            }
        }
        if (ret == 0) {
            if (splice_sparse_save(splice_dir, &sc) != 0) {
                fprintf(stderr, "error: failed to save sparse-checkout patterns\n");
                ret = 1;
            } else {
                printf("Sparse-checkout patterns set.\n");
            }
        }
    } else if (strcmp(subcmd, "add") == 0) {
        if (argc < 2) {
            fprintf(stderr, "error: no pattern given\n");
            splice_sparse_free(&sc);
            free(splice_dir);
            return 1;
        }
        for (int i = 1; i < argc; i++) {
            if (splice_sparse_add(&sc, argv[i]) != 0) {
                fprintf(stderr, "error: failed to add pattern\n");
                ret = 1;
                break;
            }
        }
        if (ret == 0) {
            if (splice_sparse_save(splice_dir, &sc) != 0) {
                fprintf(stderr, "error: failed to save sparse-checkout patterns\n");
                ret = 1;
            } else {
                printf("Sparse-checkout pattern added.\n");
            }
        }
    } else if (strcmp(subcmd, "remove") == 0) {
        if (argc < 2) {
            fprintf(stderr, "error: no pattern index given\n");
            splice_sparse_free(&sc);
            free(splice_dir);
            return 1;
        }
        int idx = atoi(argv[1]);
        if (idx < 0 || (size_t)idx >= sc.count) {
            fprintf(stderr, "error: invalid pattern index\n");
            ret = 1;
        } else {
            if (splice_sparse_remove(&sc, (size_t)idx) != 0) {
                fprintf(stderr, "error: failed to remove pattern\n");
                ret = 1;
            } else {
                if (splice_sparse_save(splice_dir, &sc) != 0) {
                    fprintf(stderr, "error: failed to save sparse-checkout patterns\n");
                    ret = 1;
                } else {
                    printf("Sparse-checkout pattern removed.\n");
                }
            }
        }
    } else if (strcmp(subcmd, "list") == 0) {
        if (sc.count == 0) {
            printf("(no sparse-checkout patterns defined)\n");
        } else {
            for (size_t i = 0; i < sc.count; i++) {
                printf("[%zu] %s\n", i, sc.patterns[i]);
            }
        }
    } else {
        fprintf(stderr, "error: unknown sparse-checkout subcommand '%s'\n", subcmd);
        fprintf(stderr, "Usage: splice sparse-checkout (set|add|remove|list) [pattern]\n");
        ret = 1;
    }

    splice_sparse_free(&sc);
    free(splice_dir);
    return ret;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    int cmd_argc = argc - 2;
    char **cmd_argv = argv + 2;

    if (strcmp(cmd, "init") == 0) {
        return cmd_init(cmd_argc, cmd_argv);
    } else if (strcmp(cmd, "add") == 0) {
        return cmd_add(cmd_argc, cmd_argv);
    } else if (strcmp(cmd, "commit") == 0) {
        return cmd_commit(cmd_argc, cmd_argv);
    } else if (strcmp(cmd, "checkout") == 0) {
        return cmd_checkout(cmd_argc, cmd_argv);
    } else if (strcmp(cmd, "sparse-checkout") == 0) {
        return cmd_sparse_checkout(cmd_argc, cmd_argv);
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
