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

#endif
