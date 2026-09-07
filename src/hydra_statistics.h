#ifndef HYDRA_STATISTICS_H
#define HYDRA_STATISTICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define HS_RUNS 128
#define HS_STEPS 1024
enum hs_state { HS_SUCCEEDED, HS_FAILED, HS_RUNNING, HS_QUEUED, HS_BLOCKED, HS_CANCELLED, HS_UNKNOWN, HS_STATES };
struct hs_run {
    char id[80], name[80], project[80], state[40];
    uint64_t created;
    bool partial;
};
struct hs_step {
    size_t run;
    char id[65], kind[32], state[40];
    uint64_t started, completed;
    unsigned attempts;
    bool attempts_known;
};
/* Caller owns this bounded value object and its storage. Loading replaces it.
 * Missing numeric evidence stays unknown; malformed framing rejects the stream. */
struct hs_model {
    struct hs_run runs[HS_RUNS];
    struct hs_step steps[HS_STEPS];
    size_t run_count, step_count, warnings;
    uint64_t observed;
    char warning[256];
};
struct hs_filter { unsigned days; bool attention; char query[80], workflow[80]; };
struct hs_summary {
    size_t runs, steps, run_states[HS_STATES], step_states[HS_STATES];
    size_t attempts_known, duration_known, undated, excluded_undated, partial_runs;
    uint64_t attempts, retries, duration_sum, duration_max;
    size_t daily[7];
};
bool hs_load(FILE *input, struct hs_model *out);
enum hs_state hs_state(const char *state);
bool hs_matches(const struct hs_model *m, size_t run, const struct hs_filter *filter);
/* Summary includes exactly the matched run cohort and all its recorded steps.
 * Durations are completed latest attempts, not total run/verification duration. */
void hs_summarize(const struct hs_model *m, const struct hs_filter *filter, struct hs_summary *out);
bool hs_duration(const struct hs_model *m, const struct hs_step *step, uint64_t *seconds);
#endif
