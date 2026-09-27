#ifndef HYDRA_TUI_PLAN_SUMMARY_H
#define HYDRA_TUI_PLAN_SUMMARY_H
#include <stdbool.h>
#include <stddef.h>

/* What an execution approval authorizes, read from the compiled preview that
 * `workflow plan tui-data` projects (the same text as `workflow plan show`).
 * The approval dialog shows these facts; the CLI still binds the full digest. */
struct plan_summary {
    unsigned parallelism, heads, minutes;
    char tools[256], writes[256];
};

/* Reads the Tools, Declared repository writes and Budgets lines. Returns false
 * when the preview has no complete Budgets line. */
bool plan_summary_parse(const char *preview, size_t length, struct plan_summary *out);
/* "tools sh, git; parallelism 1; at most 4 heads; 60 min wall time" */
void plan_summary_policy(const struct plan_summary *s, char *out, size_t size);
/* "Runs 3 steps, spawns 1 head, up to 60 minutes; writes only inside heads it spawns." */
void plan_summary_consequences(const struct plan_summary *s, size_t steps, size_t spawns, char *out, size_t size);
#endif
