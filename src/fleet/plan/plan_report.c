#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <math.h>
#include <stdio.h>
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

int plan_recipe_digest(json_object *compiled, const char *check, char digest[65]) {
    json_object *checks = f_field(f_field(compiled, "plan"), "checks"), *steps = f_field(f_field(compiled, "plan"), "steps");
    json_object *definition = NULL, *binding = NULL; size_t i;
    if (!plan_id(check)) return -1;
    for (i = 0; i < json_object_array_length(checks); i++) {
        json_object *candidate = json_object_array_get_idx(checks, i);
        if (f_string(candidate, "id") && !strcmp(f_string(candidate, "id"), check)) {
            const char *step_id = f_string(candidate, "step"); int index = plan_index(steps, step_id);
            if (index < 0) return -1;
            definition = json_object_new_object();
            f_string_add(definition, "definition", f_string(candidate, "definition"));
            json_object_object_add(definition, "args", json_object_get(f_field(json_object_array_get_idx(steps, (size_t)index), "args")));
            break;
        }
    }
    if (!definition) return -1;
    binding = json_object_new_object();
    json_object_object_add(binding, "schema_version", json_object_new_int(3));
    f_string_add(binding, "check", check);
    json_object_object_add(binding, "recipe", definition);
    i = plan_digest(binding, digest); json_object_put(binding); return (int)i;
}

static json_object *recipe(json_object *compiled, const char *check) {
    json_object *checks = f_field(f_field(compiled, "plan"), "checks"); size_t i;
    for (i = 0; i < json_object_array_length(checks); i++) {
        json_object *candidate = json_object_array_get_idx(checks, i);
        if (f_string(candidate, "id") && !strcmp(f_string(candidate, "id"), check)) return f_parse_value(f_string(candidate, "definition"));
    }
    return NULL;
}

static json_object *recipe_case(json_object *recipe_value, const char *id) {
    json_object *cases = f_field(recipe_value, "cases"); size_t i;
    for (i = 0; i < json_object_array_length(cases); i++)
        if (f_string(json_object_array_get_idx(cases, i), "id") && !strcmp(f_string(json_object_array_get_idx(cases, i), "id"), id)) return json_object_array_get_idx(cases, i);
    return NULL;
}

/* Measurements are deliberately limited to small finite numeric observations;
 * narrative strings and unbounded/container values cannot satisfy the evidence requirement. */
static bool meaningful_measurement(json_object *raw) {
    json_object *measurement = f_field(raw, "measurement");
    if (!measurement || (!json_object_is_type(measurement, json_type_int) && !json_object_is_type(measurement, json_type_double))) return false;
    return isfinite(json_object_get_double(measurement)) && fabs(json_object_get_double(measurement)) <= 1000000000000.0;
}

