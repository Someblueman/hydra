#include "fleet/plan/plan_inspect.h"
#include <string.h>

static json_object *read_compiled(const char *path, char digest[65]) {
    json_object *compiled = plan_read(path), *errors = json_object_new_array();
    if (!compiled || !f_number_is(compiled, "schema_version", 1) ||
        !f_string(compiled, "compiler") || strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) ||
        plan_validate(f_field(compiled, "plan"), f_field(compiled, "policy"), errors) || plan_digest(compiled, digest)) {
        json_object_put(compiled); compiled = NULL;
    }
    json_object_put(errors); return compiled;
}
static bool same(json_object *left, json_object *right) {
    return json_object_equal(left, right) != 0;
}
static bool estimate_file_valid(json_object *file, json_object *left, const char *left_id,
                                  json_object *right, const char *right_id) {
    json_object *plans = f_field(file, "plans");
    int count = right && strcmp(left_id, right_id) ? 2 : 1;
    if (!f_number_is(file, "schema_version", 1) || json_object_object_length(file) != 2 ||
        !json_object_is_type(plans, json_type_object) || json_object_object_length(plans) != count) return false;
    return pi_estimates_valid(f_field(plans, left_id), left) &&
        (!right || pi_estimates_valid(f_field(plans, right_id), right));
}
static void difference(json_object *out, const char *name, json_object *left, json_object *right) {
    if (!same(left, right)) json_object_array_add(out, json_object_new_string(name));
}
static json_object *scope_differences(json_object *left, json_object *right) {
    json_object *out = json_object_new_array(), *lp = f_field(left, "plan"), *rp = f_field(right, "plan");
    const char *fields[] = {"objective", "requirements", "checks", "deliverables", "obligations", "envelope", "data", NULL};
    for (size_t i = 0; fields[i]; i++) difference(out, fields[i], f_field(lp, fields[i]), f_field(rp, fields[i]));
    difference(out, "policy", f_field(left, "policy"), f_field(right, "policy"));
    difference(out, "source", f_field(left, "source"), f_field(right, "source"));
    difference(out, "context", f_field(left, "context"), f_field(right, "context"));
    difference(out, "inputs", f_field(f_field(left, "data"), "inputs"), f_field(f_field(right, "data"), "inputs"));
    return out;
}
static json_object *invalidation(json_object *left, json_object *right) {
    json_object *out = json_object_new_object(), *changed = json_object_new_array(), *checks = json_object_new_array();
    const char *fields[] = {"plan", "policy", "source", "profiles", "context", "data", "workflow", "tasks", NULL};
    for (size_t i = 0; fields[i]; i++) difference(changed, fields[i], f_field(left, fields[i]), f_field(right, fields[i]));
    json_object *right_checks = f_field(f_field(right, "plan"), "checks");
    for (size_t i = 0; i < json_object_array_length(right_checks); i++) {
        json_object *check = json_object_array_get_idx(right_checks, i), *row = json_object_new_object();
        const char *id = f_string(check, "id"); char old_digest[65], new_digest[65];
        bool reusable = !plan_check_digest(left, id, old_digest) && !plan_check_digest(right, id, new_digest) && !strcmp(old_digest, new_digest);
        f_string_add(row, "id", id); json_object_object_add(row, "binding_changed", json_object_new_boolean(!reusable));
        json_object_array_add(checks, row);
    }
    json_object_object_add(out, "changed_sections", changed); json_object_object_add(out, "checks", checks);
    f_string_add(out, "reuse_policy", "whole-plan-v1; any changed compiled binding requires fresh acceptance evidence; this inspection performs no reuse");
    return out;
}
static bool less_range(json_object *left, json_object *right, bool strict) {
    if (!left || !right) return false;
    int64_t hi = json_object_get_int64(json_object_array_get_idx(left, 1));
    int64_t lo = json_object_get_int64(json_object_array_get_idx(right, 0));
    return strict ? hi < lo : hi <= lo;
}
static void preference(json_object *data, bool matched, json_object *left, json_object *right) {
    const char *choice = "unresolved", *reason = "Estimates are missing, overlap, or trade time against cost; no deterministic preference is established.";
    bool units = same(f_field(f_field(left, "estimates"), "cost_unit"), f_field(f_field(right, "estimates"), "cost_unit"));
    if (!matched) reason = "Scope, acceptance, source or policy differs; these are not matched admissible alternatives.";
    else if (!units) reason = "Cost units differ; costs cannot be compared.";
    else if (json_object_get_boolean(f_field(left, "estimate_budget_conflict")) || json_object_get_boolean(f_field(right, "estimate_budget_conflict")))
        reason = "An estimate lower bound exceeds a hard timeout; estimates cannot relax the admitted budget.";
    else if (less_range(f_field(left, "critical_path_milliseconds"), f_field(right, "critical_path_milliseconds"), true) &&
             less_range(f_field(left, "cost_microunits"), f_field(right, "cost_microunits"), false)) choice = "left";
    else if (less_range(f_field(right, "critical_path_milliseconds"), f_field(left, "critical_path_milliseconds"), true) &&
             less_range(f_field(right, "cost_microunits"), f_field(left, "cost_microunits"), false)) choice = "right";
    if (strcmp(choice, "unresolved")) reason = "Supplied critical-path and cost ranges dominate under the stated model; this is not a scheduling or semantic quality result.";
    f_string_add(data, "modeled_preference", choice); f_string_add(data, "reason", reason);
}
static json_object *comparison(json_object *left, const char *left_id, json_object *right, const char *right_id, json_object *estimates) {
    json_object *data = json_object_new_object(), *mismatch = scope_differences(left, right);
    json_object *lm = pi_metrics(left, f_field(f_field(estimates, "plans"), left_id));
    json_object *rm = pi_metrics(right, f_field(f_field(estimates, "plans"), right_id));
    bool matched = !json_object_array_length(mismatch);
    json_object_object_add(data, "schema_version", json_object_new_int(1));
    f_string_add(data, "compiler", PLAN_COMPILER); f_string_add(data, "left_sha256", left_id); f_string_add(data, "right_sha256", right_id);
    json_object_object_add(data, "matched_scope", json_object_new_boolean(matched));
    json_object_object_add(data, "scope_differences", mismatch);
    json_object_object_add(data, "left", lm); json_object_object_add(data, "right", rm);
    json_object_object_add(data, "invalidation", invalidation(left, right));
    preference(data, matched, lm, rm);
    return data;
}
json_object *plan_inspect_cli(int argc, char **argv) {
    bool compare = !strcmp(argv[0], "compare"); int base = compare ? 3 : 2;
    json_object *left = NULL, *right = NULL, *estimates = NULL, *data = NULL, *result = NULL;
    char left_id[65], right_id[65], estimates_id[65];
    if (argc != base && argc != base + 2) goto done;
    if (!(left = read_compiled(argv[1], left_id))) goto done;
    if (compare && !(right = read_compiled(argv[2], right_id))) goto done;
    if (argc == base + 2 && (strcmp(argv[base], "--estimates") || !(estimates = plan_read(argv[base + 1])) ||
        !estimate_file_valid(estimates, left, left_id, right, right_id) || plan_digest(estimates, estimates_id))) goto done;
    if (compare) data = comparison(left, left_id, right, right_id, estimates);
    else {
        data = pi_explain(left, left_id);
        json_object_object_add(data, "metrics", pi_metrics(left, f_field(f_field(estimates, "plans"), left_id)));
    }
    if (estimates) f_string_add(data, "estimates_sha256", estimates_id);
    else json_object_object_add(data, "estimates_sha256", NULL);
    result = f_success(compare ? "workflow plan compare" : "workflow plan explain", data);
done:
    json_object_put(estimates); json_object_put(right); json_object_put(left);
    return result ? result : f_error("workflow plan inspect", "invalid_inspection_input", "expected a supported compiled plan and optional closed-schema estimates bound to its exact digest");
}
