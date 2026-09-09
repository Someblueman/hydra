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

/* Measurements are deliberately limited to finite numeric observations;
 * narrative strings and container values cannot satisfy the evidence requirement. */
static bool meaningful_measurement(json_object *raw) {
    json_object *measurement = f_field(raw, "measurement");
    if (!measurement || (!json_object_is_type(measurement, json_type_int) && !json_object_is_type(measurement, json_type_double))) return false;
    return isfinite(json_object_get_double(measurement));
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

static bool text_list(json_object *values, size_t minimum, size_t maximum) {
    size_t i;
    if (!plan_list(values, minimum, maximum)) return false;
    for (i = 0; i < json_object_array_length(values); i++)
        if (!plan_text(json_object_array_get_idx(values, i))) return false;
    return true;
}

static json_object *accepted_recipe(json_object *compiled, const char *check_id) {
    json_object *accepted = recipe(compiled, check_id);
    const char *predicate = f_string(accepted, "predicate");
    if (predicate && !strcmp(predicate, "equals") &&
        task_keys(accepted, (const char *const[]){"predicate", "cases", NULL}) &&
        plan_list(f_field(accepted, "cases"), 1, 4096) && valid_recipe_cases(accepted)) return accepted;
    json_object_put(accepted); return NULL;
}

static bool record_shape(json_object *record) {
    const char *const keys[] = {"obligation_id", "subject_manifest_sha256", "validator_identity",
        "validator_recipe_sha256", "invocation", "environment", "case_inventory", "observations",
        "raw_evidence_sha256", "counts", "limitations", "reviewer_decision", NULL};
    json_object *invocation = f_field(record, "invocation"), *environment = f_field(record, "environment");
    return task_keys(record, keys) &&
        task_keys(invocation, (const char *const[]){"argv", "exit_code", NULL}) &&
        text_list(f_field(invocation, "argv"), 1, 128) &&
        json_object_is_type(f_field(invocation, "exit_code"), json_type_int) &&
        task_keys(environment, (const char *const[]){"host", "toolchain", NULL}) &&
        nonempty_string(environment, "host") && nonempty_string(environment, "toolchain") &&
        plan_list(f_field(record, "case_inventory"), 1, 4096) &&
        plan_list(f_field(record, "observations"), 1, 4096) &&
        task_hex(f_string(record, "raw_evidence_sha256"), 64) &&
        task_keys(f_field(record, "counts"), (const char *const[]){"executed", "failed", "skipped", NULL}) &&
        text_list(f_field(record, "limitations"), 0, 32);
}

static bool record_bindings(json_object *record, json_object *compiled, const char *subject,
                            const char *check_id) {
    const char *id = f_string(record, "obligation_id"), *hash = f_string(record, "subject_manifest_sha256");
    json_object *declared = obligation(f_field(compiled, "plan"), id);
    const char *owner = f_string(f_field(declared, "evaluation"), "check");
    char recipe_hash[65];
    return plan_id(id) && owner && !strcmp(owner, check_id) && task_hex(hash, 64) &&
        !strcmp(hash, subject) && nonempty_string(record, "validator_identity") &&
        task_hex(f_string(record, "validator_recipe_sha256"), 64) &&
        !plan_recipe_digest(compiled, check_id, recipe_hash) &&
        !strcmp(f_string(record, "validator_recipe_sha256"), recipe_hash);
}

static bool unique_object_ids(json_object *items, const char *key) {
    size_t i, j;
    for (i = 0; i < json_object_array_length(items); i++) {
        const char *id = f_string(json_object_array_get_idx(items, i), key);
        if (!id) return false;
        for (j = 0; j < i; j++)
            if (!strcmp(id, f_string(json_object_array_get_idx(items, j), key))) return false;
    }
    return true;
}

static bool inventory_matches(json_object *inventory, json_object *accepted) {
    size_t i, j;
    if (json_object_array_length(inventory) != json_object_array_length(f_field(accepted, "cases"))) return false;
    for (i = 0; i < json_object_array_length(inventory); i++) {
        const char *id = f_text(json_object_array_get_idx(inventory, i));
        if (!plan_id(id) || !recipe_case(accepted, id)) return false;
        for (j = 0; j < i; j++)
            if (!strcmp(id, f_text(json_object_array_get_idx(inventory, j)))) return false;
    }
    return true;
}

struct observation_counts { int executed, failed; bool measured, inconclusive; };

static bool raw_observation_valid(json_object *observed) {
    char digest[65];
    return task_keys(observed, (const char *const[]){"id", "raw", "raw_sha256", NULL}) &&
        plan_id(f_string(observed, "id")) && json_object_is_type(f_field(observed, "raw"), json_type_object) &&
        task_hex(f_string(observed, "raw_sha256"), 64) && !plan_digest(f_field(observed, "raw"), digest) &&
        !strcmp(digest, f_string(observed, "raw_sha256"));
}

static bool observation_valid(json_object *observed, json_object *accepted) {
    return raw_observation_valid(observed) && recipe_case(accepted, f_string(observed, "id")) &&
        f_field(f_field(observed, "raw"), "actual");
}

/* Unique observations over the entire accepted case set prevent dropped cases
 * from being hidden by a report-supplied inventory or recomputed counts. */
static bool count_observations(json_object *observations, json_object *accepted,
                               struct observation_counts *counts) {
    size_t i;
    if (json_object_array_length(observations) != json_object_array_length(f_field(accepted, "cases")) ||
        !unique_object_ids(observations, "id")) return false;
    for (i = 0; i < json_object_array_length(observations); i++) {
        json_object *observed = json_object_array_get_idx(observations, i), *raw = f_field(observed, "raw");
        json_object *expected;
        if (!observation_valid(observed, accepted)) return false;
        expected = recipe_case(accepted, f_string(observed, "id"));
        counts->executed++;
        if (!json_object_equal(f_field(raw, "actual"), f_field(expected, "expected"))) counts->failed++;
        if (meaningful_measurement(raw)) counts->measured = true;
    }
    return true;
}

static bool record_counts_match(json_object *record, json_object *compiled,
                                 const struct observation_counts *observed) {
    json_object *counts = f_field(record, "counts");
    json_object *declared = obligation(f_field(compiled, "plan"), f_string(record, "obligation_id"));
    char digest[65];
    if (plan_has(f_field(declared, "required_evidence"), "measurements") && !observed->measured) return false;
    return !plan_digest(f_field(record, "observations"), digest) &&
        !strcmp(digest, f_string(record, "raw_evidence_sha256")) &&
        f_number_is(counts, "executed", observed->executed) &&
        f_number_is(counts, "failed", observed->failed) && f_number_is(counts, "skipped", 0);
}

/* Assessments preserve a reviewer's explicit decision and evidence. They do
 * not acquire machine-checkable semantics by imitating executable predicates. */
static bool assessment_observation(json_object *record, struct observation_counts *counts) {
    json_object *observations = f_field(record, "observations"), *inventory = f_field(record, "case_inventory");
    json_object *observed, *raw; const char *id = f_string(record, "obligation_id"), *verdict, *inventory_id;
    if (json_object_array_length(observations) != 1 || json_object_array_length(inventory) != 1) return false;
    observed = json_object_array_get_idx(observations, 0); raw = f_field(observed, "raw");
    verdict = f_string(raw, "verdict"); inventory_id = f_text(json_object_array_get_idx(inventory, 0));
    if (!raw_observation_valid(observed) || strcmp(f_string(observed, "id"), id) ||
        !inventory_id || strcmp(inventory_id, id) || !verdict ||
        (strcmp(verdict, "pass") && strcmp(verdict, "fail") && strcmp(verdict, "inconclusive"))) return false;
    counts->executed = 1;
    counts->failed = !strcmp(verdict, "fail");
    counts->inconclusive = !strcmp(verdict, "inconclusive");
    counts->measured = meaningful_measurement(raw);
    return true;
}

static bool record_observations(json_object *record, json_object *compiled, json_object *check,
                                 struct observation_counts *counts) {
    json_object *accepted; bool valid;
    if (!strcmp(f_string(check, "method"), "assessment")) return assessment_observation(record, counts);
    accepted = accepted_recipe(compiled, f_string(check, "id"));
    if (!accepted) return false;
    valid = inventory_matches(f_field(record, "case_inventory"), accepted) &&
        count_observations(f_field(record, "observations"), accepted, counts);
    json_object_put(accepted); return valid;
}

static bool evidence_record(json_object *record, json_object *compiled, const char *subject,
                            json_object *check, bool *failed, bool *inconclusive) {
    struct observation_counts counts = {0};
    if (!record_shape(record) || !record_bindings(record, compiled, subject, f_string(check, "id")) ||
        !record_observations(record, compiled, check, &counts) || !record_counts_match(record, compiled, &counts)) return false;
    if (counts.failed || json_object_get_int64(f_field(f_field(record, "invocation"), "exit_code")) != 0) *failed = true;
    if (counts.inconclusive) *inconclusive = true;
    return true;
}

static bool state_is(json_object *report, const char *key, const char *a, const char *b, const char *c) {
    const char *value = f_string(report, key);
    return value && (!strcmp(value, a) || !strcmp(value, b) || !strcmp(value, c));
}

static bool report_v3_shape(json_object *report) {
    const char *const keys[] = {"schema_version", "execution_status", "evidence_status", "domain_verdict",
        "verdict", "subject_sha256", "validator_sha256", "evidence_records", "limitations", "requirements", "evidence", NULL};
    const char *verdict = f_string(report, "verdict"), *domain = f_string(report, "domain_verdict");
    return f_number_is(report, "schema_version", 3) && task_keys(report, keys) &&
        state_is(report, "execution_status", "completed", "failed", "skipped") &&
        state_is(report, "evidence_status", "valid", "invalid", "inconclusive") &&
        state_is(report, "domain_verdict", "pass", "fail", "inconclusive") &&
        verdict && !strcmp(verdict, domain) &&
        plan_list(f_field(report, "evidence_records"), 1, 256) &&
        plan_list(f_field(report, "requirements"), 1, 64) &&
        text_list(f_field(report, "limitations"), 0, 32) && plan_text(f_field(report, "evidence"));
}

static bool report_v3_bindings(json_object *compiled, json_object *check, json_object *report,
                               const char *subject) {
    const char *hash = f_string(report, "subject_sha256"), *validator = f_string(report, "validator_sha256");
    char definition[65];
    return task_hex(hash, 64) && !strcmp(hash, subject) && task_hex(validator, 64) &&
        !plan_check_digest(compiled, f_string(check, "id"), definition) && !strcmp(validator, definition) &&
        report_coverage(f_field(f_field(compiled, "plan"), "requirements"),
                        f_field(report, "requirements"), f_string(check, "id"));
}

static bool records_have_obligation(json_object *records, const char *id) {
    size_t i;
    for (i = 0; i < json_object_array_length(records); i++)
        if (!strcmp(f_string(json_object_array_get_idx(records, i), "obligation_id"), id)) return true;
    return false;
}

static bool records_cover_obligations(json_object *records, json_object *compiled, const char *check_id) {
    json_object *obligations = f_field(f_field(compiled, "plan"), "obligations"); size_t i;
    if (!unique_object_ids(records, "obligation_id")) return false;
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *item = json_object_array_get_idx(obligations, i);
        const char *owner = f_string(f_field(item, "evaluation"), "check");
        if (owner && !strcmp(owner, check_id) && !records_have_obligation(records, f_string(item, "id"))) return false;
    }
    return true;
}

