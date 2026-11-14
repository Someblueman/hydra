#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/review_result.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>

/* The short planning flow's native helpers: checking a draft against the
 * project policy, the project policy itself, and the result review as text.
 * Each borrows argv and returns caller-owned JSON, or prints and returns NULL. */

static json_object *check_result(json_object *errors, const char *id) {
    json_object *data = json_object_new_object(), *result;
    json_object_object_add(data, "diagnostics", json_object_get(errors));
    if (id) f_string_add(data, "plan", id);
    result = f_success("workflow plan check", data);
    json_object_object_add(result, "ok", json_object_new_boolean(json_object_array_length(errors) == 0));
    if (json_object_array_length(errors)) {
        json_object *error = json_object_new_object();
        f_string_add(error, "code", "invalid_plan");
        f_string_add(error, "message", "the plan does not pass the plan policy; see data.diagnostics");
        f_string_add(error, "recovery", "fix each listed problem in the draft and publish it again");
        json_object_object_add(result, "error", error);
    }
    return result;
}

/* check <draft> <policy> [--assets-dir <dir>]: the shape, policy, graph and
 * asset checks of validation, without binding a source checkout. */
json_object *plan_check_cli(int argc, char **argv) {
    json_object *draft = plan_read(argv[1]), *policy = plan_read(argv[2]), *errors = json_object_new_array();
    json_object *lowered = NULL, *assets = NULL, *result;
    const char *directory = argc == 5 && !strcmp(argv[3], "--assets-dir") ? argv[4] : NULL;
    if (argc != 3 && !directory) plan_error(errors, "$", "invalid_arguments", "use check <draft.json> <policy.json> [--assets-dir <dir>]");
    else if (!draft || !policy) plan_error(errors, "$", "invalid_json", "expected bounded UTF-8 JSON objects without duplicate members");
    else if ((lowered = plan_lowered(draft, policy, errors))) {
        assets = plan_assets_load(lowered, directory, errors);
        if (!json_object_array_length(errors)) (void)plan_validate(lowered, policy, errors);
    }
    result = check_result(errors, f_string(lowered, "id"));
    json_object_put(assets); json_object_put(lowered); json_object_put(draft); json_object_put(policy); json_object_put(errors);
    return result;
}

static bool policy_shape(json_object *policy) {
    const char *const keys[] = {"schema_version", "envelope", NULL};
    return policy && task_keys(policy, keys) && f_number_is(policy, "schema_version", 1) &&
        json_object_is_type(f_field(f_field(policy, "envelope"), "tools"), json_type_array);
}
/* A tool is one word: letters, digits and . _ + : - only. */
static bool tool_name(const char *tool) {
    size_t n = tool ? strlen(tool) : 0;
    if (!n || n > 64) return false;
    for (size_t i = 0; i < n; i++)
        if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+:-", tool[i])) return false;
    return true;
}
static void tools_remove(json_object *tools, const char *tool) {
    for (size_t i = json_object_array_length(tools); i > 0; i--)
        if (!strcmp(f_text(json_object_array_get_idx(tools, i - 1)), tool)) json_object_array_del_idx(tools, i - 1, 1);
}
static json_object *policy_print(json_object *policy) {
    puts(json_object_to_json_string_ext(policy, JSON_C_TO_STRING_PLAIN));
    return NULL;
}

/* policy-for <policy.json> <profile|->: the policy a planning head plans
 * under, which also authorizes that head's own agent profile. */
json_object *plan_policy_for_cli(int argc, char **argv) {
    json_object *policy = argc == 3 ? plan_read(argv[1]) : NULL, *tools, *result;
    char tool[80];
    if (!policy_shape(policy)) { json_object_put(policy); return plan_failure("workflow plan policy", "invalid_policy", "the plan policy is not a schema 1 policy with an envelope", "reset it with hydra plan policy --reset"); }
    tools = f_field(f_field(policy, "envelope"), "tools");
    if (strcmp(argv[2], "-") && plan_id(argv[2]) && snprintf(tool, sizeof(tool), "profile:%s", argv[2]) < (int)sizeof(tool) && !plan_has(tools, tool))
        json_object_array_add(tools, json_object_new_string(tool));
    result = policy_print(policy);
    json_object_put(policy);
    return result;
}

