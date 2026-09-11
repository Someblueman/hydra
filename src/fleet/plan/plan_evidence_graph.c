#include "fleet/plan/plan.h"
#include <string.h>

static bool checks_subject(json_object *checks, const char *step, const char *delivery) {
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (!strcmp(f_string(check, "step"), step) && !strcmp(f_string(check, "deliverable"), delivery)) return true;
    }
    return false;
}
static bool consumes(json_object *step, json_object *inputs, json_object *delivery) {
    const char *source = f_string(f_field(step, "args"), "source_step");
    if (source && !strcmp(source, f_string(delivery, "step"))) return true;
    if (!json_object_is_type(inputs, json_type_object)) return false;
    json_object_object_foreach(inputs, name, ref) {
        const char *producer = f_string(ref, "step"), *output = f_string(ref, "output"); (void)name;
        if (producer && output && !strcmp(producer, f_string(delivery, "step")) && !strcmp(output, f_string(delivery, "output"))) return true;
    }
    return false;
}
static void required_checks(json_object *plan, json_object *delivery, size_t consumer, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors) {
    json_object *checks = f_field(plan, "checks"), *steps = f_field(plan, "steps");
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (strcmp(f_string(check, "deliverable"), f_string(delivery, "id"))) continue;
        int validator = plan_index(steps, f_string(check, "step"));
        if (validator < 0 || !reach[consumer][validator])
            plan_error(errors, "steps.needs", "missing_evidence_join", "consumers of checked artifacts must depend on every required validator");
    }
}
static void join_consumers(json_object *plan, json_object *delivery, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors) {
    json_object *steps = f_field(plan, "steps"), *data = f_field(f_field(plan, "data"), "steps"), *checks = f_field(plan, "checks");
    for (size_t j = 0; j < json_object_array_length(steps); j++) {
        const char *id = f_string(json_object_array_get_idx(steps, j), "id");
        if (checks_subject(checks, id, f_string(delivery, "id")) || !consumes(json_object_array_get_idx(steps, j), f_field(f_field(data, id), "inputs"), delivery)) continue;
        required_checks(plan, delivery, j, reach, errors);
    }
}
static void check_contexts(json_object *plan, json_object *errors) {
    json_object *checks = f_field(plan, "checks"), *data = f_field(f_field(plan, "data"), "steps");
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        const char *id = f_string(json_object_array_get_idx(checks, i), "step"); bool context = false;
        json_object *inputs = f_field(f_field(data, id), "inputs");
        json_object_object_foreach(inputs, name, ref) { (void)name; if (f_field(ref, "validation")) context = true; }
        if (!context) plan_error(errors, "checks", "missing_validation_context", "distributed validators require an input with validation: plan");
    }
}
static void repair_contexts(json_object *plan, json_object *errors) {
    if (!json_object_get_int(f_field(f_field(plan, "envelope"), "repair_budget"))) return;
    json_object *steps = f_field(plan, "steps"), *data = f_field(f_field(plan, "data"), "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i); bool context = false;
        if (!strcmp(f_string(step, "role"), "verify")) continue;
        json_object *inputs = f_field(f_field(data, f_string(step, "id")), "inputs");
        json_object_object_foreach(inputs, name, ref) { (void)name; if (f_field(ref, "repair")) context = true; }
        if (!context && !f_field(f_field(f_field(plan, "reuse_policy"), "steps"), f_string(step, "id")))
            plan_error(errors, "data", "missing_repair_context", "repairable producers and composers require an input with repair: plan unless explicitly eligible for sealed-artifact reuse");
    }
}
int plan_evidence_graph(json_object *plan, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors) {
    if (json_object_array_length(errors) || !f_number_is(plan, "schema_version", 2)) return 0;
    json_object *deliverables = f_field(plan, "deliverables"); bool final = false;
    for (size_t i = 0; i < json_object_array_length(deliverables); i++) {
        json_object *delivery = json_object_array_get_idx(deliverables, i);
        if (!strcmp(f_string(delivery, "destination"), "run-artifact")) final = true;
        join_consumers(plan, delivery, reach, errors);
    }
    if (!final) plan_error(errors, "deliverables", "missing_final", "at least one composed final artifact is required");
    check_contexts(plan, errors); repair_contexts(plan, errors);
    return json_object_array_length(errors) ? -1 : 0;
}
