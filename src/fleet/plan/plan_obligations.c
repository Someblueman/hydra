#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 9A is deliberately a small obligation layer over the existing requirement,
 * check and workflow-data records. It proves only that an obligation can reach
 * its declared evidence path. It does not inspect the truth of a criterion. */

static json_object *record(json_object *array, const char *id) {
    size_t i;
    if (!id || !json_object_is_type(array, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(array); i++) {
        json_object *row = json_object_array_get_idx(array, i);
        if (f_string(row, "id") && !strcmp(f_string(row, "id"), id)) return row;
    }
    return NULL;
}

static json_object *step_output(json_object *plan, const char *step, const char *output) {
    json_object *steps = f_field(f_field(plan, "data"), "steps");
    return f_field(f_field(f_field(steps, step), "outputs"), output);
}

static int obligation_index(json_object *obligations, const char *id) {
    size_t i;
    if (!id || !json_object_is_type(obligations, json_type_array)) return -1;
    for (i = 0; i < json_object_array_length(obligations); i++)
        if (f_string(json_object_array_get_idx(obligations, i), "id") &&
            !strcmp(f_string(json_object_array_get_idx(obligations, i), "id"), id)) return (int)i;
    return -1;
}

static bool strings(json_object *array, size_t minimum, size_t maximum) {
    size_t i, j;
    if (!plan_list(array, minimum, maximum)) return false;
    for (i = 0; i < json_object_array_length(array); i++) {
        const char *value = f_text(json_object_array_get_idx(array, i));
        if (!value || !*value || strlen(value) > 8192) return false;
        for (j = 0; j < i; j++)
            if (!strcmp(value, f_text(json_object_array_get_idx(array, j)))) return false;
    }
    return true;
}

static bool environment(json_object *value, json_object *envelope) {
    const char *const keys[] = {"hosts", "tools", "effects", NULL};
    const char *const names[] = {"hosts", "tools", "effects"};
    size_t i, j;
    if (!task_keys(value, keys)) return false;
    for (i = 0; i < 3; i++) {
        json_object *declared = f_field(value, names[i]);
        json_object *allowed = f_field(envelope, names[i]);
        if (!strings(declared, 1, 64)) return false;
        for (j = 0; j < json_object_array_length(declared); j++)
            if (!plan_has(allowed, f_text(json_object_array_get_idx(declared, j)))) return false;
    }
    return true;
}

static bool subject(json_object *plan, json_object *value, json_object *requirement,
                    json_object *deliverable) {
    const char *const keys[] = {"deliverable", "step", "output", NULL};
    const char *delivery_id = f_string(value, "deliverable");
    const char *step = f_string(value, "step");
    const char *output = f_string(value, "output");
    (void)plan;
    if (!task_keys(value, keys) || !plan_id(delivery_id) || !plan_id(step) || !plan_id(output) ||
        !deliverable || strcmp(delivery_id, f_string(requirement, "deliverable")) ||
        strcmp(step, f_string(deliverable, "step")) || strcmp(output, f_string(deliverable, "output"))) return false;
    return step_output(plan, step, output) != NULL;
}

static bool evidence_fields(json_object *value) {
    /* These names are the stable v1/v2 report bindings. Additional bounded
     * labels are allowed for future evidence adapters, but they are not
     * treated as observations by this milestone. */
    size_t i; bool subject = false, verdict = false, evidence = false;
    if (!strings(value, 1, 32)) return false;
    for (i = 0; i < json_object_array_length(value); i++) {
        const char *name = f_text(json_object_array_get_idx(value, i));
        if (!strcmp(name, "subject_sha256")) { subject = true; continue; }
        if (!strcmp(name, "verdict")) { verdict = true; continue; }
        if (!strcmp(name, "evidence")) { evidence = true; continue; }
        if (!strcmp(name, "requirements") || !strcmp(name, "raw_evidence") || !strcmp(name, "measurements")) continue;
        /* Obligation IDs are handled as explicit evidence joins below. */
        if (!plan_id(name)) return false;
    }
    return subject && verdict && evidence;
}

static bool intent_reference(json_object *plan, const char *value) {
    const char *prefix = "context:";
    if (!value) return false;
    if (!strcmp(value, "objective")) return true;
    if (strncmp(value, prefix, strlen(prefix)) || !task_path(value + strlen(prefix))) return false;
    return plan_has(f_field(plan, "context"), value + strlen(prefix));
}

static bool completion_rule(const char *value) {
    return value && (!strcmp(value, "pass") || !strcmp(value, "verdict=pass"));
}

static void mismatch(json_object *errors, const char *path, const char *id,
                     const char *message, const char *expected, const char *actual) {
    char counterexample[512];
    if (!expected) expected = "<missing>";
    if (!actual) actual = "<missing>";
    (void)snprintf(counterexample, sizeof(counterexample), "%s: expected %s, observed %s", message, expected, actual);
    plan_obligation_error(errors, path, "impossible_evaluation", message, id, counterexample);
}

static bool check_subject(json_object *plan, json_object *obligation, json_object *requirement,
                          json_object *check, json_object *errors, size_t index) {
    char path[128];
    json_object *deliverable = record(f_field(plan, "deliverables"), f_string(requirement, "deliverable"));
    json_object *evaluation = f_field(obligation, "evaluation");
    json_object *subject_value = f_field(obligation, "subject");
    const char *id = f_string(obligation, "id");
    const char *method = f_string(evaluation, "method");
    const char *check_id = f_string(evaluation, "check");
    const char *check_method = f_string(check, "method");
    const char *check_delivery = f_string(check, "deliverable");
    (void)index;
    if (!subject(plan, subject_value, requirement, deliverable)) {
        snprintf(path, sizeof(path), "obligations[%zu].subject", index);
        mismatch(errors, path, id, "obligation subject is not the exact declared deliverable", f_string(requirement, "deliverable"), f_string(subject_value, "deliverable"));
        return false;
    }
    if (!check || !check_id || !check_delivery || strcmp(check_delivery, f_string(requirement, "deliverable"))) {
        snprintf(path, sizeof(path), "obligations[%zu].evaluation.check", index);
        mismatch(errors, path, id, "evaluation check does not evaluate the obligation requirement", f_string(requirement, "deliverable"), check_delivery);
        return false;
    }
    if (!method || !check_method || strcmp(method, check_method)) {
        snprintf(path, sizeof(path), "obligations[%zu].evaluation.method", index);
        mismatch(errors, path, id, "evaluation method differs from the bound check", check_method, method);
        return false;
    }
    return true;
}

int plan_obligations_validate(json_object *plan, json_object *errors) {
    const char *const keys[] = {"id", "requirement", "intent_ref", "subject", "criterion",
                                "evaluation", "required_evidence", "environment",
                                "completion_rule", "limitations", "domain", NULL};
    const char *const subject_keys[] = {"deliverable", "step", "output", NULL};
    const char *const evaluation_keys[] = {"method", "check", NULL};
    json_object *obligations = f_field(plan, "obligations"), *requirements = f_field(plan, "requirements");
    json_object *envelope = f_field(plan, "envelope");
    size_t i; bool *owned = NULL; int status = 0;
    if (!obligations) return 0; /* schema 1/2 legacy input: projection is derived only */
    if (!plan_list(obligations, 1, PLAN_OBLIGATIONS)) {
        plan_error(errors, "obligations", "invalid_collection", "explicit obligations must contain 1 to 256 records");
        return -1;
    }
    owned = calloc(json_object_array_length(requirements), sizeof(*owned));
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *obligation = json_object_array_get_idx(obligations, i);
        json_object *requirement, *check, *evaluation, *env;
        const char *id = f_string(obligation, "id");
        const char *requirement_id = f_string(obligation, "requirement");
        const char *domain = f_string(obligation, "domain");
        char path[128];
        snprintf(path, sizeof(path), "obligations[%zu]", i);
        if (!task_keys(obligation, keys) || !plan_id(id) || obligation_index(obligations, id) != (int)i) {
            plan_obligation_error(errors, path, "invalid_obligation", "obligation has unknown fields, an invalid ID or a duplicate ID", id, "obligation IDs must be unique and bounded");
            status = -1; continue;
        }
        requirement = record(requirements, requirement_id);
        if (!requirement) {
            plan_obligation_error(errors, path, "orphan_obligation", "obligation names no requirement", id, requirement_id ? requirement_id : "<missing requirement>");
            status = -1; continue;
        }
        owned[(size_t)plan_index(requirements, requirement_id)] = true;
        evaluation = f_field(obligation, "evaluation"); env = f_field(obligation, "environment");
        check = record(f_field(plan, "checks"), f_string(evaluation, "check"));
        if (!intent_reference(plan, f_string(obligation, "intent_ref"))) {
            plan_obligation_error(errors, path, "invalid_intent_reference", "intent_ref must be objective or a declared context:<path>", id, f_string(obligation, "intent_ref"));
            status = -1;
        }
        if (!completion_rule(f_string(obligation, "completion_rule"))) {
            plan_obligation_error(errors, path, "unsupported_completion_rule", "only pass/verdict=pass completion is supported by workflow plan v1", id, f_string(obligation, "completion_rule"));
            status = -1;
        }
        if (!evidence_fields(f_field(obligation, "required_evidence"))) {
            plan_obligation_error(errors, path, "missing_required_evidence", "required_evidence must include subject_sha256, verdict and evidence report fields", id, "subject_sha256 + verdict + evidence");
            status = -1;
        }
        if (!intent_reference(plan, f_string(obligation, "intent_ref")) || !plan_text(f_field(obligation, "criterion")) ||
            !task_keys(f_field(obligation, "subject"), subject_keys) ||
            !task_keys(evaluation, evaluation_keys) || !f_string(evaluation, "method") ||
            (strcmp(f_string(evaluation, "method"), "executable") && strcmp(f_string(evaluation, "method"), "assessment")) ||
            !environment(env, envelope) ||
            !completion_rule(f_string(obligation, "completion_rule")) || !strings(f_field(obligation, "limitations"), 0, 32) ||
            (domain && strcmp(domain, "feature") && strcmp(domain, "performance") && strcmp(domain, "research"))) {
            plan_obligation_error(errors, path, "invalid_obligation", "intent, exact subject, criterion, evaluation, evidence, environment, completion rule, limitations and optional domain are required", id, "all required fields must be explicit; unsupported completion rules are not inferred");
            status = -1; continue;
        }
        if (!check_subject(plan, obligation, requirement, check, errors, i)) status = -1;
        if (!check) {
            snprintf(path, sizeof(path), "obligations[%zu].evaluation.check", i);
            plan_obligation_error(errors, path, "missing_evaluation", "obligation evaluation names no check", id, f_string(evaluation, "check"));
            status = -1;
        }
    }
    if (owned) {
        for (i = 0; i < json_object_array_length(requirements); i++) if (!owned[i]) {
            char path[128]; snprintf(path, sizeof(path), "requirements[%zu].obligations", i);
            plan_error(errors, path, "missing_obligation", "every requirement in an explicit obligation plan needs at least one distinct obligation");
            status = -1;
        }
        free(owned);
    }
    return status;
}

