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

/* ------------------------------------------------------------------------- */
/* Version                                                                   */
/* ------------------------------------------------------------------------- */

#define SPLICE_VERSION_STRING "1.0.0"

const char *splice_version_string(void)
{
    return SPLICE_VERSION_STRING;
}

void splice_version(int *major, int *minor, int *patch)
{
    if (major) *major = SPLICE_VERSION_MAJOR;
    if (minor) *minor = SPLICE_VERSION_MINOR;
    if (patch) *patch = SPLICE_VERSION_PATCH;
}

/* ------------------------------------------------------------------------- */
/* Error strings                                                             */
/* ------------------------------------------------------------------------- */

const char *splice_strerror(splice_error err)
{
    switch (err) {
        case SPLICE_OK:              return "success";
        case SPLICE_ERROR_NOMEM:     return "out of memory";
        case SPLICE_ERROR_NOTFOUND:  return "not found";
        case SPLICE_ERROR_EXISTS:    return "already exists";
        case SPLICE_ERROR_INVALID:   return "invalid argument";
        case SPLICE_ERROR_IO:        return "I/O error";
        case SPLICE_ERROR_CORRUPTED: return "data corrupted";
        case SPLICE_ERROR_UNKNOWN:   return "unknown error";
        default:                     return "unknown error";
    }
}

/* ------------------------------------------------------------------------- */
/* Repository handle                                                         */
/* ------------------------------------------------------------------------- */

struct splice_repo {
    splice_store *store;
    char *path;      /* path to .splice directory */
    char *workdir;   /* path to working directory */
};

/* Internal: walk up from start_dir looking for a .splice directory.
 * Returns malloc'd path to the .splice directory, or NULL if not found. */
static char *discover_splice_dir(const char *start_dir)
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

        char *slash = strrchr(path, '/');
        if (!slash || slash == path) {
            free(path);
            return NULL;
        }
        *slash = '\0';
    }
}

/* Internal: compute working directory from .splice path.
 * Returns malloc'd string or NULL. */
static char *workdir_from_splice_path(const char *splice_path)
{
    size_t len = strlen(splice_path);
    size_t suffix_len = strlen(SPLICE_DIR) + 1; /* +1 for '/' */
    if (len > suffix_len) {
        char *workdir = malloc(len - suffix_len + 1);
        if (!workdir) return NULL;
        memcpy(workdir, splice_path, len - suffix_len);
        workdir[len - suffix_len] = '\0';
        return workdir;
    }
    return NULL;
}

splice_repo *splice_repo_init(const char *path)
{
    if (!path) return NULL;

    /* Ensure parent directory exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            return NULL;
        }
    }

    size_t total = strlen(path) + 1 + strlen(SPLICE_DIR) + 1;
    char *splice_path = malloc(total);
    if (!splice_path) return NULL;
    snprintf(splice_path, total, "%s/%s", path, SPLICE_DIR);

    if (stat(splice_path, &st) == 0) {
        free(splice_path);
        return NULL; /* already exists */
    }

    splice_store *store = splice_store_open(splice_path);
    if (!store) {
        free(splice_path);
        return NULL;
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

    splice_repo *repo = calloc(1, sizeof(*repo));
    if (!repo) {
        free(splice_path);
        return NULL;
    }

    repo->path = splice_path;
    repo->workdir = workdir_from_splice_path(splice_path);
    repo->store = splice_store_open(splice_path);
    if (!repo->store) {
        free(repo->workdir);
        free(repo->path);
        free(repo);
        return NULL;
    }

    return repo;
}

