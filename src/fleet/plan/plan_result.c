#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/retention/retention.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool valid_data(json_object *compiled) {
    char scratch[] = "/tmp/hydra-plan-result.XXXXXX", path[F_PATH], graph[F_PATH];
    json_object *plan = plan_canonical(f_field(compiled, "plan")), *checked = NULL;
    if (!plan || !mkdtemp(scratch)) { json_object_put(plan); return false; }
    json_object_object_add(plan, "data", json_object_get(f_field(compiled, "data")));
    if (!plan_lower(plan, scratch) && !f_path(path, sizeof(path), scratch, "data.json") && !f_path(graph, sizeof(graph), scratch, "graph.tsv")) checked = wd_manifest(path, graph);
    bool valid = checked != NULL;
    json_object_put(checked); json_object_put(plan); f_remove_tree(scratch); return valid;
}

int plan_attempt_directory(const char *run, const char *step, char directory[F_PATH]) {
    char path[F_PATH], *number = NULL, *end = NULL; int status = -1;
    if (!plan_id(step) || snprintf(path, sizeof(path), "%s/steps/%s/attempts", run, step) >= (int)sizeof(path)) return -1;
    number = f_read(path, 16);
    if (!number) return -1;
    long attempt = strtol(number, &end, 10);
    if (attempt >= 1 && attempt <= 11 && end != number && !strcmp(end, "\n") &&
        snprintf(directory, F_PATH, "%s/steps/%s/attempt-%ld", run, step, attempt) < F_PATH) status = 0;
    free(number); return status;
}
/* Read the selected attempt's sealed receipt, including while its validator is
 * finishing. A versioned reuse proof can retain an unchanged original attempt;
 * affected steps always point to their new repair attempt. */
json_object *plan_artifact(json_object *compiled, const char *run, const char *step, const char *name, char path[F_PATH]) {
    char directory[F_PATH]; json_object *receipt, *file, *decl;
    if (!plan_id(name) || plan_attempt_directory(run, step, directory) || snprintf(path, F_PATH, "%s/artifacts/%s", directory, name) >= F_PATH) return NULL;
    decl = f_field(f_field(f_field(f_field(f_field(compiled, "data"), "steps"), step), "outputs"), name);
    if (!f_string(decl, "type")) return NULL;
    receipt = task_read_record(directory, "outputs.json"); file = wd_file(path, decl);
    if (!file || !json_object_equal(file, f_field(f_field(receipt, "files"), name))) { json_object_put(file); file = NULL; }
    json_object_put(receipt); return file;
}
json_object *plan_delivery(const char *run) {
    char path[F_PATH], digest[65]; json_object *compiled = NULL, *plan, *data, *delivery = NULL, *checks, *errors = json_object_new_array();
    size_t i; int status = -1;
    if (retention_expired(run)) goto done;
    if (f_path(path, sizeof(path), run, "compiled.json") || !(compiled = plan_read(path))) goto done;
    plan = f_field(compiled, "plan"); data = f_field(compiled, "data");
    if (plan_validate(plan, f_field(compiled, "policy"), errors)) goto done;
    if (!valid_data(compiled) || wd_verify(data, run) || plan_digest(compiled, digest)) goto done;
    {
        char *accepted;
        if (f_path(path, sizeof(path), run, "plan-accepted") || !(accepted = f_read(path, 66))) goto done;
        accepted[strcspn(accepted, "\r\n")] = '\0';
        bool matches = !strcmp(accepted, digest); free(accepted); if (!matches) goto done;
    }
    delivery = json_object_new_object(); json_object_object_add(delivery, "schema_version", json_object_new_int(1));
    f_string_add(delivery, "plan_sha256", digest); f_string_add(delivery, "verdict", "pass");
    json_object_object_add(delivery, "deliverables", json_object_new_object()); json_object_object_add(delivery, "checks", json_object_new_object());
    {
        json_object *deliverables = f_field(plan, "deliverables");
        for (i = 0; i < json_object_array_length(deliverables); i++) {
            json_object *d = json_object_array_get_idx(deliverables, i), *file = plan_artifact(compiled, run, f_string(d, "step"), f_string(d, "output"), path);
            if (!file) goto done;
            f_string_add(file, "path", path); json_object_object_add(f_field(delivery, "deliverables"), f_string(d, "id"), file);
        }
    }
    checks = f_field(plan, "checks");
    for (i = 0; i < json_object_array_length(checks); i++) {
        json_object *c = json_object_array_get_idx(checks, i), *file = plan_artifact(compiled, run, f_string(c, "step"), f_string(c, "report"), path), *report;
        json_object *subject = f_field(f_field(delivery, "deliverables"), f_string(c, "deliverable"));
        if (!file) goto done;
        json_object_put(file); report = plan_read(path);
        if (!plan_repair_fresh(run, c, f_string(subject, "sha256")) ||
            plan_report(compiled, c, report, f_string(subject, "sha256")) != PLAN_PASS) {
            json_object_put(report); goto done;
        }
        json_object_object_add(f_field(delivery, "checks"), f_string(c, "id"), report);
    }
    status = 0;
done:
    json_object_put(compiled); json_object_put(errors);
    if (status) { json_object_put(delivery); return NULL; }
    return delivery;
}

/* Presentation of an already verified delivery; shares the public result gate. */
void plan_delivery_view(json_object *delivery) {
    printf("Accepted plan: %s\n", f_string(delivery, "plan_sha256"));
    json_object_object_foreach(f_field(delivery, "deliverables"), deliverable_id, file) {
        printf("\nDeliverable %s: %lld bytes (%s)\nSHA-256: %s\nSealed path: %s\n", deliverable_id,
            (long long)json_object_get_int64(f_field(file, "bytes")), f_string(file, "type"), f_string(file, "sha256"), f_string(file, "path"));
    }
    json_object_object_foreach(f_field(delivery, "checks"), check_id, report) {
        json_object *requirements = f_field(report, "requirements"); size_t i;
        printf("\nCheck %s: %s\nSubject SHA-256: %s\nRequirements:", check_id, f_string(report, "verdict"), f_string(report, "subject_sha256"));
        for (i = 0; i < json_object_array_length(requirements); i++) printf(" %s", f_text(json_object_array_get_idx(requirements, i)));
        printf("\nEvidence: %s\n", f_string(report, "evidence"));
        if (f_number_is(report, "schema_version", 3)) {
            json_object *records = f_field(report, "evidence_records");
            printf("Execution status: %s\nEvidence status: %s\nDomain verdict: %s\n",
                f_string(report, "execution_status"), f_string(report, "evidence_status"), f_string(report, "domain_verdict"));
            for (i = 0; i < json_object_array_length(records); i++) {
                json_object *record = json_object_array_get_idx(records, i), *counts = f_field(record, "counts");
                printf("Obligation %s: executed=%d failed=%d skipped=%d\n", f_string(record, "obligation_id"),
                    json_object_get_int(f_field(counts, "executed")), json_object_get_int(f_field(counts, "failed")),
                    json_object_get_int(f_field(counts, "skipped")));
            }
        }
    }
}
int plan_finish(const char *run) {
    json_object *delivery = plan_delivery(run); int status = -1;
    if (delivery) status = task_write_json(run, "delivery.json", delivery, true);
    json_object_put(delivery); return status;
}
