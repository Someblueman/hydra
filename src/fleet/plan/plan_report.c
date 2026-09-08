#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <string.h>

int plan_check_digest(json_object *compiled, const char *check, char digest[65]) {
    char accepted[65]; json_object *binding; int status;
    if (!plan_id(check) || plan_index(f_field(f_field(compiled, "plan"), "checks"), check) < 0 ||
        plan_digest(compiled, accepted)) return -1;
    binding = json_object_new_object();
    json_object_object_add(binding, "schema_version", json_object_new_int(2));
    f_string_add(binding, "plan_sha256", accepted);
    f_string_add(binding, "check", check);
    status = plan_digest(binding, digest); json_object_put(binding); return status;
}

static bool report_claims(json_object *requirements, json_object *claimed, const char *id) {
    size_t i, j;
    for (i = 0; i < json_object_array_length(claimed); i++) {
        const char *name = f_text(json_object_array_get_idx(claimed, i)); int index;
        if (!plan_id(name) || (index = plan_index(requirements, name)) < 0) return false;
        const char *owner = f_string(json_object_array_get_idx(requirements, (size_t)index), "check");
        if (!owner || strcmp(owner, id)) return false;
        for (j = 0; j < i; j++) if (!strcmp(name, f_text(json_object_array_get_idx(claimed, j)))) return false;
    }
    return true;
}

static bool report_coverage(json_object *requirements, json_object *claimed, const char *id) {
    size_t i;
    if (!report_claims(requirements, claimed, id)) return false;
    for (i = 0; i < json_object_array_length(requirements); i++) {
        json_object *requirement = json_object_array_get_idx(requirements, i);
        const char *owner = f_string(requirement, "check");
        if (!owner || (!strcmp(owner, id) && !plan_has(claimed, f_string(requirement, "id")))) return false;
    }
    return true;
}

static bool report_bindings(json_object *compiled, const char *id, json_object *report,
                            const char *subject, bool v2) {
    const char *const legacy[] = {"schema_version", "verdict", "subject_sha256", "requirements", "evidence", NULL};
    const char *const bound[] = {"schema_version", "verdict", "subject_sha256", "validator_sha256", "requirements", "evidence", NULL};
    char definition[65];
    if ((!v2 && !f_number_is(report, "schema_version", 1)) || !task_keys(report, v2 ? bound : legacy) ||
        !plan_id(id) || !task_hex(subject, 64) || !f_string(report, "subject_sha256") ||
        strcmp(f_string(report, "subject_sha256"), subject) || !plan_list(f_field(report, "requirements"), 1, 64) ||
        !plan_text(f_field(report, "evidence"))) return false;
    if (v2 && (plan_check_digest(compiled, id, definition) || !f_string(report, "validator_sha256") ||
        strcmp(f_string(report, "validator_sha256"), definition))) return false;
    return true;
}

enum plan_verdict plan_report(json_object *compiled, json_object *check,
                              json_object *report, const char *subject) {
    json_object *requirements = f_field(f_field(compiled, "plan"), "requirements");
    const char *verdict = f_string(report, "verdict"), *id = f_string(check, "id");
    bool v2 = f_number_is(report, "schema_version", 2);
    if ((f_number_is(f_field(compiled, "plan"), "schema_version", 2) && !v2) || !verdict || !report_bindings(compiled, id, report, subject, v2) ||
        !report_coverage(requirements, f_field(report, "requirements"), id)) return PLAN_INVALID;
    if (!strcmp(verdict, "pass")) return PLAN_PASS;
    if (!strcmp(verdict, "fail")) return PLAN_FAIL;
    if (v2 && !strcmp(verdict, "inconclusive")) return PLAN_INCONCLUSIVE;
    return PLAN_INVALID;
}

json_object *plan_validation_context(json_object *compiled, const char *step) {
    json_object *checks = f_field(f_field(compiled, "plan"), "checks"), *data = json_object_new_object();
    if (!plan_id(step) || plan_index(f_field(f_field(compiled, "plan"), "steps"), step) < 0) goto bad;
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i); char digest[65];
        const char *owner = f_string(check, "step"), *id = f_string(check, "id");
        if (!owner) goto bad;
        if (strcmp(owner, step)) continue;
        if (plan_check_digest(compiled, id, digest)) goto bad;
        f_string_add(data, id, digest);
    }
    return data;
bad:
    json_object_put(data); return NULL;
}