static bool assessment_records_valid(json_object *records, json_object *check) {
    const char *const keys[] = {"rubric", "source_locators", "disagreement", "authority", NULL}; size_t i;
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *review = f_field(json_object_array_get_idx(records, i), "reviewer_decision");
        if (!task_keys(review, keys) || !plan_text(f_field(review, "rubric")) ||
            strcmp(f_string(review, "rubric"), f_string(check, "definition")) ||
            !text_list(f_field(review, "source_locators"), 1, 64) ||
            !plan_text(f_field(review, "disagreement")) || !plan_text(f_field(review, "authority"))) return false;
    }
    return true;
}

static enum plan_verdict derived_verdict(json_object *report, bool failed, bool inconclusive) {
    const char *domain = f_string(report, "domain_verdict");
    if (failed || inconclusive || strcmp(f_string(report, "execution_status"), "completed") ||
        strcmp(f_string(report, "evidence_status"), "valid")) {
        if (strcmp(domain, failed ? "fail" : "inconclusive")) return PLAN_INVALID;
        return failed ? PLAN_FAIL : PLAN_INCONCLUSIVE;
    }
    return !strcmp(domain, "pass") ? PLAN_PASS : PLAN_INVALID;
}

static enum plan_verdict report_v3(json_object *compiled, json_object *check, json_object *report, const char *subject) {
    json_object *records = f_field(report, "evidence_records"); bool failed = false, inconclusive = false; size_t i;
    const char *id = f_string(check, "id");
    if (!report_v3_shape(report) || !report_v3_bindings(compiled, check, report, subject)) return PLAN_INVALID;
    for (i = 0; i < json_object_array_length(records); i++)
        if (!evidence_record(json_object_array_get_idx(records, i), compiled, subject, check, &failed, &inconclusive)) return PLAN_INVALID;
    if (!records_cover_obligations(records, compiled, id)) return PLAN_INVALID;
    if (!strcmp(f_string(check, "method"), "assessment") && !assessment_records_valid(records, check)) return PLAN_INVALID;
    return derived_verdict(report, failed, inconclusive);
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
