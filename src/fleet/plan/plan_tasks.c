#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/workflow/workflow_task.h"
#include <string.h>

static bool task_capabilities(json_object *caps) {
    size_t count = json_object_array_length(caps);
    return plan_has(caps, "exec") && (count == 1 || (count == 2 && plan_has(caps, "execution-headless")));
}
static bool task_policy(json_object *binding, json_object *env, json_object *source, int64_t *seconds) {
    json_object *spec = f_field(binding, "spec"), *work = f_field(spec, "work"), *limits = f_field(spec, "limits");
    const char *commit = f_string(f_field(spec, "source"), "commit"), *kind = f_string(work, "kind");
    if (!commit || strcmp(commit, f_string(source, "commit")) || !kind || strcmp(kind, "exec") ||
        !plan_has(f_field(env, "hosts"), f_string(spec, "host")) ||
        !plan_has(f_field(env, "tools"), f_text(json_object_array_get_idx(f_field(work, "argv"), 0))) ||
        !plan_has(f_field(env, "effects"), "execute") || !plan_has(f_field(env, "effects"), "worktree") ||
        !task_capabilities(f_field(spec, "capabilities")) ||
        json_object_get_int64(f_field(limits, "artifact_bytes")) > json_object_get_int64(f_field(env, "artifact_bytes"))) return false;
    *seconds += json_object_get_int64(f_field(limits, "queue_seconds")) + json_object_get_int64(f_field(limits, "startup_seconds")) +
                json_object_get_int64(f_field(limits, "execution_seconds"));
    return true;
}
json_object *plan_task_bindings(json_object *plan, json_object *data, json_object *source, const char *scratch, json_object *errors) {
    json_object *steps = f_field(plan, "steps"), *env = f_field(plan, "envelope"), *out = json_object_new_object();
    int64_t seconds = 0;
    if (wd_initialize(data, f_string(source, "root"), scratch)) goto bad;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *id = f_string(step, "id"), *descriptor = f_string(f_field(step, "args"), "task_input");
        json_object *binding = wt_step_binding(scratch, data, id, descriptor);
        if (!binding || !task_policy(binding, env, source, &seconds)) { json_object_put(binding); goto bad; }
        const char *producer = f_string(f_field(step, "args"), "source_step");
        if (producer) f_string_add(binding, "source_step", producer);
        json_object_object_add(out, id, binding);
    }
    int64_t rounds = 1 + json_object_get_int64(f_field(env, "repair_budget"));
    if (seconds * rounds > json_object_get_int64(f_field(env, "timeout_seconds")) ||
        json_object_array_length(steps) * (size_t)rounds > (size_t)json_object_get_int64(f_field(env, "max_heads"))) goto bad;
    return out;
bad:
    plan_error(errors, "steps", "invalid_task_policy", "task recipes must match source, declared inputs/outputs, explicit hosts, tools, effects and total finite budgets");
    json_object_put(out); return NULL;
}
