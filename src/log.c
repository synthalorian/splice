#define _POSIX_C_SOURCE 200809L

#include "splice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Format a Unix timestamp as a human-readable string.
 * out_buf must be at least 64 bytes. */
static void format_timestamp(int64_t ts, char *out_buf, size_t out_len)
{
    time_t t = (time_t)ts;
    struct tm *tm_info = localtime(&t);
    if (tm_info) {
        strftime(out_buf, out_len, "%a %b %d %H:%M:%S %Y", tm_info);
    } else {
        snprintf(out_buf, out_len, "%lld", (long long)ts);
    }
}

/* Print a single commit in log format. */
static void print_commit(const splice_oid *oid, const splice_commit *commit)
{
    char time_buf[64];
    format_timestamp(commit->timestamp, time_buf, sizeof(time_buf));

    printf("commit %.16s\n", oid->hex);
    printf("Author: %s\n", commit->author ? commit->author : "");
    printf("Date:   %s\n\n", time_buf);

    if (commit->message && commit->message[0]) {
        /* Indent each line of the message */
        const char *p = commit->message;
        while (*p) {
            const char *end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            printf("    %.*s\n", (int)(end - p), p);
            if (*end == '\0') break;
            p = end + 1;
        }
        printf("\n");
    } else {
        printf("\n");
    }
}

int splice_log(splice_store *store, const char *ref_name)
{
    if (!store || !ref_name)
        return -1;

    splice_oid commit_oid;
    if (splice_ref_read(store, ref_name, &commit_oid) != 0)
        return -1;

    /* Walk linear history via first parent to avoid infinite loops,
     * cap iterations as a safety guard. */
    splice_oid current = commit_oid;
    size_t max_commits = 10000;

    for (size_t i = 0; i < max_commits; i++) {
        splice_commit commit;
        memset(&commit, 0, sizeof(commit));
        if (splice_commit_read(store, &current, &commit) != 0)
            return -1;

        print_commit(&current, &commit);

        if (commit.parent_count == 0) {
            splice_commit_free(&commit);
            break;
        }

        splice_oid next = commit.parent_oids[0];
        splice_commit_free(&commit);
        current = next;
    }

    return 0;
}

int splice_log_oid(splice_store *store, const splice_oid *commit_oid)
{
    if (!store || !commit_oid)
        return -1;

    splice_oid current = *commit_oid;
    size_t max_commits = 10000;

    for (size_t i = 0; i < max_commits; i++) {
        splice_commit commit;
        memset(&commit, 0, sizeof(commit));
        if (splice_commit_read(store, &current, &commit) != 0)
            return -1;

        print_commit(&current, &commit);

        if (commit.parent_count == 0) {
            splice_commit_free(&commit);
            break;
        }

        splice_oid next = commit.parent_oids[0];
        splice_commit_free(&commit);
        current = next;
    }

    return 0;
}
