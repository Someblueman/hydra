#include "fleet/support/json.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <stdio.h>
#include <string.h>

static bool integer(json_object *o, const char *key, int64_t lo, int64_t hi) {
    json_object *v = f_field(o, key); int64_t n = json_object_get_int64(v);
    return json_object_is_type(v, json_type_int) && n >= lo && n <= hi;
}
static bool strings(json_object *array, size_t minimum, bool ids) {
    size_t i, j;
    if (!plan_list(array, minimum, 64)) return false;
    for (i = 0; i < json_object_array_length(array); i++) {
        const char *s = f_text(json_object_array_get_idx(array, i));
        if (!plan_text(json_object_array_get_idx(array, i)) || (ids && !plan_id(s))) return false;
        for (j = 0; j < i; j++) if (!strcmp(s, f_text(json_object_array_get_idx(array, j)))) return false;
    }
    return true;
}
/* The published YAML reader intentionally has no escaping syntax. Reject
 * strings outside its lossless subset instead of rewriting executable bytes. */
static bool yaml_text(const char *s) {
    const char *p;
    if (!s || !*s || strlen(s) > 4096 || s[0] == ' ' || s[strlen(s)-1] == ' ') return false;
    for (p = s; *p; p++) if ((unsigned char)*p < 32 || strchr("'\"{}[],#&*!|>", *p)) return false;
    return true;
}
static bool scope(const char *s) {
    const char *colon; char head[65]; size_t length;
    if (!s || !(colon = strchr(s, ':'))) return false;
    length = (size_t)(colon - s); if (!length || length >= sizeof(head)) return false;
    memcpy(head, s, length); head[length] = '\0';
    return plan_id(head) && (!strcmp(colon + 1, "*") || task_path(colon + 1));
}
static bool permitted_scope(json_object *allowed, const char *s) {
    size_t i;
    for (i = 0; i < json_object_array_length(allowed); i++) {
        const char *a = f_text(json_object_array_get_idx(allowed, i)); size_t n = strlen(a);
        if (!strcmp(a, s) || (n && a[n-1] == '*' && !strncmp(a, s, n-1))) return true;
        if (!strncmp(a, s, n) && s[n] == '/') return true;
    }
    return false;
}
static bool envelope(json_object *o) {
    const char *const keys[] = {"hosts", "tools", "effects", "writes", "parallelism", "timeout_seconds", "artifact_bytes", "max_heads", "disk_mb", "retry_budget", "repair_budget", NULL};
    size_t i;
    if (!task_keys(o, keys) || !strings(f_field(o, "hosts"), 1, false) || !strings(f_field(o, "tools"), 1, false) ||
        !strings(f_field(o, "effects"), 1, true) || !strings(f_field(o, "writes"), 0, false) ||
        !integer(o, "parallelism", 1, 16) || !integer(o, "timeout_seconds", 1, 86400) ||
        !integer(o, "artifact_bytes", 1, 64 * TASK_FILE_LIMIT) || !integer(o, "max_heads", 1, 64) ||
        !integer(o, "disk_mb", 1, 1048576) || !f_number_is(o, "retry_budget", 0) || !f_number_is(o, "repair_budget", 0)) return false;
    for (i = 0; i < json_object_array_length(f_field(o, "writes")); i++) if (!scope(f_text(json_object_array_get_idx(f_field(o, "writes"), i)))) return false;
    return true;
}
static bool exec_recipe(json_object *args, json_object *env, json_object *errors, const char *path) {
    const char *const exec_keys[] = {"head", "argv", "profile", "prompt_input", "result_file", "timeout", NULL};
    json_object *argv = f_field(args, "argv");
    size_t i; char tool[128];
    if (!task_keys(args, exec_keys) || !plan_id(f_string(args, "head")) || !integer(args, "timeout", 1, 86400)) goto invalid;
    if (argv) {
        if (!plan_list(argv, 1, 64) || f_field(args, "profile") || f_field(args, "prompt_input") || f_field(args, "result_file")) goto invalid;
        for (i = 0; i < json_object_array_length(argv); i++) if (!yaml_text(f_text(json_object_array_get_idx(argv, i)))) {
            plan_error(errors, path, "unsupported_argument", "argv must fit workflow schema 1 scalar syntax; put complex code in a source-bound script"); return false;
        }
        if (!plan_has(f_field(env, "tools"), f_text(json_object_array_get_idx(argv, 0)))) goto unauthorized;
    } else {
        if (!plan_id(f_string(args, "profile")) || !plan_id(f_string(args, "prompt_input")) || !plan_id(f_string(args, "result_file"))) goto invalid;
        snprintf(tool, sizeof(tool), "profile:%s", f_string(args, "profile"));
        if (!plan_has(f_field(env, "tools"), tool)) goto unauthorized;
    }
    if (!plan_has(f_field(env, "effects"), "execute")) goto unauthorized;
    return true;
invalid:
    plan_error(errors, path, "invalid_step", "invalid fields, recipe, dependency list, role or write scope"); return false;
unauthorized:
    plan_error(errors, path, "unauthorized", "operation, tool or write scope is outside the declared envelope"); return false;
}
static bool step_fields(json_object *step) {
    const char *const keys[] = {"id", "role", "kind", "needs", "args", "writes", NULL};
    const char *role = f_string(step, "role");
    return task_keys(step, keys) && plan_id(f_string(step, "id")) && role &&
        (!strcmp(role, "work") || !strcmp(role, "compose") || !strcmp(role, "verify")) &&
        strings(f_field(step, "needs"), 0, true) && strings(f_field(step, "writes"), 0, false);
}
static bool supported_version(json_object *plan) {
    return f_number_is(plan, "schema_version", 1) || f_number_is(plan, "schema_version", 2);
}
static bool placed_hosts(json_object *plan, json_object *env) {
    return f_number_is(plan, "schema_version", 2) ||
        (json_object_array_length(f_field(env, "hosts")) == 1 && plan_has(f_field(env, "hosts"), "local"));
}
static bool supported_kind(const char *kind, bool distributed) {
    return kind && (distributed ? !strcmp(kind, "task") : (!strcmp(kind, "exec") || !strcmp(kind, "spawn")));
}
static bool task_recipe(json_object *step) {
    const char *const keys[] = {"task_input", "source_step", NULL};
    json_object *args = f_field(step, "args"); const char *producer = f_string(args, "source_step");
    if (!task_keys(args, keys) || !plan_id(f_string(args, "task_input"))) return false;
    return !f_field(args, "source_step") || (plan_id(producer) && strcmp(f_string(step, "role"), "verify") &&
        plan_has(f_field(step, "needs"), producer));
}
static bool recipe(json_object *step, json_object *env, json_object *errors, const char *path, bool distributed) {
    const char *const spawn_keys[] = {"branch", NULL};
    const char *kind = f_string(step, "kind"), *role = f_string(step, "role");
    json_object *args = f_field(step, "args"), *writes = f_field(step, "writes");
    size_t i;
    if (!step_fields(step)) goto invalid;
    if (!supported_kind(kind, distributed)) {
        plan_error(errors, path, "unsupported_operation", "schema 1 supports local spawn/exec; schema 2 supports explicitly placed task recipes"); return false;
    }
    if (distributed) {
        if (!task_recipe(step)) goto invalid;
    } else if (!strcmp(kind, "spawn")) {
        if (!task_keys(args, spawn_keys) || !plan_id(f_string(args, "branch")) || strcmp(role, "work") || json_object_array_length(writes)) goto invalid;
        if (!plan_has(f_field(env, "effects"), "worktree")) goto unauthorized;
    } else {
        if (!exec_recipe(args, env, errors, path)) return false;
    }
    for (i = 0; i < json_object_array_length(writes); i++) {
        const char *s = f_text(json_object_array_get_idx(writes, i));
        if (!scope(s)) goto invalid;
        if (!permitted_scope(f_field(env, "writes"), s)) goto unauthorized;
        if (!strcmp(kind, "exec")) {
            const char *head = f_string(args, "head"); size_t n = strlen(head);
            if (strncmp(head, s, n) || s[n] != ':') goto invalid;
        }
    }
    return true;
invalid:
    plan_error(errors, path, "invalid_step", "invalid fields, recipe, dependency list, role or write scope"); return false;
unauthorized:
    plan_error(errors, path, "unauthorized", "operation, tool or write scope is outside the declared envelope"); return false;
}
static bool records(json_object *array, const char *path, const char *const keys[], json_object *errors) {
    size_t i;
    if (!plan_list(array, 1, 64)) { plan_error(errors, path, "invalid_collection", "expected 1 to 64 records"); return false; }
    for (i = 0; i < json_object_array_length(array); i++) {
        json_object *row = json_object_array_get_idx(array, i); const char *id = f_string(row, "id");
        if (!task_keys(row, keys) || !plan_id(id) || plan_index(array, id) != (int)i) {
            plan_error(errors, path, "invalid_record", "unknown fields, invalid ID or duplicate ID"); return false;
        }
    }
    return true;
}
static void policy_bounds(json_object *env, json_object *allowed, json_object *errors) {
    size_t i, j;
    const char *sets[] = {"hosts", "tools", "effects", "writes"};
    const char *bounds[] = {"parallelism", "timeout_seconds", "artifact_bytes", "max_heads"};
    for (i = 0; i < 4; i++) {
        json_object *set = f_field(env, sets[i]);
        for (j = 0; j < json_object_array_length(set); j++) {
            const char *s = f_text(json_object_array_get_idx(set, j));
            if (!(i == 3 ? permitted_scope(f_field(allowed, sets[i]), s) : plan_has(f_field(allowed, sets[i]), s)))
                plan_error(errors, sets[i], "unauthorized", "plan exceeds supplied policy");
            if (i == 2 && strcmp(s, "worktree") && strcmp(s, "execute")) plan_error(errors, "effects", "unsupported_effect", "supported effects are worktree and execute");
        }
    }
    for (i = 0; i < 4; i++) if (json_object_get_int64(f_field(env, bounds[i])) > json_object_get_int64(f_field(allowed, bounds[i])))
        plan_error(errors, bounds[i], "over_budget", "plan budget exceeds policy");
    if (json_object_get_int64(f_field(env, "disk_mb")) < json_object_get_int64(f_field(allowed, "disk_mb")))
        plan_error(errors, "disk_mb", "unauthorized", "plan free-space floor is below policy");
}
int plan_validate(json_object *plan, json_object *policy, json_object *errors) {
    const char *const keys[] = {"schema_version", "id", "objective", "context", "assumptions", "questions", "deliverables", "requirements", "checks", "steps", "data", "envelope", NULL};
    const char *const policy_keys[] = {"schema_version", "envelope", NULL};
    const char *const deliverable_keys[] = {"id", "description", "step", "output", "destination", NULL};
    const char *const requirement_keys[] = {"id", "criterion", "deliverable", "check", NULL};
    const char *const check_keys[] = {"id", "method", "definition", "step", "input", "report", "deliverable", NULL};
    json_object *env = f_field(plan, "envelope"), *allowed = f_field(policy, "envelope"), *steps = f_field(plan, "steps");
    bool distributed = f_number_is(plan, "schema_version", 2);
    size_t i; int64_t seconds = 0; unsigned heads = 0; char path[128];
    if (!task_keys(plan, keys) || !supported_version(plan) || !plan_id(f_string(plan, "id")) ||
        !plan_text(f_field(plan, "objective")) || !strings(f_field(plan, "context"), 0, false) ||
        !strings(f_field(plan, "assumptions"), 0, false) || !strings(f_field(plan, "questions"), 0, false))
        plan_error(errors, "$", "invalid_plan", "expected schema 1 or 2, objective, ID and explicit context, assumptions and questions arrays; unknown fields are rejected");
    if (plan_list(f_field(plan, "questions"), 1, 64)) plan_error(errors, "questions", "unresolved_question", "resolve material questions before compilation");
    if (!envelope(env) || !task_keys(policy, policy_keys) || !f_number_is(policy, "schema_version", 1) || !envelope(allowed)) {
        plan_error(errors, "envelope", "invalid_policy", "explicit bounded plan and policy envelopes required; retry and repair budgets must be zero"); return -1;
    }
    if (!placed_hosts(plan, env))
        plan_error(errors, "envelope.hosts", "unsupported_host", "only local execution is implemented");
    policy_bounds(env, allowed, errors);
    if (!plan_list(steps, 1, PLAN_STEPS)) plan_error(errors, "steps", "invalid_steps", "expected 1 to 64 steps");
    else for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        snprintf(path, sizeof(path), "steps[%zu]", i);
        if (!recipe(step, env, errors, path, distributed)) continue;
        if (plan_index(steps, f_string(step, "id")) != (int)i) plan_error(errors, path, "duplicate_id", "step ID is repeated");
        if (!strcmp(f_string(step, "kind"), "spawn")) heads++;
        else seconds += json_object_get_int64(f_field(f_field(step, "args"), "timeout"));
    }
    if (seconds > json_object_get_int64(f_field(env, "timeout_seconds")) || heads > json_object_get_int64(f_field(env, "max_heads")))
        plan_error(errors, "envelope", "over_budget", "sum of execution timeouts or created heads exceeds plan budget");
    (void)records(f_field(plan, "deliverables"), "deliverables", deliverable_keys, errors);
    (void)records(f_field(plan, "requirements"), "requirements", requirement_keys, errors);
    (void)records(f_field(plan, "checks"), "checks", check_keys, errors);
    if (json_object_array_length(errors)) return -1;
    return plan_graph(plan, errors);
}
