#include "fleet/plan/plan_expansion.h"
#include "fleet/task/task.h"
#include <string.h>

/* The closed schemas of the policy's expansion bounds and of a proposal. No
 * value here is interpreted; structural refusals name the field path. The
 * steps, deliverables, requirements, checks, obligations and data of a
 * proposal are checked by the ordinary compiler once they are lowered. */

static bool bound(json_object *o, const char *key, int64_t lo, int64_t hi) {
    json_object *v = f_field(o, key); int64_t n = json_object_get_int64(v);
    return json_object_is_type(v, json_type_int) && n >= lo && n <= hi;
}
/* policy.expansion: every bound is explicit; absent means expansions are not authorized. */
bool plan_expansion_policy(json_object *policy, json_object *errors) {
    const char *const keys[] = {"max_nodes", "max_work_seconds", "max_artifact_bytes", "max_rounds", "max_repairs_per_claim", NULL};
    json_object *bounds = f_field(policy, "expansion");
    if (!bounds) return true;
    if (task_keys(bounds, keys) && json_object_object_length(bounds) == 5 &&
        bound(bounds, "max_nodes", 1, 4 * PLAN_STEPS) && bound(bounds, "max_work_seconds", 1, 864000) &&
        bound(bounds, "max_artifact_bytes", 1, 64 * TASK_FILE_LIMIT) && bound(bounds, "max_rounds", 1, PLAN_EXPANSION_ROUNDS) &&
        bound(bounds, "max_repairs_per_claim", 0, 10)) return true;
    plan_error(errors, "expansion", "invalid_policy", "policy.expansion declares exactly max_nodes (1-256), max_work_seconds (1-864000), max_artifact_bytes (1-33554432), max_rounds (1-16) and max_repairs_per_claim (0-10)");
    return false;
}

static bool unique_ids(json_object *rows, const char *key) {
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        const char *id = f_string(json_object_array_get_idx(rows, i), key);
        if (!plan_id(id)) return false;
        for (size_t j = 0; j < i; j++) if (!strcmp(id, f_string(json_object_array_get_idx(rows, j), key))) return false;
    }
    return true;
}
static bool claim_shape(json_object *row) {
    const char *const keys[] = {"id", "kind", "of", NULL};
    const char *kind = f_string(row, "kind");
    if (!task_keys(row, keys) || !kind) return false;
    if (!strcmp(kind, "new")) return f_field(row, "of") == NULL;
    return !strcmp(kind, "retry") && plan_id(f_string(row, "of"));
}
/* Every claim names a requirement of the expansion and every requirement is claimed once. */
static bool claims(json_object *proposal, json_object *errors) {
    json_object *rows = f_field(proposal, "claims"), *requirements = f_field(proposal, "requirements");
    if (!plan_list(rows, 1, 64) || !unique_ids(rows, "id")) {
        plan_error(errors, "claims", "invalid_claims", "claims lists 1 to 64 records with unique plan IDs"); return false;
    }
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!claim_shape(row)) {
            plan_error(errors, "claims", "invalid_claims", "each claim is {id, kind: new} or {id, kind: retry, of: <earlier requirement>}"); return false;
        }
        if (plan_index(requirements, f_string(row, "id")) < 0) {
            plan_error(errors, "claims", "unclaimed_requirement", "every claim names a requirement of the expansion and every requirement is claimed exactly once"); return false;
        }
    }
    if (!json_object_is_type(requirements, json_type_array) || json_object_array_length(requirements) != json_object_array_length(rows)) {
        plan_error(errors, "claims", "unclaimed_requirement", "every claim names a requirement of the expansion and every requirement is claimed exactly once"); return false;
    }
    return true;
}
static bool dependencies(json_object *proposal, json_object *errors) {
    const char *const keys[] = {"input", "deliverable", NULL};
    json_object *rows = f_field(proposal, "dependencies");
    if (!plan_list(rows, 0, PLAN_ASSETS) || !unique_ids(rows, "input") || !unique_ids(rows, "deliverable")) {
        plan_error(errors, "dependencies", "invalid_dependencies", "dependencies lists at most 16 records with unique input names and unique deliverables"); return false;
    }
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        if (task_keys(json_object_array_get_idx(rows, i), keys)) continue;
        plan_error(errors, "dependencies", "invalid_dependencies", "each dependency is {input, deliverable} naming a sealed, passing deliverable of an earlier version"); return false;
    }
    return true;
}
static bool joins(json_object *proposal, json_object *errors) {
    json_object *rows = f_field(proposal, "joins"), *steps = f_field(proposal, "steps");
    if (!plan_list(rows, 1, PLAN_STEPS)) goto invalid;
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        const char *id = f_text(json_object_array_get_idx(rows, i));
        if (!plan_id(id) || plan_index(steps, id) < 0) goto invalid;
        for (size_t j = 0; j < i; j++) if (!strcmp(id, f_text(json_object_array_get_idx(rows, j)))) goto invalid;
    }
    return true;
invalid:
    plan_error(errors, "joins", "invalid_joins", "joins lists 1 to 64 distinct step IDs of the expansion whose membership is frozen at admission"); return false;
}
static bool envelope_shape(json_object *proposal, json_object *errors) {
    json_object *env = f_field(proposal, "envelope");
    if (!env || json_object_is_type(env, json_type_object)) return true;
    plan_error(errors, "envelope", "invalid_envelope", "an expansion envelope is an object that only shrinks the base plan's envelope"); return false;
}
static bool document_shape(json_object *proposal) {
    const char *const keys[] = {"schema_version", "expansion", "base_sha256", "claims", "dependencies", "joins", "steps",
                                "deliverables", "requirements", "checks", "obligations", "data", "envelope", NULL};
    const char *const data_keys[] = {"steps", NULL};
    const char *kind = f_string(proposal, "expansion");
    return task_keys(proposal, keys) && f_number_is(proposal, "schema_version", 1) && kind && !strcmp(kind, PLAN_EXPANSION) &&
        task_hex(f_string(proposal, "base_sha256"), 64) && f_field(proposal, "claims") && f_field(proposal, "dependencies") &&
        f_field(proposal, "joins") && f_field(proposal, "steps") && f_field(proposal, "deliverables") && f_field(proposal, "requirements") &&
        f_field(proposal, "checks") && task_keys(f_field(proposal, "data"), data_keys) &&
        json_object_is_type(f_field(f_field(proposal, "data"), "steps"), json_type_object);
}
bool px_proposal_validate(json_object *proposal, json_object *errors) {
    if (!document_shape(proposal)) {
        plan_error(errors, "$", "invalid_expansion", "expected the closed hydra-plan-expansion-1 document: schema_version 1, expansion, base_sha256, claims, dependencies, joins, steps, deliverables, requirements, checks, data.steps and optional obligations and envelope");
        return false;
    }
    if (!plan_list(f_field(proposal, "steps"), 1, PLAN_STEPS)) {
        plan_error(errors, "steps", "empty_expansion", "an expansion adds 1 to 64 new steps; an empty expansion changes nothing and is refused");
        return false;
    }
    return claims(proposal, errors) & dependencies(proposal, errors) & joins(proposal, errors) & envelope_shape(proposal, errors);
}
