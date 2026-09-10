#define _XOPEN_SOURCE 700
#include "fleet/plan/plan_reuse.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/workflow/workflow_task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool same_attempt(const char *run, const char *id, char attempt[F_PATH]) {
    char authoritative[F_PATH];
    return !plan_attempt_directory(run, id, attempt) && !wd_producer_directory(run, id, authoritative) &&
        !strcmp(attempt, authoritative);
}
static int bind_file(json_object *out, const char *directory, const char *name) {
    char path[F_PATH], digest[65];
    if (f_path(path, sizeof(path), directory, name) || f_hash(path, digest)) return -1;
    f_string_add(out, name, digest); return 0;
}
static json_object *input_digests(const char *run, json_object *compiled, const char *id, const char *attempt) {
    char scratch[] = "/tmp/hydra-plan-reuse.XXXXXX", expected[F_PATH], actual[F_PATH], left[65], right[65];
    json_object *data = f_field(compiled, "data"), *out = json_object_new_object(); bool valid = false;
    json_object *inputs = f_field(f_field(f_field(data, "steps"), id), "inputs");
    if (!mkdtemp(scratch)) goto bad;
    if (wd_prepare(data, run, id, scratch)) goto done;
    json_object_object_foreach(inputs, name, reference) {
        (void)reference;
        if (snprintf(expected, sizeof(expected), "%s/inputs/%s", scratch, name) >= (int)sizeof(expected) ||
            snprintf(actual, sizeof(actual), "%s/inputs/%s", attempt, name) >= (int)sizeof(actual) ||
            f_hash(expected, left) || f_hash(actual, right) || strcmp(left, right)) goto done;
        f_string_add(out, name, left);
    }
    valid = true;
done:
    f_remove_tree(scratch);
    if (valid) return out;
bad:
    json_object_put(out); return NULL;
}
static bool remote_valid(const char *run, const char *id, const char *attempt,
                         json_object *binding) {
    char remote[F_PATH]; json_object *receipt = NULL, *result = NULL, *checked = NULL; bool valid = false;
    if (f_path(remote, sizeof(remote), attempt, "remote") ||
        !(receipt = task_read_record(remote, "receipt.json")) ||
        !wt_parent_receipt(run, id, attempt, binding, receipt)) goto done;
    result = task_read_record(remote, "result.json");
    checked = task_result_verify(result);
    /* Runtime observations in the submit receipt and sealed result differ.
     * Both must bind to this original dispatch; compare the task identity. */
    json_object *final = f_field(f_field(result, "result"), "receipt");
    valid = json_object_get_boolean(f_field(checked, "ok")) &&
        wt_parent_receipt(run, id, attempt, binding, final) &&
        !strcmp(f_string(receipt, "task_id"), f_string(final, "task_id"));
done:
    json_object_put(checked); json_object_put(result); json_object_put(receipt); return valid;
}
json_object *pr_snapshot(const char *run, json_object *compiled, const char *id) {
    char attempt[F_PATH], environment[F_PATH], digest[65]; json_object *out = NULL, *inputs = NULL;
    json_object *binding = f_field(f_field(compiled, "tasks"), id);
    json_object *rule = f_field(f_field(f_field(f_field(compiled, "plan"), "reuse_policy"), "steps"), id);
    const char *const receipts[] = {"outputs.json", "remote/dispatch.json", "remote/receipt.json", "remote/result.json", NULL};
    if (!rule || !same_attempt(run, id, attempt) || !binding ||
        snprintf(environment, sizeof(environment), "%s/inputs/%s", attempt, f_string(rule, "environment_input")) >= (int)sizeof(environment) ||
        !pr_environment(environment, binding) ||
        strcmp(f_string(f_field(f_field(binding, "spec"), "work"), "kind"), "exec") ||
        !remote_valid(run, id, attempt, binding) ||
        wd_verify_output(f_field(compiled, "data"), id, attempt) ||
        !(inputs = input_digests(run, compiled, id, attempt))) goto done;
    out = json_object_new_object(); json_object_object_add(out, "inputs", json_object_get(inputs));
    /* Keep the actual historical execution identity, never invent a new receipt. */
    f_string_add(out, "attempt", strrchr(attempt, '/') + 1);
    if (plan_digest(binding, digest)) goto bad;
    f_string_add(out, "task_binding_sha256", digest);
    if (plan_digest(rule, digest)) goto bad;
    f_string_add(out, "reuse_rule_sha256", digest);
    for (size_t i = 0; receipts[i]; i++) if (bind_file(out, attempt, receipts[i])) goto bad;
    goto done;
bad:
    json_object_put(out); out = NULL;
done:
    json_object_put(inputs); return out;
}
