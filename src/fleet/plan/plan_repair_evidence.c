#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>

static json_object *negative_reports(const char *run, json_object *compiled, const char *step) {
    json_object *checks = f_field(f_field(compiled, "plan"), "checks"), *out = json_object_new_object();
    char path[F_PATH];
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (strcmp(step, f_string(check, "step"))) continue;
        enum plan_verdict verdict = plan_check_result(compiled, run, check);
        if (verdict == PLAN_PASS) continue;
        if (verdict == PLAN_INVALID) goto bad;
        json_object *file = plan_artifact(compiled, run, step, f_string(check, "report"), path), *report;
        if (!file) goto bad;
        json_object_put(file); report = plan_read(path);
        if (!report) goto bad;
        f_string_add(report, "deliverable", f_string(check, "deliverable"));
        json_object_object_add(out, f_string(check, "id"), report);
    }
    if (json_object_object_length(out)) return out;
bad:
    json_object_put(out); return NULL;
}
static int append_step(const char *run, json_object *compiled, const char *id, json_object *failures) {
    char path[F_PATH], *state; json_object *reports;
    if (snprintf(path, sizeof(path), "%s/steps/%s/state", run, id) >= (int)sizeof(path) || !(state = f_read(path, 64))) return -1;
    bool failed = !strcmp(state, "failed\n"), terminal = failed || !strcmp(state, "succeeded\n") || !strcmp(state, "cancelled\n");
    free(state);
    if (!terminal) return -1;
    if (!failed) return 0;
    reports = negative_reports(run, compiled, id);
    if (!reports) return -1;
    json_object_object_foreach(reports, check, report) { json_object_object_add(failures, check, json_object_get(report)); }
    json_object_put(reports); return 0;
}
json_object *plan_repair_evidence(const char *run, json_object *compiled) {
    json_object *steps = f_field(f_field(compiled, "plan"), "steps"), *out = json_object_new_object(), *failures = json_object_new_object();
    char digest[65]; bool valid = false;
    if (plan_digest(compiled, digest)) goto done;
    f_string_add(out, "plan_sha256", digest); json_object_object_add(out, "failures", json_object_get(failures));
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        if (append_step(run, compiled, f_string(json_object_array_get_idx(steps, i), "id"), failures)) goto done;
    }
    valid = json_object_object_length(failures) > 0;
done:
    json_object_put(failures);
    if (!valid) { json_object_put(out); return NULL; }
    return out;
}
