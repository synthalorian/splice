#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Commit serialization format (text, null-terminated):
 *   tree <16-char hex OID>\n
 *   parent <16-char hex OID>\n   (0 or more)
 *   author <name>\n
 *   time <timestamp>\n
 *   \n
 *   <message>
 */

int splice_commit_create(splice_store *store,
                         const splice_commit *commit,
                         splice_oid *out_commit_oid)
{
    if (!store || !commit || !out_commit_oid)
        return -1;

    /* Compute serialized size */
    size_t msg_len = commit->message ? strlen(commit->message) : 0;
    size_t author_len = commit->author ? strlen(commit->author) : 0;

    size_t size = 6 + 16 + 1;                     /* "tree " + oid + "\n" */
    size += (7 + 16 + 1) * commit->parent_count;  /* "parent " + oid + "\n" */
    size += 7 + author_len + 1;                   /* "author " + name + "\n" */
    size += 6 + 20 + 1;                           /* "time " + timestamp + "\n" */
    size += 1;                                    /* "\n" separator */
    size += msg_len;                              /* message */

    char *buf = malloc(size + 1);
    if (!buf)
        return -1;

    char *p = buf;
    p += sprintf(p, "tree %.16s\n", commit->tree_oid.hex);

    for (size_t i = 0; i < commit->parent_count; i++) {
        p += sprintf(p, "parent %.16s\n", commit->parent_oids[i].hex);
    }

    p += sprintf(p, "author %s\n", commit->author ? commit->author : "");
    p += sprintf(p, "time %lld\n", (long long)commit->timestamp);
    p += sprintf(p, "\n");

    if (msg_len > 0) {
        memcpy(p, commit->message, msg_len);
        p += msg_len;
    }

    size_t actual_size = (size_t)(p - buf);
    int ret = splice_object_write_typed(store, SPLICE_OBJ_COMMIT,
                                        buf, actual_size, out_commit_oid);
    free(buf);
    return ret;
}

int splice_commit_read(splice_store *store,
                       const splice_oid *commit_oid,
                       splice_commit *out_commit)
{
    if (!store || !commit_oid || !out_commit)
        return -1;

    void *data = NULL;
    size_t data_len = 0;
    if (splice_object_read(store, commit_oid, &data, &data_len) != 0)
        return -1;

    /* Ensure null-terminated for string parsing */
    char *text = realloc(data, data_len + 1);
    if (!text) {
        free(data);
        return -1;
    }
    text[data_len] = '\0';

    memset(out_commit, 0, sizeof(*out_commit));

    /* Parse line by line */
    char *line = text;
    char *msg_start = NULL;

    while (*line) {
        char *end = strchr(line, '\n');
        if (!end)
            break;

        *end = '\0';

        if (line == text && strncmp(line, "tree ", 5) == 0) {
            if (splice_oid_from_hex(line + 5, &out_commit->tree_oid) != 0) {
                splice_commit_free(out_commit);
                free(text);
                return -1;
            }
        }
        else if (strncmp(line, "parent ", 7) == 0) {
            splice_oid *new_parents = realloc(out_commit->parent_oids,
                (out_commit->parent_count + 1) * sizeof(*new_parents));
            if (!new_parents) {
                splice_commit_free(out_commit);
                free(text);
                return -1;
            }
            out_commit->parent_oids = new_parents;
            if (splice_oid_from_hex(line + 7, &out_commit->parent_oids[out_commit->parent_count]) != 0) {
                splice_commit_free(out_commit);
                free(text);
                return -1;
            }
            out_commit->parent_count++;
        }
        else if (strncmp(line, "author ", 7) == 0) {
            out_commit->author = strdup(line + 7);
            if (!out_commit->author) {
                splice_commit_free(out_commit);
                free(text);
                return -1;
            }
        }
        else if (strncmp(line, "time ", 5) == 0) {
            out_commit->timestamp = (int64_t)atoll(line + 5);
        }
        else if (*line == '\0') {
            /* Empty line separates headers from message */
            msg_start = end + 1;
            break;
        }

        line = end + 1;
    }

    if (msg_start) {
        size_t msg_len = strlen(msg_start);
        out_commit->message = malloc(msg_len + 1);
        if (!out_commit->message) {
            splice_commit_free(out_commit);
            free(text);
            return -1;
        }
        memcpy(out_commit->message, msg_start, msg_len + 1);
    }

    free(text);
    return 0;
}

void splice_commit_free(splice_commit *commit)
{
    if (!commit)
        return;
    free(commit->parent_oids);
    free(commit->author);
    free(commit->message);
    memset(commit, 0, sizeof(*commit));
}