static bool consumes_subject(json_object *plan, json_object *check, json_object *subject_value) {
    json_object *data = f_field(f_field(plan, "data"), "steps");
    json_object *inputs = f_field(f_field(data, f_string(check, "step")), "inputs");
    const char *step = f_string(subject_value, "step"), *output = f_string(subject_value, "output");
    json_object_object_foreach(inputs, name, value) {
        (void)name;
        if (f_string(value, "step") && f_string(value, "output") && !strcmp(f_string(value, "step"), step) && !strcmp(f_string(value, "output"), output)) return true;
    }
    return false;
}

int plan_obligations_graph(json_object *plan, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors) {
    json_object *obligations = f_field(plan, "obligations"), *steps = f_field(plan, "steps");
    size_t i, j, k;
    bool evidence_reach[PLAN_OBLIGATIONS][PLAN_OBLIGATIONS] = {{false}};
    if (!obligations || json_object_array_length(errors)) return 0;
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *obligation = json_object_array_get_idx(obligations, i);
        json_object *evaluation = f_field(obligation, "evaluation");
        json_object *check = record(f_field(plan, "checks"), f_string(evaluation, "check"));
        json_object *subject_value = f_field(obligation, "subject");
        int check_index = plan_index(steps, f_string(check, "step"));
        int subject_index = plan_index(steps, f_string(subject_value, "step"));
        char path[128], counterexample[512];
        if (check && subject_index >= 0 && check_index >= 0 &&
            consumes_subject(plan, check, subject_value) && check_index != subject_index && reach[check_index][subject_index]) continue;
        snprintf(path, sizeof(path), "obligations[%zu].evaluation", i);
        snprintf(counterexample, sizeof(counterexample), "check step %s does not reach subject step %s through declared needs and subject input", f_string(check, "step") ? f_string(check, "step") : "<missing>", f_string(subject_value, "step") ? f_string(subject_value, "step") : "<missing>");
        plan_obligation_error(errors, path, "impossible_evaluation", "required evidence cannot evaluate the exact candidate on a reachable path", f_string(obligation, "id"), counterexample);
    }
    /* Evidence labels that name another obligation are explicit joins. They
     * must form an acyclic relation; ordinary report field labels are leaves. */
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *evidence = f_field(json_object_array_get_idx(obligations, i), "required_evidence");
        for (j = 0; j < json_object_array_length(evidence); j++) {
            const char *ref = f_text(json_object_array_get_idx(evidence, j));
            int target = obligation_index(obligations, ref);
            if (target < 0) continue;
            evidence_reach[i][(size_t)target] = true;
        }
    }
    for (k = 0; k < json_object_array_length(obligations); k++)
        for (i = 0; i < json_object_array_length(obligations); i++)
            for (j = 0; j < json_object_array_length(obligations); j++)
                evidence_reach[i][j] = evidence_reach[i][j] || (evidence_reach[i][k] && evidence_reach[k][j]);
    for (i = 0; i < json_object_array_length(obligations); i++) if (evidence_reach[i][i])
        plan_obligation_error(errors, "obligations.required_evidence", "circular_evidence", "required evidence joins form a cycle", f_string(json_object_array_get_idx(obligations, i), "id"), f_string(json_object_array_get_idx(obligations, i), "id"));
    return json_object_array_length(errors) ? -1 : 0;
}