/* policy-tool <policy.json> allow|remove <tool>: the edited policy. */
json_object *plan_policy_tool_cli(int argc, char **argv) {
    json_object *policy = argc == 4 ? plan_read(argv[1]) : NULL, *tools, *result;
    bool allow = argc == 4 && !strcmp(argv[2], "allow");
    if (!policy_shape(policy) || (!allow && strcmp(argv[2], "remove")) || !tool_name(argv[3])) {
        json_object_put(policy);
        return plan_failure("workflow plan policy", "invalid_arguments", "a tool is one word of letters, digits and . _ + : -, such as npm or profile:claude",
                            "run hydra plan policy --allow-tool <tool> or --remove-tool <tool>");
    }
    tools = f_field(f_field(policy, "envelope"), "tools");
    tools_remove(tools, argv[3]);
    if (allow) json_object_array_add(tools, json_object_new_string(argv[3]));
    result = policy_print(policy);
    json_object_put(policy);
    return result;
}

static void list_line(const char *label, json_object *items, const char *empty) {
    printf("  %s: ", label);
    for (size_t i = 0; i < json_object_array_length(items); i++) printf("%s%s", i ? ", " : "", f_text(json_object_array_get_idx(items, i)));
    printf("%s\n", json_object_array_length(items) ? "" : empty);
}
static void writes_line(json_object *writes) {
    if (json_object_array_length(writes) == 1 && !strcmp(f_text(json_object_array_get_idx(writes, 0)), "@spawned:*"))
        puts("  Changes: only inside the branches a plan creates; your checkout and existing branches are never written");
    else list_line("Changes allowed in", writes, "nothing");
}
/* policy-text <policy.json>: the policy in words. */
json_object *plan_policy_text_cli(int argc, char **argv) {
    json_object *policy = argc == 2 ? plan_read(argv[1]) : NULL, *env = f_field(policy, "envelope");
    int64_t seconds = json_object_get_int64(f_field(env, "timeout_seconds"));
    if (!policy_shape(policy)) { json_object_put(policy); return plan_failure("workflow plan policy", "invalid_policy", "the plan policy is not a schema 1 policy with an envelope", "reset it with hydra plan policy --reset"); }
    list_line("Tools", f_field(env, "tools"), "none");
    puts("  Agents: the planning head's own agent profile, plus any profile:<name> listed above");
    writes_line(f_field(env, "writes"));
    printf("  Time: at most %lld minute%s per plan, summed over its steps\n", (long long)((seconds + 59) / 60), seconds > 60 ? "s" : "");
    printf("  Heads: at most %d; steps at a time: %d; retries: none; repairs: %d\n", json_object_get_int(f_field(env, "max_heads")),
           json_object_get_int(f_field(env, "parallelism")), json_object_get_int(f_field(env, "repair_budget")));
    printf("  Artifacts: at most %lld KiB; free disk needed: %d MiB\n", (long long)(json_object_get_int64(f_field(env, "artifact_bytes")) / 1024),
           json_object_get_int(f_field(env, "disk_mb")));
    json_object_put(policy);
    return NULL;
}

static void print_line(void *context, const char *line) { (void)context; puts(line); }

/* review-text <run-dir> <project-dir> [--json]: the result review of a plan run. */
json_object *plan_review_text_cli(int argc, char **argv) {
    json_object *delivery = NULL, *result;
    char path[F_PATH], *state = NULL;
    bool json = argc == 4 && !strcmp(argv[3], "--json");
    if (argc != 3 && !json) return plan_failure("workflow plan review", "invalid_arguments", "use review-text <run-dir> <project-dir> [--json]", "run hydra plan review");
    if (!f_path(path, sizeof(path), argv[1], "state")) state = f_read(path, 64);
    if (state && !strcmp(state, "succeeded\n")) delivery = plan_delivery(argv[1]);
    free(state);
    result = review_result(argv[1], argv[2], delivery);
    json_object_put(delivery);
    if (!result) return plan_failure("workflow plan review", "not_a_plan_run", "this run has no accepted compiled plan", "name a run that hydra plan approve started");
    if (json) return f_success("workflow plan review", result);
    review_result_text(result, print_line, NULL);
    json_object_put(result);
    return NULL;
}
