#include "fleet/plan/plan_inspect.h"
#include <string.h>

static bool closed(json_object *object, const char *const *keys) {
    if (!json_object_is_type(object, json_type_object)) return false;
    json_object_object_foreach(object, key, value) {
        size_t i = 0; (void)value;
        while (keys[i] && strcmp(keys[i], key)) i++;
        if (!keys[i]) return false;
    }
    return true;
}
static bool range_valid(json_object *range) {
    if (!plan_list(range, 2, 2)) return false;
    json_object *low = json_object_array_get_idx(range, 0), *high = json_object_array_get_idx(range, 1);
    return json_object_is_type(low, json_type_int) && json_object_is_type(high, json_type_int) &&
        json_object_get_int64(low) >= 0 && json_object_get_int64(high) >= json_object_get_int64(low) &&
        json_object_get_int64(high) <= INT64_C(1000000000000);
}
bool pi_estimates_valid(json_object *estimates, json_object *compiled) {
    static const char *const keys[] = {"source", "cost_unit", "steps", NULL};
    static const char *const row_keys[] = {"milliseconds", "cost_microunits", NULL};
    json_object *steps = f_field(estimates, "steps"), *plan_steps = f_field(f_field(compiled, "plan"), "steps");
    if (!closed(estimates, keys) || !plan_text(f_field(estimates, "source")) ||
        !plan_text(f_field(estimates, "cost_unit")) || !json_object_is_type(steps, json_type_object)) return false;
    json_object_object_foreach(steps, id, row) {
        if (plan_index(plan_steps, id) < 0 || !closed(row, row_keys)) return false;
        if (f_field(row, "milliseconds") && !range_valid(f_field(row, "milliseconds"))) return false;
        if (f_field(row, "cost_microunits") && !range_valid(f_field(row, "cost_microunits"))) return false;
    }
    return true;
}
static json_object *range(int64_t low, int64_t high) {
    json_object *out = json_object_new_array();
    json_object_array_add(out, json_object_new_int64(low));
    json_object_array_add(out, json_object_new_int64(high)); return out;
}
static json_object *sum_estimates(json_object *steps, json_object *estimates, const char *metric) {
    int64_t totals[2] = {0, 0};
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        const char *id = f_string(json_object_array_get_idx(steps, i), "id");
        json_object *values = f_field(f_field(f_field(estimates, "steps"), id), metric);
        if (!values) return NULL;
        for (size_t j = 0; j < 2; j++) totals[j] += json_object_get_int64(json_object_array_get_idx(values, j));
    }
    return range(totals[0], totals[1]);
}
struct path_state {
    int64_t longest[PLAN_STEPS][2], totals[2];
    bool done[PLAN_STEPS];
};
static int path_step(json_object *steps, json_object *estimates, size_t index, struct path_state *state) {
    json_object *step = json_object_array_get_idx(steps, index), *needs = f_field(step, "needs");
    int64_t before[2] = {0, 0};
    for (size_t j = 0; j < json_object_array_length(needs); j++) {
        int dependency = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
        if (!state->done[dependency]) return 0;
        for (size_t k = 0; k < 2; k++)
            if (state->longest[dependency][k] > before[k]) before[k] = state->longest[dependency][k];
    }
    json_object *duration = f_field(f_field(f_field(estimates, "steps"), f_string(step, "id")), "milliseconds");
    if (!duration) return -1;
    for (size_t k = 0; k < 2; k++) {
        state->longest[index][k] = before[k] + json_object_get_int64(json_object_array_get_idx(duration, k));
        if (state->longest[index][k] > state->totals[k]) state->totals[k] = state->longest[index][k];
    }
    state->done[index] = true; return 1;
}
static json_object *critical_path(json_object *steps, json_object *estimates) {
    struct path_state state = {0}; size_t count = json_object_array_length(steps), completed = 0;
    for (size_t pass = 0; pass < count && completed < count; pass++) {
        for (size_t i = 0; i < count; i++) {
            if (state.done[i]) continue;
            int status = path_step(steps, estimates, i, &state);
            if (status < 0) return NULL;
            completed += (size_t)status;
        }
    }
    return completed == count ? range(state.totals[0], state.totals[1]) : NULL;
}
static bool budget_conflict(json_object *plan, json_object *estimates, json_object *path) {
    json_object *steps = f_field(plan, "steps");
    int64_t deadline = json_object_get_int64(f_field(f_field(plan, "envelope"), "timeout_seconds")) * 1000;
    if (path && json_object_get_int64(json_object_array_get_idx(path, 0)) > deadline) return true;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i), *timeout = f_field(f_field(step, "args"), "timeout");
        json_object *duration = f_field(f_field(f_field(estimates, "steps"), f_string(step, "id")), "milliseconds");
        if (timeout && duration && json_object_get_int64(json_object_array_get_idx(duration, 0)) > json_object_get_int64(timeout) * 1000) return true;
    }
    return false;
}
json_object *pi_metrics(json_object *compiled, json_object *estimates) {
    json_object *out = json_object_new_object(), *plan = f_field(compiled, "plan"), *steps = f_field(plan, "steps");
    size_t edges = 0, verifies = 0;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        edges += json_object_array_length(f_field(step, "needs"));
        if (!strcmp(f_string(step, "role"), "verify")) verifies++;
    }
    json_object_object_add(out, "nodes", json_object_new_int64((int64_t)json_object_array_length(steps)));
    json_object_object_add(out, "edges", json_object_new_int64((int64_t)edges));
    json_object_object_add(out, "verification_steps", json_object_new_int64((int64_t)verifies));
    json_object_object_add(out, "hard_budgets", json_object_get(f_field(plan, "envelope")));
    json_object_object_add(out, "estimates", json_object_get(estimates));
    json_object *path = critical_path(steps, estimates);
    json_object_object_add(out, "critical_path_milliseconds", path);
    json_object_object_add(out, "estimate_budget_conflict", estimates ? json_object_new_boolean(budget_conflict(plan, estimates, path)) : NULL);
    json_object_object_add(out, "total_work_milliseconds", sum_estimates(steps, estimates, "milliseconds"));
    json_object_object_add(out, "cost_microunits", sum_estimates(steps, estimates, "cost_microunits"));
    json_object_object_add(out, "transfer_bytes", NULL); json_object_object_add(out, "rework_probability", NULL);
    f_string_add(out, "limits", "Ranges are supplied estimates, not calibrated confidence. Critical path ignores resource contention, transfers, queueing and recovery. Timeouts are hard budgets, not runtime estimates. Scheduling and host placement remain separate.");
    return out;
}
