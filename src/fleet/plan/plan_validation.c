#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>

static json_object *run_definition(const char *run) {
    char path[F_PATH], digest[65], *accepted = NULL; json_object *compiled = NULL, *errors = json_object_new_array();
    if (f_path(path, sizeof(path), run, "compiled.json") || !(compiled = plan_read(path)) ||
        plan_validate(f_field(compiled, "plan"), f_field(compiled, "policy"), errors) || plan_digest(compiled, digest) ||
        f_path(path, sizeof(path), run, "plan-accepted") || !(accepted = f_read(path, 66))) goto bad;
    accepted[strcspn(accepted, "\r\n")] = '\0';
    if (strcmp(accepted, digest)) goto bad;
    free(accepted); json_object_put(errors); return compiled;
bad:
    free(accepted); json_object_put(errors); json_object_put(compiled); return NULL;
}
int plan_validation_write(const char *run, const char *step, const char *directory, const char *name) {
    json_object *compiled = run_definition(run), *context = NULL; int status = -1;
    if (!compiled || !f_number_is(f_field(compiled, "plan"), "schema_version", 2)) goto done;
    context = plan_validation_context(compiled, step);
    if (!context || !json_object_object_length(context)) goto done;
    status = task_write_json(directory, name, context, false);
done:
    json_object_put(compiled); json_object_put(context); return status;
}
static enum plan_verdict check_result(json_object *compiled, const char *run, json_object *check) {
    json_object *deliverables = f_field(f_field(compiled, "plan"), "deliverables");
    int index = plan_index(deliverables, f_string(check, "deliverable"));
    json_object *subject = NULL, *file = NULL, *report = NULL; char path[F_PATH]; enum plan_verdict verdict = PLAN_INVALID;
    if (index < 0) return verdict;
    json_object *delivery = json_object_array_get_idx(deliverables, (size_t)index);
    subject = plan_artifact(compiled, run, f_string(delivery, "step"), f_string(delivery, "output"), path);
    file = plan_artifact(compiled, run, f_string(check, "step"), f_string(check, "report"), path);
    if (subject && file && (report = plan_read(path))) verdict = plan_report(compiled, check, report, f_string(subject, "sha256"));
    json_object_put(subject); json_object_put(file); json_object_put(report); return verdict;
}
int plan_step_check(const char *run, const char *step) {
    json_object *compiled = run_definition(run), *observed = json_object_new_object(); int status = -1; char directory[F_PATH];
    if (!compiled || !plan_id(step)) goto done;
    if (!f_number_is(f_field(compiled, "plan"), "schema_version", 2)) { status = 0; goto done; }
    json_object *checks = f_field(f_field(compiled, "plan"), "checks"); bool pass = true;
    const char *names[] = {"invalid", "pass", "fail", "inconclusive"};
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (strcmp(f_string(check, "step"), step)) continue;
        enum plan_verdict verdict = check_result(compiled, run, check);
        f_string_add(observed, f_string(check, "id"), names[verdict]);
        if (verdict != PLAN_PASS) pass = false;
    }
    if (snprintf(directory, sizeof(directory), "%s/steps/%s/attempt-1", run, step) >= (int)sizeof(directory) ||
        task_write_json(directory, "validation.json", observed, true)) goto done;
    status = pass ? 0 : -1;
done:
    json_object_put(observed); json_object_put(compiled); return status;
}
