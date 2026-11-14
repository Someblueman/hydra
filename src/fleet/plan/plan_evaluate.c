#include "fleet/plan/plan_inspect.h"
#include <string.h>

/* Three separate concerns of a compiled plan: decomposition quality (what the
 * graph covers and transfers), fixed-graph scheduling (how long the declared
 * graph takes under supplied or recorded step ranges) and host placement
 * (declared hosts only). Every estimate-derived figure is a range or null;
 * hard budgets are copied from the envelope and never adjusted. */
static json_object *range(int64_t low, int64_t high) {
    json_object *out = json_object_new_array();
    json_object_array_add(out, json_object_new_int64(low));
    json_object_array_add(out, json_object_new_int64(high)); return out;
}
static json_object *step_range(json_object *estimates, const char *id, const char *metric) {
    return f_field(f_field(f_field(estimates, "steps"), id), metric);
}
/* Sum of a metric over the selected steps; null as soon as one is unknown. */
static json_object *sum_selected(json_object *steps, json_object *estimates, const char *metric, const bool *selected) {
    int64_t totals[2] = {0, 0};
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *values;
        if (!selected[i]) continue;
        values = step_range(estimates, f_string(json_object_array_get_idx(steps, i), "id"), metric);
        if (!values) return NULL;
        for (size_t k = 0; k < 2; k++) totals[k] += json_object_get_int64(json_object_array_get_idx(values, k));
    }
    return range(totals[0], totals[1]);
}
struct graph {
    size_t count;
    bool reach[PLAN_STEPS][PLAN_STEPS], required[PLAN_STEPS], check[PLAN_STEPS];
    unsigned level[PLAN_STEPS];
};
static void mark_targets(json_object *steps, json_object *records, struct graph *g, bool checks) {
    for (size_t i = 0; i < json_object_array_length(records); i++) {
        int target = plan_index(steps, f_string(json_object_array_get_idx(records, i), "step"));
        if (target < 0) continue;
        if (checks) g->check[target] = true;
        g->required[target] = true;
        for (size_t j = 0; j < g->count; j++) if (g->reach[target][j]) g->required[j] = true;
    }
}
/* 1 + the highest level among a step's dependencies, or 0 while one is unresolved. */
static unsigned level_of(json_object *steps, size_t i, const unsigned *level) {
    json_object *needs = f_field(json_object_array_get_idx(steps, i), "needs"); unsigned best = 0;
    for (size_t j = 0; j < json_object_array_length(needs); j++) {
        int d = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
        if (d < 0 || !level[d]) return 0;
        if (level[d] > best) best = level[d];
    }
    return best + 1;
}
static void levels(json_object *steps, struct graph *g) {
    size_t done = 0;
    for (size_t pass = 0; pass < g->count && done < g->count; pass++)
        for (size_t i = 0; i < g->count; i++)
            if (!g->level[i] && (g->level[i] = level_of(steps, i, g->level))) done++;
}
static void analyse(json_object *plan, struct graph *g) {
    json_object *steps = f_field(plan, "steps");
    memset(g, 0, sizeof(*g)); g->count = json_object_array_length(steps);
    pi_reach(steps, g->reach);
    mark_targets(steps, f_field(plan, "checks"), g, true);
    mark_targets(steps, f_field(plan, "deliverables"), g, false);
    levels(steps, g);
}
/* Longest estimated path into step i over its dependencies' totals. */
static void longest_before(json_object *steps, size_t i, int64_t longest[][2], int64_t out[2]) {
    json_object *needs = f_field(json_object_array_get_idx(steps, i), "needs");
    out[0] = out[1] = 0;
    for (size_t j = 0; j < json_object_array_length(needs); j++) {
        int d = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
        for (size_t k = 0; d >= 0 && k < 2; k++) if (longest[d][k] > out[k]) out[k] = longest[d][k];
    }
}
/* Accumulates step i's longest path; false when its estimate is unknown. */
static bool visit(json_object *steps, json_object *estimates, const struct graph *g, size_t i, int64_t longest[][2], int64_t best[2]) {
    json_object *duration = step_range(estimates, f_string(json_object_array_get_idx(steps, i), "id"), "milliseconds");
    if (!duration) return false;
    longest_before(steps, i, longest, longest[i]);
    for (size_t k = 0; k < 2; k++) {
        longest[i][k] += json_object_get_int64(json_object_array_get_idx(duration, k));
        if (g->check[i] && longest[i][k] > best[k]) best[k] = longest[i][k];
    }
    return true;
}
/* Longest estimated path ending at a check step, over required nodes only. */
static json_object *verified_path(json_object *steps, json_object *estimates, const struct graph *g) {
    int64_t longest[PLAN_STEPS][2] = {{0}}, best[2] = {0, 0}; bool any = false;
    if (!estimates) return NULL;
    for (unsigned level = 1; level <= g->count; level++)
        for (size_t i = 0; i < g->count; i++) {
            if (g->level[i] != level || !g->required[i]) continue;
            if (!visit(steps, estimates, g, i, longest, best)) return NULL;
            any = any || g->check[i];
        }
    return any ? range(best[0], best[1]) : NULL;
}
static const char *step_head(json_object *step) {
    json_object *args = f_field(step, "args");
    return f_string(args, "head") ? f_string(args, "head") : f_string(args, "branch");
}
static json_object *transfers(json_object *plan) {
    json_object *out = json_object_new_object(), *steps = f_field(plan, "steps"), *data = f_field(f_field(plan, "data"), "steps");
    int64_t inputs = 0, cross = 0, bytes = 0;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        json_object *declared = f_field(f_field(data, f_string(step, "id")), "inputs");
        if (!json_object_is_type(declared, json_type_object)) continue;
        json_object_object_foreach(declared, name, input) {
            int producer = plan_index(steps, f_string(input, "step")); json_object *output; (void)name;
            if (producer < 0) continue;
            output = f_field(f_field(f_field(data, f_string(input, "step")), "outputs"), f_string(input, "output"));
            inputs++; bytes += json_object_get_int64(f_field(output, "max_bytes"));
            const char *from = step_head(json_object_array_get_idx(steps, (size_t)producer)), *to = step_head(step);
            if (!from || !to || strcmp(from, to)) cross++;
        }
    }
    json_object_object_add(out, "artifact_inputs", json_object_new_int64(inputs));
    json_object_object_add(out, "cross_head_inputs", json_object_new_int64(cross));
    json_object_object_add(out, "declared_max_bytes", json_object_new_int64(bytes));
    json_object_object_add(out, "observed_bytes", NULL);
    return out;
}
static json_object *rework(json_object *plan) {
    json_object *out = json_object_new_object(), *env = f_field(plan, "envelope");
    int64_t repairs = json_object_get_int64(f_field(env, "repair_budget"));
    json_object_object_add(out, "retry_budget", json_object_get(f_field(env, "retry_budget")));
    json_object_object_add(out, "repair_budget", json_object_get(f_field(env, "repair_budget")));
    f_string_add(out, "repair_scope", repairs ? "bounded; affected checks need fresh evidence" : "none");
    json_object_object_add(out, "probability", NULL);
    f_string_add(out, "basis", "declared budgets only; no rework probability is estimated");
    return out;
}
static json_object *decomposition(json_object *plan, json_object *estimates, const struct graph *g) {
    json_object *out = json_object_new_object(), *effort = json_object_new_object(), *steps = f_field(plan, "steps");
    bool verify[PLAN_STEPS] = {false}; int64_t edges = 0, verifies = 0, required = 0, unconnected = 0;
    for (size_t i = 0; i < g->count; i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        edges += json_object_array_length(f_field(step, "needs"));
        verify[i] = !strcmp(f_string(step, "role"), "verify"); verifies += verify[i];
        if (g->required[i]) required++; else unconnected++;
    }
    json_object_object_add(out, "nodes", json_object_new_int64((int64_t)g->count));
    json_object_object_add(out, "edges", json_object_new_int64(edges));
    json_object_object_add(out, "required_nodes", json_object_new_int64(required));
    json_object_object_add(out, "unconnected_nodes", json_object_new_int64(unconnected));
    json_object_object_add(effort, "checks", json_object_new_int64((int64_t)json_object_array_length(f_field(plan, "checks"))));
    json_object_object_add(effort, "obligations", json_object_new_int64((int64_t)(f_field(plan, "obligations") ? json_object_array_length(f_field(plan, "obligations")) : 0)));
    json_object_object_add(effort, "verify_steps", json_object_new_int64(verifies));
    json_object_object_add(effort, "verify_milliseconds", estimates ? sum_selected(steps, estimates, "milliseconds", verify) : NULL);
    json_object_object_add(effort, "verify_cost_microunits", estimates ? sum_selected(steps, estimates, "cost_microunits", verify) : NULL);
    json_object_object_add(out, "validation_effort", effort);
    json_object_object_add(out, "transfers", transfers(plan));
    json_object_object_add(out, "rework_risk", rework(plan));
    return out;
}
static json_object *scheduling(json_object *compiled, json_object *estimates, const struct graph *g) {
    json_object *plan = f_field(compiled, "plan"), *env = f_field(plan, "envelope"), *steps = f_field(plan, "steps");
    json_object *out = json_object_new_object(), *budgets = json_object_new_object(), *metrics = pi_metrics(compiled, estimates);
    unsigned width[PLAN_STEPS + 1] = {0}, depth = 0, widest = 0;
    for (size_t i = 0; i < g->count; i++) {
        if (g->level[i] > depth) depth = g->level[i];
        if (++width[g->level[i]] > widest) widest = width[g->level[i]];
    }
    json_object_object_add(out, "depth_nodes", json_object_new_int64(depth));
    json_object_object_add(out, "max_level_width", json_object_new_int64(widest));
    const char *keys[] = {"parallelism", "timeout_seconds", "max_heads", NULL};
    for (size_t i = 0; keys[i]; i++) json_object_object_add(budgets, keys[i], json_object_get(f_field(env, keys[i])));
    json_object_object_add(out, "hard_budgets", budgets);
    json_object_object_add(out, "critical_path_milliseconds", json_object_get(f_field(metrics, "critical_path_milliseconds")));
    json_object_object_add(out, "total_work_milliseconds", json_object_get(f_field(metrics, "total_work_milliseconds")));
    json_object_object_add(out, "cost_microunits", json_object_get(f_field(metrics, "cost_microunits")));
    json_object_object_add(out, "time_to_verified_result_milliseconds", verified_path(steps, estimates, g));
    json_object_object_add(out, "cost_to_verified_result_microunits", estimates ? sum_selected(steps, estimates, "cost_microunits", g->required) : NULL);
    json_object_object_add(out, "estimate_budget_conflict", json_object_get(f_field(metrics, "estimate_budget_conflict")));
    json_object_object_add(out, "estimates", json_object_get(estimates));
    json_object_put(metrics);
    return out;
}
static json_object *placement(json_object *plan) {
    json_object *out = json_object_new_object(), *steps = f_field(plan, "steps"); int64_t heads = 0;
    for (size_t i = 0; i < json_object_array_length(steps); i++)
        if (!strcmp(f_string(json_object_array_get_idx(steps, i), "kind"), "spawn")) heads++;
    json_object_object_add(out, "hosts", json_object_get(f_field(f_field(plan, "envelope"), "hosts")));
    json_object_object_add(out, "heads_created", json_object_new_int64(heads));
    return out;
}
static int64_t number(json_object *object, const char *key) { return json_object_get_int64(f_field(object, key)); }
/* Dominance over unconnected work, cross-head transfers and verify steps;
 * matched scope already fixes checks, requirements, envelope and data. */
