#ifndef HYDRA_TUI_STORE_H
#define HYDRA_TUI_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/* The local head snapshot read straight from Hydra's state store: the rows
 * `hydra tui --data` prints, without starting the shell CLI. This file knows
 * only the store's files; what tmux and git alone can say (live sessions,
 * uncommitted files) is handed in by the caller. The two readers are kept
 * identical by tests/test_tui_store_parity.sh. */

#define STORE_TEXT 1024
#define STORE_PATH 4096

/* Where the records are. An empty string is "not known" and reads as it does
 * in the shell reader, where the same variable would be empty. */
struct store_location {
    char home[STORE_PATH];       /* HYDRA_HOME, physical */
    char state_root[STORE_PATH]; /* HYDRA_STATE_V2_ROOT, or home/state/v2 */
    char common[STORE_PATH];     /* the repository's git common directory, physical */
    char toplevel[STORE_PATH];   /* the working tree's top level */
    char host[256];              /* this machine's name, as lock owners record it */
};

/* One head row before live sessions and uncommitted files are known. */
struct store_head {
    char branch[STORE_TEXT], session[STORE_TEXT], profile[STORE_TEXT], group[STORE_TEXT], pr[STORE_TEXT];
    /* The session name as the live-session lookup reads it. */
    char live_key[STORE_TEXT];
    char declared[STORE_TEXT], observed[STORE_TEXT], confidence[STORE_TEXT], instance[STORE_TEXT];
    char desired[STORE_TEXT], head_id[STORE_TEXT], mode[16];
    char adapter[STORE_TEXT], adapter_confidence[STORE_TEXT];
    char head_dir[STORE_PATH], source[STORE_PATH], adapter_source[STORE_PATH];
    /* The head's worktree when it is a directory, otherwise empty. */
    char worktree[STORE_PATH];
    unsigned events, signals, messages, claims, scopes, queue, resources, gates, approved;
    /* The record's fields did not split into the expected columns. */
    bool extra;
};

struct store_snapshot {
    struct store_head *heads;
    size_t head_count;
    char state_source[STORE_PATH], notification_source[STORE_PATH];
    unsigned notifications;
    /* Recovery rows that depend on the store alone, ready to print. */
    char *findings;
    size_t findings_length;
    /* Some listed head has a terminal, so live sessions decide its status. */
    bool interactive;
    /* A directory under the worktree root has no head record: the shell
     * reader decides whether it is a leftover worktree. */
    bool leftovers;
};

/* Line counts of append-only records, kept while a file is unchanged. */
struct store_cache_entry {
    char *path;
    dev_t device;
    ino_t inode;
    off_t size;
    time_t seconds;
    long nanoseconds;
    unsigned lines, matching;
};
struct store_cache {
    struct store_cache_entry *entries;
    size_t count, capacity;
};

/* Reads the store. cache may be NULL. False only when memory ran out; a
 * missing or unreadable store reads as an empty one, as it does in the shell.
 * The caller frees the snapshot with store_snapshot_free. */
bool store_read(const struct store_location *where, struct store_cache *cache, struct store_snapshot *snapshot);
void store_snapshot_free(struct store_snapshot *snapshot);
void store_cache_free(struct store_cache *cache);

/* Uncommitted files in a worktree; false when not known (the count is then 0). */
typedef bool store_diff_fn(void *context, const char *worktree, unsigned *count);

/* Writes the protocol header and every row. sessions holds the live tmux
 * session names, one per line; extra_rows (may be NULL) is appended as is. */
bool store_write(const struct store_snapshot *snapshot, const char *sessions, store_diff_fn *diff, void *context,
                 const char *extra_rows, FILE *out);

/* What a look at the project's workflow runs directory tells, without
 * reading any run: enough to know when the CLI's workflow readers would
 * print what they printed before. */
struct store_runs {
    char project[STORE_TEXT];  /* the project's ID; empty when it has none */
    size_t count;              /* run_* entries */
    /* Every run has ended (finished, failed or cancelled): what the CLI
     * prints for it no longer depends on the clock or on a live owner. */
    bool settled;
    /* Changes when a run is added or removed or a recorded graph changes. */
    unsigned long long graphs;
    /* Changes when the records a settled run is shown from change: its
     * state, its step directories and its spawned heads' retirement. */
    unsigned long long records;
};
void store_runs(const struct store_location *where, struct store_runs *runs);

/* A change stamp of one file: false when it cannot be read. */
struct store_stamp {
    dev_t device;
    ino_t inode;
    off_t size;
    time_t seconds;
    long nanoseconds;
};
bool store_stamp(const char *path, struct store_stamp *stamp);
bool store_stamp_equal(const struct store_stamp *a, const struct store_stamp *b);
bool store_valid_id(const char *value);
/* A path from a printf format; empty when it does not fit the buffer. */
void store_path(char *out, size_t size, const char *format, ...);
/* The first line of a file as the shell reader takes it; false when the file
 * cannot be read. */
bool store_first_line(const char *path, char *out, size_t size);
#endif