static bool valid_recipe_cases(json_object *accepted) {
    json_object *cases = f_field(accepted, "cases"); size_t i, j;
    for (i = 0; i < json_object_array_length(cases); i++) {
        json_object *item = json_object_array_get_idx(cases, i);
        const char *id = f_string(item, "id"); json_object *expected = f_field(item, "expected");
        if (!task_keys(item, (const char *const[]){"id", "expected", NULL}) || !plan_id(id) || !expected || json_object_is_type(expected, json_type_null)) return false;
        for (j = 0; j < i; j++) if (!strcmp(id, f_string(json_object_array_get_idx(cases, j), "id"))) return false;
    }
    return true;
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

static json_object *obligation(json_object *plan, const char *id) {
    json_object *items = f_field(plan, "obligations");
    size_t i;
    if (!id || !json_object_is_type(items, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(items); i++)
        if (f_string(json_object_array_get_idx(items, i), "id") &&
            !strcmp(f_string(json_object_array_get_idx(items, i), "id"), id))
            return json_object_array_get_idx(items, i);
    return NULL;
}

static bool nonempty_string(json_object *object, const char *key) {
    const char *value = f_string(object, key);
    return value && *value && strlen(value) <= 8192;
}

/* A v3 record is deliberately concrete. The adapter owns the verdict: the
 * report's counts and prose are checked against observations and never used
 * as a substitute for them. */
static bool evidence_record(json_object *record, json_object *compiled, json_object *plan, const char *subject,
                            const char *check_id, bool *failed,
                            bool *skipped) {
    const char *const keys[] = {"obligation_id", "subject_manifest_sha256", "validator_identity",
        "validator_recipe_sha256", "invocation", "environment", "case_inventory", "observations", "raw_evidence_sha256",
        "counts", "limitations", "reviewer_decision", NULL};
    const char *const invocation_keys[] = {"argv", "exit_code", NULL};
    const char *const environment_keys[] = {"host", "toolchain", NULL};
    const char *const observation_keys[] = {"id", "raw", "raw_sha256", NULL};
    const char *const count_keys[] = {"executed", "failed", "skipped", NULL};
    const char *id = f_string(record, "obligation_id"), *hash = f_string(record, "subject_manifest_sha256");
    json_object *o, *observations, *counts, *inventory, *accepted; char digest[65], recipe_hash[65]; size_t i; bool has_measurement = false;
    int executed = 0, failures = 0, skips = 0;
    accepted = recipe(compiled, check_id);
    if (!accepted || !json_object_is_type(accepted, json_type_object) ||
        !f_string(accepted, "predicate") || strcmp(f_string(accepted, "predicate"), "equals") ||
        !task_keys(accepted, (const char *const[]){"predicate", "cases", NULL}) ||
        !plan_list(f_field(accepted, "cases"), 1, 4096) || !valid_recipe_cases(accepted)) goto bad;
    if (!task_keys(record, keys) || !plan_id(id) || !obligation(plan, id) ||
        !f_string(f_field(obligation(plan, id), "evaluation"), "check") ||
        strcmp(f_string(f_field(obligation(plan, id), "evaluation"), "check"), check_id) ||
        !task_hex(hash, 64) || strcmp(hash, subject) || !nonempty_string(record, "validator_identity") ||
        !task_hex(f_string(record, "validator_recipe_sha256"), 64) || plan_recipe_digest(compiled, check_id, recipe_hash) ||
        strcmp(f_string(record, "validator_recipe_sha256"), recipe_hash) ||
        !task_keys(f_field(record, "invocation"), invocation_keys) ||
        !json_object_is_type(f_field(record, "invocation"), json_type_object) ||
        !json_object_is_type(f_field(f_field(record, "invocation"), "argv"), json_type_array) ||
        !json_object_is_type(f_field(f_field(record, "invocation"), "exit_code"), json_type_int) ||
        !task_keys(f_field(record, "environment"), environment_keys) ||
        !nonempty_string(f_field(record, "environment"), "host") ||
        !nonempty_string(f_field(record, "environment"), "toolchain") ||
        !plan_list(f_field(record, "case_inventory"), 1, 4096) || !plan_list(f_field(record, "observations"), 1, 4096) || !task_hex(f_string(record, "raw_evidence_sha256"), 64) ||
        !task_keys(f_field(record, "counts"), count_keys) || !plan_list(f_field(record, "limitations"), 0, 32)) goto bad;
    (void)check_id;
    inventory = f_field(record, "case_inventory"); observations = f_field(record, "observations");
    if (json_object_get_int(f_field(f_field(record, "invocation"), "exit_code")) != 0) *failed = true;
    for (i = 0; i < json_object_array_length(observations); i++) {
        bool equal;
        o = json_object_array_get_idx(observations, i);
        if (!task_keys(o, observation_keys) || !plan_id(f_string(o, "id")) || !f_field(o, "raw") || !recipe_case(accepted, f_string(o, "id")) ||
            !f_field(f_field(o, "raw"), "actual") || !task_hex(f_string(o, "raw_sha256"), 64) ||
            plan_digest(f_field(o, "raw"), digest) || strcmp(digest, f_string(o, "raw_sha256"))) goto bad;
        equal = json_object_equal(f_field(f_field(o, "raw"), "actual"), f_field(recipe_case(accepted, f_string(o, "id")), "expected"));
        executed++;
        if (!equal) failures++;
        if (meaningful_measurement(f_field(o, "raw"))) has_measurement = true;
        for (size_t j = 0; j < i; j++) if (!strcmp(f_string(o, "id"), f_string(json_object_array_get_idx(observations, j), "id"))) goto bad;
        if (!plan_has(inventory, f_string(o, "id"))) goto bad;
    }
    for (i = 0; i < json_object_array_length(f_field(accepted, "cases")); i++) {
        json_object *expected_case = json_object_array_get_idx(f_field(accepted, "cases"), i);
        const char *id = f_string(expected_case, "id"); bool found = false; size_t j;
        if (!plan_id(id) || !f_field(expected_case, "expected")) { goto bad; }
        for (j = 0; j < json_object_array_length(observations); j++) if (!strcmp(id, f_string(json_object_array_get_idx(observations, j), "id"))) found = true;
        if (!found || !plan_has(inventory, id)) { goto bad; }
    }
    for (i = 0; i < json_object_array_length(inventory); i++) {
        const char *id = f_text(json_object_array_get_idx(inventory, i)); bool found = false; size_t j;
        if (!plan_id(id) || !recipe_case(accepted, id)) { goto bad; }
        for (j = 0; j < json_object_array_length(observations); j++) if (!strcmp(id, f_string(json_object_array_get_idx(observations, j), "id"))) found = true;
        if (!found) { goto bad; }
        for (j = 0; j < i; j++) if (!strcmp(id, f_text(json_object_array_get_idx(inventory, j)))) { goto bad; }
    }
    if (plan_digest(observations, digest) || strcmp(digest, f_string(record, "raw_evidence_sha256"))) { goto bad; }
    if (plan_has(f_field(obligation(plan, id), "required_evidence"), "measurements") && !has_measurement) { goto bad; }
    counts = f_field(record, "counts");
    if (!f_number_is(counts, "executed", executed) || !f_number_is(counts, "failed", failures) || !f_number_is(counts, "skipped", skips)) goto bad;
    *failed = *failed || failures != 0; *skipped = *skipped || skips != 0;
    json_object_put(accepted); return true;
bad:
    json_object_put(accepted); return false;
}

static enum plan_verdict report_v3(json_object *compiled, json_object *check, json_object *report, const char *subject) {
    const char *const keys[] = {"schema_version", "execution_status", "evidence_status", "domain_verdict",
        "verdict", "subject_sha256", "validator_sha256", "evidence_records", "limitations", "requirements", "evidence", NULL};
    const char *execution = f_string(report, "execution_status"), *evidence = f_string(report, "evidence_status");
    const char *domain = f_string(report, "domain_verdict"), *verdict = f_string(report, "verdict");
    json_object *records = f_field(report, "evidence_records"), *requirements = f_field(report, "requirements");
    json_object *obligations = f_field(f_field(compiled, "plan"), "obligations");
    bool failed = false, skipped = false; size_t i; char definition[65];
    if (!f_number_is(report, "schema_version", 3) || !task_keys(report, keys) ||
        !execution || (strcmp(execution, "completed") && strcmp(execution, "failed") && strcmp(execution, "skipped")) ||
        !evidence || (strcmp(evidence, "valid") && strcmp(evidence, "invalid") && strcmp(evidence, "inconclusive")) ||
        !domain || (strcmp(domain, "pass") && strcmp(domain, "fail") && strcmp(domain, "inconclusive")) ||
        !verdict || strcmp(verdict, domain) || !task_hex(f_string(report, "subject_sha256"), 64) ||
        strcmp(f_string(report, "subject_sha256"), subject) || !task_hex(f_string(report, "validator_sha256"), 64) ||
        plan_check_digest(compiled, f_string(check, "id"), definition) || strcmp(f_string(report, "validator_sha256"), definition) ||
        !plan_list(records, 1, 256) || !plan_list(requirements, 1, 64) || !report_coverage(f_field(f_field(compiled, "plan"), "requirements"), requirements, f_string(check, "id")) ||
        !plan_list(f_field(report, "limitations"), 0, 32) ||
        !plan_text(f_field(report, "evidence"))) return false;
    for (i = 0; i < json_object_array_length(records); i++)
        if (!evidence_record(json_object_array_get_idx(records, i), compiled, f_field(compiled, "plan"), subject,
                              f_string(check, "id"), &failed, &skipped)) return false;
    for (i = 0; i < json_object_array_length(records); i++) {
        size_t j;
        for (j = 0; j < i; j++)
            if (!strcmp(f_string(json_object_array_get_idx(records, i), "obligation_id"),
                        f_string(json_object_array_get_idx(records, j), "obligation_id"))) return PLAN_INVALID;
    }
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *o = json_object_array_get_idx(obligations, i), *eval = f_field(o, "evaluation");
        if (eval && !strcmp(f_string(eval, "check"), f_string(check, "id"))) {
            /* record IDs are obligations; avoid accepting partial coverage */
            bool found = false; size_t j;
            for (j = 0; j < json_object_array_length(records); j++)
                if (!strcmp(f_string(json_object_array_get_idx(records, j), "obligation_id"), f_string(o, "id"))) found = true;
            if (!found) return false;
        }
    }
    if (!strcmp(f_string(check, "method"), "assessment")) {
        for (i = 0; i < json_object_array_length(records); i++) {
            const char *const review_keys[] = {"rubric", "source_locators", "disagreement", "authority", NULL};
            json_object *review = f_field(json_object_array_get_idx(records, i), "reviewer_decision");
            if (!task_keys(review, review_keys) || !plan_text(f_field(review, "rubric")) ||
                !plan_list(f_field(review, "source_locators"), 1, 64) ||
                !plan_text(f_field(review, "disagreement")) || !plan_text(f_field(review, "authority"))) return PLAN_INVALID;
        }
    }
    if (failed || skipped || strcmp(execution, "completed") || strcmp(evidence, "valid")) {
        if (!strcmp(domain, failed ? "fail" : "inconclusive") && !strcmp(verdict, domain))
            return failed ? PLAN_FAIL : PLAN_INCONCLUSIVE;
        return PLAN_INVALID;
    }
    return !strcmp(domain, "pass") && !strcmp(verdict, "pass") ? PLAN_PASS : PLAN_INVALID;
}

static bool requires_measurements(json_object *compiled, const char *check_id) {
    json_object *obligations = f_field(f_field(compiled, "plan"), "obligations"); size_t i;
    if (!json_object_is_type(obligations, json_type_array)) return false;
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *o = json_object_array_get_idx(obligations, i), *evaluation = f_field(o, "evaluation");
        if (evaluation && !strcmp(f_string(evaluation, "check"), check_id) &&
            plan_has(f_field(o, "required_evidence"), "measurements")) return true;
    }
    return false;
}

enum plan_verdict plan_report(json_object *compiled, json_object *check,
                              json_object *report, const char *subject) {
    json_object *requirements = f_field(f_field(compiled, "plan"), "requirements");
    const char *verdict = f_string(report, "verdict"), *id = f_string(check, "id");
    bool v2 = f_number_is(report, "schema_version", 2);
    if (f_number_is(report, "schema_version", 3)) return report_v3(compiled, check, report, subject);
    if ((f_number_is(f_field(compiled, "plan"), "schema_version", 2) && (!v2 || requires_measurements(compiled, id))) || !verdict || !report_bindings(compiled, id, report, subject, v2) ||
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
        if (plan_recipe_digest(compiled, id, digest)) goto bad;
        { char recipe_id[128]; if (snprintf(recipe_id, sizeof(recipe_id), "%s-recipe", id) >= (int)sizeof(recipe_id)) goto bad; f_string_add(data, recipe_id, digest); }
    }
    return data;
bad:
    json_object_put(data); return NULL;
}
