#ifndef HYDRA_TUI_PLAN_DRAFT_H
#define HYDRA_TUI_PLAN_DRAFT_H
#include <stdbool.h>
#include <stddef.h>

/* A readable account of a draft plan that Hydra has not validated yet, read
 * from the JSON the agent published: the workflow schema or the simple form.
 * It says what each step does in words (the branch a spawn creates, the
 * command a step runs, the worker's instructions) and the declared limits.
 * Nothing here is checked; validation still decides what may run. */
#define PLAN_DRAFT_STEPS 32
struct plan_draft_step {
    char id[65], kind[32], role[32], what[1024];
};
struct plan_draft {
    size_t step_count, steps_total;
    unsigned heads, minutes;
    char tools[256], writes[256];
    struct plan_draft_step steps[PLAN_DRAFT_STEPS];
};

/* False when text is not one JSON object with a steps array. */
bool plan_draft_parse(const char *text, size_t length, struct plan_draft *out);
#endif