static void decomposition_preference(json_object *out, bool matched, json_object *l, json_object *r) {
    const char *choice = "unresolved", *reason = "Alternatives differ in no dominated decomposition metric; no deterministic preference is established.";
    int64_t left[3] = {number(l, "unconnected_nodes"), number(f_field(l, "transfers"), "cross_head_inputs"), number(f_field(l, "validation_effort"), "verify_steps")};
    int64_t right[3] = {number(r, "unconnected_nodes"), number(f_field(r, "transfers"), "cross_head_inputs"), number(f_field(r, "validation_effort"), "verify_steps")};
    bool left_better = false, right_better = false;
    for (size_t i = 0; i < 3; i++) { left_better = left_better || left[i] < right[i]; right_better = right_better || right[i] < left[i]; }
    if (!matched) reason = "Scope, acceptance, source or policy differs; decomposition quality is not compared across scopes.";
    else if (left_better && !right_better) choice = "left";
    else if (right_better && !left_better) choice = "right";
    if (strcmp(choice, "unresolved")) reason = "Fewer unconnected nodes, cross-head transfers or verify steps with no metric worse; this is a structural count, not a semantic quality result.";
    f_string_add(out, "modeled_preference", choice); f_string_add(out, "reason", reason);
}
json_object *pe_evaluate(json_object *left, const char *left_id, json_object *right, const char *right_id, json_object *estimates) {
    json_object *data = json_object_new_object(), *mismatch = pi_scope_differences(left, right), *section;
    json_object *lp = f_field(left, "plan"), *rp = f_field(right, "plan");
    json_object *le = f_field(f_field(estimates, "plans"), left_id), *re = f_field(f_field(estimates, "plans"), right_id);
    struct graph lg, rg; bool matched = !json_object_array_length(mismatch);
    analyse(lp, &lg); analyse(rp, &rg);
    json_object_object_add(data, "schema_version", json_object_new_int(1));
    f_string_add(data, "analysis", PLAN_EVALUATION); f_string_add(data, "compiler", PLAN_COMPILER);
    f_string_add(data, "left_sha256", left_id); f_string_add(data, "right_sha256", right_id);
    json_object_object_add(data, "matched_scope", json_object_new_boolean(matched));
    json_object_object_add(data, "scope_differences", mismatch);
    section = json_object_new_object();
    json_object_object_add(section, "left", decomposition(lp, le, &lg));
    json_object_object_add(section, "right", decomposition(rp, re, &rg));
    decomposition_preference(section, matched, f_field(section, "left"), f_field(section, "right"));
    json_object_object_add(data, "decomposition", section);
    section = json_object_new_object();
    json_object_object_add(section, "left", scheduling(left, le, &lg));
    json_object_object_add(section, "right", scheduling(right, re, &rg));
    pi_preference(section, matched, f_field(section, "left"), f_field(section, "right"));
    json_object_object_add(data, "scheduling", section);
    section = json_object_new_object();
    json_object_object_add(section, "left", placement(lp)); json_object_object_add(section, "right", placement(rp));
    f_string_add(section, "modeled_preference", "not_assessed");
    f_string_add(section, "reason", "Only local execution is implemented; declared hosts are not compared or placed.");
    json_object_object_add(data, "placement", section);
    f_string_add(data, "limits", "Supplied or recorded ranges are not calibrated confidence. Fixed-graph paths ignore contention, queueing, transfers and recovery. Hard budgets come from the envelope and are never relaxed by estimates. Rework risk is declared budget, not probability. No preference claims semantic adequacy or global optimality.");
    return data;
}
