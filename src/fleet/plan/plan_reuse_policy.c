#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <string.h>

static bool eligible_declaration(json_object *plan, const char *id, json_object *rule) {
    const char *const keys[] = {"dependencies", "effects", "environment_input", NULL};
    json_object *steps = f_field(plan, "steps"); int index = plan_index(steps, id);
    const char *environment = f_string(rule, "environment_input");
    if (index < 0 || !task_keys(rule, keys) ||
        !f_string(rule, "dependencies") || strcmp(f_string(rule, "dependencies"), "complete") ||
        !f_string(rule, "effects") || strcmp(f_string(rule, "effects"), "artifact_only") || !plan_id(environment)) return false;
    json_object *step = json_object_array_get_idx(steps, (size_t)index);
    json_object *inputs = f_field(f_field(f_field(f_field(plan, "data"), "steps"), id), "inputs");
    const char *input = f_string(f_field(inputs, environment), "input");
    if (strcmp(f_string(step, "kind"), "task") || f_field(f_field(step, "args"), "source_step") ||
        json_object_array_length(f_field(step, "writes")) || !input ||
        !f_field(f_field(f_field(plan, "data"), "inputs"), input)) return false;
    json_object_object_foreach(inputs, name, reference) {
        (void)name;
        if (f_field(reference, "repair") || f_field(reference, "provenance")) return false;
    }
    return true;
}
int plan_reuse_validate(json_object *plan, json_object *errors) {
    const char *const keys[] = {"schema_version", "mode", "steps", NULL};
    json_object *policy = f_field(plan, "reuse_policy"), *steps = f_field(policy, "steps");
    if (!policy) return 0;
    if (!f_number_is(plan, "schema_version", 2) || !task_keys(policy, keys) || !f_number_is(policy, "schema_version", 1) ||
        !f_string(policy, "mode") || strcmp(f_string(policy, "mode"), "sealed_artifacts") ||
        !json_object_is_type(steps, json_type_object) || !json_object_object_length(steps) ||
        json_object_object_length(steps) > (int)PLAN_STEPS) goto invalid;
    json_object_object_foreach(steps, id, rule) {
        if (!eligible_declaration(plan, id, rule)) goto invalid;
    }
    return 0;
invalid:
    plan_error(errors, "reuse_policy", "unsupported_reuse", "schema 2 sealed-artifact reuse requires a version 1 policy, explicit complete dependency and artifact-only effect assumptions, and a bound environment input; dynamic source, repair and provenance inputs are ineligible");
    return -1;
}
