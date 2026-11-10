#ifndef HYDRA_TUI_SNAPSHOT_H
#define HYDRA_TUI_SNAPSHOT_H
#include <stdbool.h>
#include <stdio.h>
struct store_location;

/* The local head snapshot without the shell CLI (snapshot.c): Hydra's state
 * store is read in this process (store.h), live sessions come from one tmux
 * command, and uncommitted-file counts from git, a few worktrees at a time.
 * Nothing here waits: begin starts a refresh and step is called until it has
 * ended. One snapshot value serves every refresh, because it remembers what
 * need not be asked again. */
struct native_snapshot;

/* hydra is the CLI used for the rows only the shell reader can produce. */
struct native_snapshot *snapshot_new(const char *hydra);
void snapshot_free(struct native_snapshot *s);

/* Starts a refresh unless one is in flight. selected names the branch whose
 * uncommitted-file count is always read again (may be empty). The whole
 * refresh, commands included, has budget_ms. */
void snapshot_begin(struct native_snapshot *s, const char *selected, long budget_ms);
bool snapshot_active(const struct native_snapshot *s);

/* Advances the refresh in flight. True once it has ended: *rows is then the
 * complete document, rewound and the caller's to close, or NULL when the
 * refresh failed (*timed_out says whether a command ran out of time). */
bool snapshot_step(struct native_snapshot *s, FILE **rows, bool *timed_out);

/* Stops the refresh in flight and its commands. */
void snapshot_cancel(struct native_snapshot *s);

/* Where the records are, once a refresh has located the repository; NULL
 * before that. */
const struct store_location *snapshot_where(const struct native_snapshot *s);

/* One refresh, waited for, printed to out: `hydra-tui --data`. Returns 0, or
 * 1 when the refresh failed. */
int snapshot_print(const char *hydra, FILE *out);
#endif