splice_repo *splice_repo_open(const char *path)
{
    if (!path) return NULL;

    size_t total = strlen(path) + 1 + strlen(SPLICE_DIR) + 1;
    char *splice_path = malloc(total);
    if (!splice_path) return NULL;
    snprintf(splice_path, total, "%s/%s", path, SPLICE_DIR);

    struct stat st;
    if (stat(splice_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        free(splice_path);
        return NULL;
    }

    splice_repo *repo = calloc(1, sizeof(*repo));
    if (!repo) {
        free(splice_path);
        return NULL;
    }

    repo->path = splice_path;
    repo->workdir = workdir_from_splice_path(splice_path);
    repo->store = splice_store_open(splice_path);
    if (!repo->store) {
        free(repo->workdir);
        free(repo->path);
        free(repo);
        return NULL;
    }

    return repo;
}

splice_repo *splice_repo_discover(const char *start_dir)
{
    if (!start_dir) return NULL;

    char *splice_path = discover_splice_dir(start_dir);
    if (!splice_path) return NULL;

    splice_repo *repo = calloc(1, sizeof(*repo));
    if (!repo) {
        free(splice_path);
        return NULL;
    }

    repo->path = splice_path;
    repo->workdir = workdir_from_splice_path(splice_path);
    repo->store = splice_store_open(splice_path);
    if (!repo->store) {
        free(repo->workdir);
        free(repo->path);
        free(repo);
        return NULL;
    }

    return repo;
}

void splice_repo_close(splice_repo *repo)
{
    if (!repo) return;
    if (repo->store) splice_store_close(repo->store);
    free(repo->path);
    free(repo->workdir);
    free(repo);
}

splice_store *splice_repo_store(splice_repo *repo)
{
    return repo ? repo->store : NULL;
}

const char *splice_repo_path(splice_repo *repo)
{
    return repo ? repo->path : NULL;
}

const char *splice_repo_workdir(splice_repo *repo)
{
    return repo ? repo->workdir : NULL;
}

/* ------------------------------------------------------------------------- */
/* High-level operations                                                     */
/* ------------------------------------------------------------------------- */

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

int splice_repo_add(splice_repo *repo, const char **paths, size_t count)
{
    if (!repo || !paths || count == 0) return -1;

    splice_index index;
    if (splice_index_read(repo->path, &index) != 0) {
        return -1;
    }

    int ret = 0;
    for (size_t i = 0; i < count; i++) {
        const char *filepath = paths[i];

        void *data = NULL;
        size_t len = 0;
        if (read_file(filepath, &data, &len) != 0) {
            ret = -1;
            continue;
        }

        splice_oid oid;
        if (splice_object_write(repo->store, data, len, &oid) != 0) {
            free(data);
            ret = -1;
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
            ret = -1;
            continue;
        }
    }

    if (splice_index_write(repo->path, &index) != 0) {
        ret = -1;
    }

    splice_index_free(&index);
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

int splice_repo_commit(splice_repo *repo, const char *message, const char *author)
{
    if (!repo || !message) return -1;

    splice_index index;
    if (splice_index_read(repo->path, &index) != 0) {
        return -1;
    }

    if (index.count == 0) {
        splice_index_free(&index);
        return -1;
    }

    splice_tree_entry *tree_entries = calloc(index.count, sizeof(*tree_entries));
    if (!tree_entries) {
        splice_index_free(&index);
        return -1;
    }

    for (size_t i = 0; i < index.count; i++) {
        tree_entries[i].name = strdup(index.entries[i].path);
        tree_entries[i].oid = index.entries[i].oid;
        tree_entries[i].mode = index.entries[i].mode;
    }

    splice_oid tree_oid;
    if (splice_tree_build(repo->store, tree_entries, index.count, &tree_oid) != 0) {
        for (size_t i = 0; i < index.count; i++) free(tree_entries[i].name);
        free(tree_entries);
        splice_index_free(&index);
        return -1;
    }

    for (size_t i = 0; i < index.count; i++) free(tree_entries[i].name);
    free(tree_entries);

    /* Get current branch from HEAD */
    char ref_name[256];
    splice_oid parent_oid;
    int has_parent = 0;

    if (splice_head_read(repo->store, ref_name, sizeof(ref_name)) == 0) {
        const char *ref = ref_name;
        if (strncmp(ref, "refs/", 5) == 0)
            ref += 5;
        if (splice_ref_read(repo->store, ref, &parent_oid) == 0) {
            has_parent = 1;
        }
    }

    splice_commit commit;
    memset(&commit, 0, sizeof(commit));
    commit.tree_oid = tree_oid;
    commit.author = author ? strdup(author) : get_author();
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
    int ret = splice_commit_create(repo->store, &commit, &commit_oid);

    free(commit.author);
    free(commit.message);
    free(commit.parent_oids);

    if (ret != 0) {
        splice_index_free(&index);
        return -1;
    }

    if (splice_head_read(repo->store, ref_name, sizeof(ref_name)) == 0) {
        const char *ref_path = ref_name;
        if (strncmp(ref_path, "refs/", 5) == 0) {
            ref_path += 5;
        }
        splice_ref_write(repo->store, ref_path, &commit_oid);
    }

    /* Clear index after successful commit */
    splice_index cleared;
    memset(&cleared, 0, sizeof(cleared));
    splice_index_write(repo->path, &cleared);

    splice_index_free(&index);
    return 0;
}

int splice_repo_checkout(splice_repo *repo, const char *ref, int lazy)
{
    if (!repo || !ref) return -1;

    splice_oid commit_oid;
    char ref_path[256];

    if (strncmp(ref, "refs/", 5) == 0)
        snprintf(ref_path, sizeof(ref_path), "%s", ref + 5);
    else
        snprintf(ref_path, sizeof(ref_path), "heads/%s", ref);

    if (splice_ref_read(repo->store, ref_path, &commit_oid) != 0) {
        return -1;
    }

    splice_commit commit;
    if (splice_commit_read(repo->store, &commit_oid, &commit) != 0) {
        return -1;
    }

    int ret;
    if (lazy) {
        ret = splice_checkout_lazy(repo->store, &commit.tree_oid, repo->workdir);
    } else {
        ret = splice_checkout(repo->store, &commit.tree_oid, repo->workdir);
    }

    splice_commit_free(&commit);
    return ret;
}

int splice_repo_log(splice_repo *repo, const char *ref)
{
    if (!repo) return -1;

    const char *r = ref ? ref : "heads/main";
    return splice_log(repo->store, r);
}

int splice_repo_diff(splice_repo *repo,
                     const char *old_ref,
                     const char *new_ref)
{
    if (!repo) return -1;

    if (!old_ref && !new_ref) {
        /* HEAD vs working directory */
        char ref_name[256];
        splice_oid head_oid;
        const char *ref = "heads/main";

        if (splice_head_read(repo->store, ref_name, sizeof(ref_name)) == 0) {
            ref = ref_name;
            if (strncmp(ref, "refs/", 5) == 0)
                ref += 5;
        }

        if (splice_ref_read(repo->store, ref, &head_oid) != 0) {
            return -1;
        }

        splice_commit head_commit;
        if (splice_commit_read(repo->store, &head_oid, &head_commit) != 0) {
            return -1;
        }

        int ret = splice_diff_tree_workdir(repo->store, &head_commit.tree_oid, repo->workdir);
        splice_commit_free(&head_commit);
        return ret;
    } else if (old_ref && !new_ref) {
        /* commit vs working directory */
        splice_oid commit_oid;
        if (splice_oid_from_hex(old_ref, &commit_oid) == 0 &&
            splice_object_exists(repo->store, &commit_oid) == 1) {
            /* ok */
        } else {
            char branch_ref[256];
            snprintf(branch_ref, sizeof(branch_ref), "heads/%s", old_ref);
            if (splice_ref_read(repo->store, branch_ref, &commit_oid) != 0) {
                if (splice_ref_read(repo->store, old_ref, &commit_oid) != 0) {
                    return -1;
                }
            }
        }

        splice_commit commit;
        if (splice_commit_read(repo->store, &commit_oid, &commit) != 0) {
            return -1;
        }

        int ret = splice_diff_tree_workdir(repo->store, &commit.tree_oid, repo->workdir);
        splice_commit_free(&commit);
        return ret;
    } else if (old_ref && new_ref) {
        /* commit vs commit */
        splice_oid old_oid, new_oid;

        if (splice_oid_from_hex(old_ref, &old_oid) == 0 &&
            splice_object_exists(repo->store, &old_oid) == 1) {
            /* ok */
        } else {
            char branch_ref[256];
            snprintf(branch_ref, sizeof(branch_ref), "heads/%s", old_ref);
            if (splice_ref_read(repo->store, branch_ref, &old_oid) != 0) {
                if (splice_ref_read(repo->store, old_ref, &old_oid) != 0) {
                    return -1;
                }
            }
        }

        if (splice_oid_from_hex(new_ref, &new_oid) == 0 &&
            splice_object_exists(repo->store, &new_oid) == 1) {
            /* ok */
        } else {
            char branch_ref[256];
            snprintf(branch_ref, sizeof(branch_ref), "heads/%s", new_ref);
            if (splice_ref_read(repo->store, branch_ref, &new_oid) != 0) {
                if (splice_ref_read(repo->store, new_ref, &new_oid) != 0) {
                    return -1;
                }
            }
        }

        return splice_diff_commits(repo->store, &old_oid, &new_oid);
    }

    return -1;
}

int splice_repo_sparse_load(splice_repo *repo, splice_sparse_checkout *out_sc)
{
    if (!repo || !out_sc) return -1;
    return splice_sparse_load(repo->path, out_sc);
}

int splice_repo_sparse_save(splice_repo *repo, const splice_sparse_checkout *sc)
{
    if (!repo || !sc) return -1;
    return splice_sparse_save(repo->path, sc);
}