static void copy_or_null(json_object *out, const char *key, json_object *value) {
    if (value) json_object_object_add(out, key, plan_canonical(value));
    else json_object_object_add(out, key, json_object_new_null());
}

static const char *safe_text(const char *value) { return value ? value : "unknown"; }

static json_object *derived(json_object *plan, json_object *requirement) {
    json_object *out = json_object_new_object();
    json_object *deliverable = record(f_field(plan, "deliverables"), f_string(requirement, "deliverable"));
    json_object *check = record(f_field(plan, "checks"), f_string(requirement, "check"));
    json_object *subject = json_object_new_object(), *evaluation = json_object_new_object(), *environment_value = json_object_new_object();
    char id[128];
    snprintf(id, sizeof(id), "legacy_%s", safe_text(f_string(requirement, "id")));
    f_string_add(out, "id", id); f_string_add(out, "requirement", safe_text(f_string(requirement, "id")));
    json_object_object_add(out, "derived", json_object_new_boolean(true));
    copy_or_null(out, "intent_ref", NULL); copy_or_null(out, "criterion", f_field(requirement, "criterion"));
    f_string_add(subject, "deliverable", safe_text(f_string(requirement, "deliverable")));
    copy_or_null(subject, "step", f_field(deliverable, "step")); copy_or_null(subject, "output", f_field(deliverable, "output"));
    json_object_object_add(out, "subject", subject);
    f_string_add(evaluation, "method", safe_text(f_string(check, "method"))); f_string_add(evaluation, "check", safe_text(f_string(requirement, "check")));
    json_object_object_add(out, "evaluation", evaluation);
    copy_or_null(out, "required_evidence", NULL); json_object_object_add(out, "environment", environment_value);
    copy_or_null(out, "completion_rule", NULL); json_object_object_add(out, "limitations", json_object_new_array());
    f_string_add(out, "semantic_status", "unknown"); f_string_add(out, "structural_status", "legacy_projection");
    return out;
}

