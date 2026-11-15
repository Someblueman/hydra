#include "fleet/plan/plan.h"
#include <stdint.h>

/* Step dependencies as one 64-bit row per step: bit j of row i means step i
 * needs step j. PLAN_STEPS is 64, so a row holds every possible step. */
typedef uint64_t plan_row;

static size_t step_count(json_object *steps) {
    size_t count = json_object_is_type(steps, json_type_array) ? json_object_array_length(steps) : 0;
    return count < PLAN_STEPS ? count : PLAN_STEPS;
}

/* The steps step i names in needs; each name that is no step is reported to
 * errors when errors is not NULL, and otherwise ignored. */
static plan_row direct_needs(json_object *steps, size_t i, json_object *errors) {
    json_object *needs = f_field(json_object_array_get_idx(steps, i), "needs");
    size_t count = json_object_is_type(needs, json_type_array) ? json_object_array_length(needs) : 0;
    plan_row row = 0;
    for (size_t j = 0; j < count; j++) {
        int index = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
        if (index >= 0 && (size_t)index < PLAN_STEPS) row |= (plan_row)1 << index;
        else if (errors) plan_error(errors, "steps.needs", "missing_dependency", "dependency does not name a step");
    }
    return row;
}

void plan_reachability(json_object *steps, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors) {
    plan_row rows[PLAN_STEPS] = {0};
    size_t n = step_count(steps);
    for (size_t i = 0; i < n; i++) rows[i] = direct_needs(steps, i, errors);
    /* Warshall's transitive closure, one row of bits at a time. */
    for (size_t k = 0; k < n; k++)
        for (size_t i = 0; i < n; i++)
            if ((rows[i] >> k) & 1U) rows[i] |= rows[k];
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) reach[i][j] = ((rows[i] >> j) & 1U) != 0;
}

static unsigned bits(plan_row row) {
    unsigned count = 0;
    for (; row; row &= row - 1) count++;
    return count;
}

size_t plan_order(json_object *steps, size_t order[PLAN_STEPS]) {
    plan_row needs[PLAN_STEPS];
    unsigned pending[PLAN_STEPS];
    size_t n = step_count(steps), count = 0;
    for (size_t i = 0; i < n; i++) {
        needs[i] = direct_needs(steps, i, NULL);
        pending[i] = bits(needs[i]);
        if (!pending[i]) order[count++] = i;
    }
    /* Kahn's algorithm: a step is placed once every step it needs is. */
    for (size_t next = 0; next < count; next++)
        for (size_t i = 0; i < n; i++)
            if (((needs[i] >> order[next]) & 1U) && !--pending[i]) order[count++] = i;
    return count;
}
