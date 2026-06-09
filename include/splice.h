#ifndef SPLICE_H
#define SPLICE_H

#include <stdint.h>
#include <stddef.h>

/* splice -- content-addressable storage for binary assets */

#define SPLICE_OID_HEXSZ 17  /* 16 hex chars + null terminator */

/* Forward declarations */
typedef struct splice_store splice_store;

/* Object types */
typedef enum {
    SPLICE_OBJ_BLOB = 1,
    SPLICE_OBJ_DELTA = 2,
    SPLICE_OBJ_TREE = 3,
    SPLICE_OBJ_COMMIT = 4,
} splice_object_type;

/* Object ID -- content address (xxhash64) */
typedef struct {
    uint64_t hash;
    char hex[SPLICE_OID_HEXSZ];
} splice_oid;

/* Store lifecycle --------------------------------------------------------- */

/* Open or create an object store at the given path.
 * Returns NULL on error. */
splice_store *splice_store_open(const char *path);

/* Close a store and free associated resources. */
void splice_store_close(splice_store *store);

/* Object operations ------------------------------------------------------- */

/* Write a blob to the store. Returns 0 on success, -1 on error.
 * On success, out_oid is populated with the content address. */
int splice_object_write(splice_store *store,
                        const void *data,
                        size_t len,
                        splice_oid *out_oid);

/* Write an object with a specific type to the store.
 * Returns 0 on success, -1 on error. */
int splice_object_write_typed(splice_store *store,
                              splice_object_type type,
                              const void *data,
                              size_t len,
                              splice_oid *out_oid);

/* Read a blob from the store by its OID.
 * Returns 0 on success, -1 on error.
 * On success, out_data is malloc'd and must be freed by the caller,
 * and out_len is set to the size of the data. */
int splice_object_read(splice_store *store,
                       const splice_oid *oid,
                       void **out_data,
                       size_t *out_len);

/* Check whether an object exists in the store.
 * Returns 1 if it exists, 0 if not, -1 on error. */
int splice_object_exists(splice_store *store, const splice_oid *oid);

/* OID utilities ----------------------------------------------------------- */

/* Convert an OID to its hex string representation.
 * out_hex must be at least SPLICE_OID_HEXSZ bytes. */
void splice_oid_to_hex(const splice_oid *oid, char *out_hex);

/* Parse a hex string into an OID.
 * Returns 0 on success, -1 on error. */
int splice_oid_from_hex(const char *hex, splice_oid *out_oid);

/* Compare two OIDs. Returns 0 if equal, non-zero otherwise. */
int splice_oid_cmp(const splice_oid *a, const splice_oid *b);

/* Delta compression ------------------------------------------------------- */

/* Create a delta object representing the difference between a base object
 * and new data. The delta is compressed using zstd with the base object as
 * a dictionary for maximum compression of similar content.
 * Returns 0 on success, -1 on error.
 * On success, out_delta_oid is populated with the content address. */
int splice_delta_create(splice_store *store,
                        const splice_oid *base_oid,
                        const void *new_data,
                        size_t new_len,
                        splice_oid *out_delta_oid);

/* Apply a delta object to reconstruct the original data.
 * Returns 0 on success, -1 on error.
 * On success, out_data is malloc'd and must be freed by the caller,
 * and out_len is set to the size of the reconstructed data. */
int splice_delta_apply(splice_store *store,
                        const splice_oid *delta_oid,
                        void **out_data,
                        size_t *out_len);

/* Tree objects ------------------------------------------------------------ */

/* A single entry in a tree: mode + name + OID */
typedef struct {
    uint32_t mode;      /* e.g. 0100644 for regular file */
    char *name;         /* filename, heap-allocated */
    splice_oid oid;     /* object this entry points to */
} splice_tree_entry;

/* Build a tree object from an array of entries.
 * Entries are sorted by name before serialization.
 * Returns 0 on success, -1 on error. */
int splice_tree_build(splice_store *store,
                      splice_tree_entry *entries,
                      size_t count,
                      splice_oid *out_tree_oid);

/* Parse a tree object into an array of entries.
 * Returns 0 on success, -1 on error.
 * On success, out_entries is malloc'd and must be freed with
 * splice_tree_entries_free(). */
int splice_tree_parse(splice_store *store,
                      const splice_oid *tree_oid,
                      splice_tree_entry **out_entries,
                      size_t *out_count);

/* Free memory allocated by splice_tree_parse(). */
void splice_tree_entries_free(splice_tree_entry *entries, size_t count);

/* Commit objects ---------------------------------------------------------- */

/* In-memory representation of a commit */
typedef struct {
    splice_oid tree_oid;        /* tree object for this commit */
    splice_oid *parent_oids;    /* array of parent commit OIDs */
    size_t parent_count;        /* number of parents (0 for root) */
    char *author;               /* author string */
    int64_t timestamp;          /* Unix timestamp */
    char *message;              /* commit message */
} splice_commit;

/* Create a commit object in the store.
 * Returns 0 on success, -1 on error. */
int splice_commit_create(splice_store *store,
                         const splice_commit *commit,
                         splice_oid *out_commit_oid);

/* Read a commit object from the store.
 * Returns 0 on success, -1 on error.
 * On success, out_commit is populated; caller must call splice_commit_free(). */
int splice_commit_read(splice_store *store,
                       const splice_oid *commit_oid,
                       splice_commit *out_commit);

/* Free memory allocated by splice_commit_read(). */
void splice_commit_free(splice_commit *commit);

/* Refs and HEAD ----------------------------------------------------------- */

/* Write a ref (branch or tag) pointing to an OID.
 * name is the ref path under refs/ (e.g. "heads/main").
 * Returns 0 on success, -1 on error. */
int splice_ref_write(splice_store *store, const char *name, const splice_oid *oid);

/* Read a ref to get the OID it points to.
 * Returns 0 on success, -1 on error. */
int splice_ref_read(splice_store *store, const char *name, splice_oid *out_oid);

/* Check whether a ref exists.
 * Returns 1 if it exists, 0 if not, -1 on error. */
int splice_ref_exists(splice_store *store, const char *name);

/* Delete a ref.
 * Returns 0 on success, -1 on error. */
int splice_ref_delete(splice_store *store, const char *name);

/* Write HEAD as a symbolic ref pointing to a ref name.
 * ref_name is the full ref name (e.g. "refs/heads/main").
 * Returns 0 on success, -1 on error. */
int splice_head_write(splice_store *store, const char *ref_name);

/* Read HEAD to get the ref name it points to.
 * out_ref_name must be at least max_len bytes.
 * Returns 0 on success, -1 on error. */
int splice_head_read(splice_store *store, char *out_ref_name, size_t max_len);

#endif