json_object *plan_obligations_projection(json_object *plan) {
    json_object *out = json_object_new_array();
    json_object *obligations = f_field(plan, "obligations");
    size_t i;
    if (obligations) {
        for (i = 0; i < json_object_array_length(obligations); i++) {
            json_object *row = plan_canonical(json_object_array_get_idx(obligations, i));
            const char *domain = f_string(row, "domain"), *method = f_string(f_field(row, "evaluation"), "method");
            json_object_object_add(row, "derived", json_object_new_boolean(false));
            f_string_add(row, "structural_status", "structurally_satisfiable");
            f_string_add(row, "semantic_status", ((!domain || !strcmp(domain, "feature")) && method && !strcmp(method, "executable")) ? "not_assessed" : "semantic_review_required");
            json_object_array_add(out, row);
        }
    } else {
        json_object *requirements = f_field(plan, "requirements");
        for (i = 0; i < json_object_array_length(requirements); i++) json_object_array_add(out, derived(plan, json_object_array_get_idx(requirements, i)));
    }
    return out;
}

static bool contains_word(const char *text, const char *word) {
    size_t i, n;
    if (!text || !word) return false;
    n = strlen(word);
    for (i = 0; text[i]; i++) {
        size_t j;
        for (j = 0; j < n && text[i + j] && tolower((unsigned char)text[i + j]) == tolower((unsigned char)word[j]); j++) {}
        if (j == n) return true;
    }
    return false;
}

