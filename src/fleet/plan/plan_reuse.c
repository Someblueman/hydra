#include "fleet/plan/plan_reuse.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>

static bool failed_subject(json_object *plan, const char *id, json_object *failures) {
    json_object *deliveries = f_field(plan, "deliverables");
    json_object_object_foreach(failures, check, report) {
        (void)check;
        int index = plan_index(deliveries, f_string(report, "deliverable"));
        if (index < 0 || !strcmp(id, f_string(json_object_array_get_idx(deliveries, (size_t)index), "step"))) return true;
    }
    return false;
}
static bool checks_pass(const char *run, json_object *compiled, const char *id) {
    json_object *checks = f_field(f_field(compiled, "plan"), "checks");
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (!strcmp(id, f_string(check, "step")) && plan_check_result(compiled, run, check) != PLAN_PASS) return false;
    }
    return true;
}
static bool needs_kept(json_object *step, json_object *kept) {
    json_object *needs = f_field(step, "needs");
    for (size_t i = 0; i < json_object_array_length(needs); i++)
        if (!f_field(kept, f_text(json_object_array_get_idx(needs, i)))) return false;
    return true;
}
json_object *plan_reuse_capture(const char *run, json_object *compiled, json_object *failures) {
    json_object *plan = f_field(compiled, "plan"), *steps = f_field(plan, "steps");
    if (!f_field(plan, "reuse_policy")) return NULL;
    json_object *out = json_object_new_object(), *kept = json_object_new_object();
    json_object_object_add(out, "schema_version", json_object_new_int(1));
    json_object_object_add(out, "steps", kept);
    /* Topological closure: a node is retained only after every predecessor. A
     * missing/unsupported predecessor invalidates every downstream candidate. */
    for (size_t pass = 0; pass < json_object_array_length(steps); pass++) {
        bool changed = false;
        for (size_t i = 0; i < json_object_array_length(steps); i++) {
            json_object *step = json_object_array_get_idx(steps, i), *proof;
            const char *id = f_string(step, "id");
            if (f_field(kept, id) || !needs_kept(step, kept) || failed_subject(plan, id, failures) ||
                !checks_pass(run, compiled, id)) continue;
            proof = pr_snapshot(run, compiled, id);
            if (proof) { json_object_object_add(kept, id, proof); changed = true; }
        }
        if (!changed) break;
    }
    return out;
}
static bool attempts_match(const char *run, json_object *steps, json_object *kept,
                           int round, bool applying) {
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        const char *id = f_string(json_object_array_get_idx(steps, i), "id");
        json_object *proof = f_field(kept, id); char directory[F_PATH], *end;
        if (plan_attempt_directory(run, id, directory)) return false;
        const char *attempt = strrchr(directory, '/') + 1;
        long number = strtol(attempt + strlen("attempt-"), &end, 10);
        if (*end || number < 1 || number > round) return false;
        /* A pending reset can contain both rounds. Once applied, every old
         * attempt must have its own retained proof; omissions are not reuse. */
        if (proof && number >= round) return false;
        if (!proof && !applying && number != round) return false;
    }
    return true;
}
bool plan_reuse_verify(const char *run, json_object *compiled, json_object *evidence, int round, bool applying) {
    const char *const keys[] = {"schema_version", "steps", NULL};
    json_object *reuse = f_field(evidence, "reuse"), *failures = f_field(evidence, "failures");
    json_object *plan = f_field(compiled, "plan"), *kept = f_field(reuse, "steps"), *steps = f_field(plan, "steps");
    if (!f_field(plan, "reuse_policy")) return reuse == NULL;
    if (!json_object_is_type(failures, json_type_object) || !json_object_object_length(failures) ||
        json_object_object_length(failures) > 64) return false;
    if (!task_keys(reuse, keys) || !f_number_is(reuse, "schema_version", 1) ||
        !json_object_is_type(kept, json_type_object) || json_object_object_length(kept) > (int)PLAN_STEPS ||
        !attempts_match(run, steps, kept, round, applying)) return false;
    json_object_object_foreach(kept, id, proof) {
        int index = plan_index(steps, id);
        if (index < 0 || failed_subject(plan, id, failures) || !needs_kept(json_object_array_get_idx(steps, (size_t)index), kept)) return false;
        json_object *current = pr_snapshot(run, compiled, id);
        bool matches = current && json_object_equal(current, proof);
        json_object_put(current);
        if (!matches) return false;
    }
    return true;
}