json_object *plan_obligations_reviews(json_object *plan) {
    json_object *out = json_object_new_array(), *obligations = f_field(plan, "obligations");
    size_t i;
    if (!obligations) return out;
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *obligation = json_object_array_get_idx(obligations, i), *evaluation = f_field(obligation, "evaluation");
        const char *domain = f_string(obligation, "domain"), *definition = NULL;
        json_object *check = record(f_field(plan, "checks"), f_string(evaluation, "check"));
        json_object *review;
        if (check) definition = f_string(check, "definition");
        if ((domain && strcmp(domain, "feature")) || contains_word(f_string(plan, "objective"), "performance") || contains_word(f_string(plan, "objective"), "latency") || contains_word(f_string(plan, "objective"), "throughput")) {
            review = json_object_new_object(); f_string_add(review, "obligation_id", safe_text(f_string(obligation, "id")));
            f_string_add(review, "code", "semantic_review_required");
            f_string_add(review, "message", "structural reachability does not establish semantic adequacy for this objective");
            if (definition && (contains_word(definition, "text") || contains_word(definition, "content")))
                f_string_add(review, "counterexample", "performance or research objective is paired with a text/content check; no metric or domain judgment is structurally proven");
            json_object_array_add(out, review);
        }
    }
    return out;
}
