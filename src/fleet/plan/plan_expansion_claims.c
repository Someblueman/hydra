#include "fleet/plan/plan_expansion.h"
#include "fleet/task/task.h"
#include <string.h>

/* A claim is a requirement: its criterion and the acceptance rule (check
 * method and definition) that decides it. A new claim asks a question no
 * earlier version asked; a retry restates an earlier claim exactly and
 * consumes that claim's repair budget. Restating an unchanged claim as new,
 * changing a claim while calling it a retry, retrying verified work and
 * retrying an uncertain outcome are all refused here, before anything is
 * lowered or compiled. */

json_object *px_refuse(const char *code, const char *message, const char *recovery, json_object *details) {
    json_object *result = f_error("workflow plan expand", code, message);
    if (recovery) f_string_add(f_field(result, "error"), "recovery", recovery);
    if (details) json_object_object_add(result, "data", details);
    return result;
}
static json_object *named(const char *key, const char *value) {
    json_object *details = json_object_new_object(); f_string_add(details, key, value); return details;
}
static json_object *record(json_object *compiled, const char *collection, const char *id) {
    json_object *rows = f_field(f_field(compiled, "plan"), collection); int index = plan_index(rows, id);
    return index < 0 ? NULL : json_object_array_get_idx(rows, (size_t)index);
}
static json_object *proposal_record(json_object *proposal, const char *collection, const char *id) {
    json_object *rows = f_field(proposal, collection); int index = plan_index(rows, id);
    return index < 0 ? NULL : json_object_array_get_idx(rows, (size_t)index);
}
static bool same_text(const char *a, const char *b) { return a && b && !strcmp(a, b); }
/* The acceptance rule of a requirement: its check's method and definition. */
static bool same_rule(json_object *check, json_object *other) {
    return check && other && same_text(f_string(check, "method"), f_string(other, "method")) &&
        same_text(f_string(check, "definition"), f_string(other, "definition"));
}
/* An earlier requirement with the same criterion anywhere in the chain. */
static const char *restated(const struct px_chain *chain, const char *criterion) {
    for (size_t v = 0; v < chain->count; v++) {
        json_object *rows = f_field(f_field(chain->versions[v].compiled, "plan"), "requirements");
        for (size_t i = 0; i < json_object_array_length(rows); i++) {
            json_object *r = json_object_array_get_idx(rows, i);
            if (same_text(f_string(r, "criterion"), criterion)) return f_string(r, "id");
        }
    }
    return NULL;
}
/* How a requirement of an admitted round was classified; base requirements are originals. */
static const char *recorded_kind(const struct px_version *version, const char *id) {
    json_object *claims = f_field(version->record, "claims");
    for (size_t i = 0; claims && i < json_object_array_length(claims); i++) {
        json_object *claim = json_object_array_get_idx(claims, i);
        if (same_text(f_string(claim, "id"), id)) return f_string(claim, "kind");
    }
    return "new";
}
static int retries_of(const struct px_chain *chain, const char *original) {
    int count = 0;
    for (size_t v = 1; v < chain->count; v++) {
        json_object *claims = f_field(chain->versions[v].record, "claims");
        for (size_t i = 0; claims && i < json_object_array_length(claims); i++)
            if (same_text(f_string(json_object_array_get_idx(claims, i), "of"), original)) count++;
    }
    return count;
}
static json_object *new_claim(const struct px_chain *chain, json_object *requirement) {
    const char *earlier = restated(chain, f_string(requirement, "criterion"));
    if (!earlier) return NULL;
    return px_refuse("claim_restated", "a new claim restates the criterion of an earlier requirement; an unchanged claim is a retry, not a new question",
                     "declare the claim as {kind: retry, of: <the earlier requirement>} or state a genuinely new criterion", named("earlier_requirement", earlier));
}
static json_object *retry_outcome(const struct px_version *version, json_object *original, const char *of) {
    const char *status = px_deliverable_status(version, f_string(original, "deliverable"), NULL, NULL);
    if (!strcmp(status, "pass"))
        return px_refuse("duplicate_work", "the original claim is verified in its version; retrying it would duplicate verified work",
                         "retry only a claim whose verification failed; a new question needs its own criterion", named("claim", of));
    if (strcmp(status, "fail") && strcmp(status, "invalid"))
        return px_refuse("unknown_outcome", "the original claim's outcome is not a recorded terminal verdict; uncertain work is never duplicated",
                         "finish, resume or inspect the run that holds the original claim before retrying it", named("claim", of));
    return NULL;
}
static json_object *retry_claim(const struct px_chain *chain, json_object *proposal, json_object *requirement, const char *of, int *attempt) {
    const struct px_version *version = px_find(chain, "requirements", of); json_object *original, *check, *earlier;
    int64_t budget = json_object_get_int64(f_field(f_field(f_field(chain->versions[0].compiled, "policy"), "expansion"), "max_repairs_per_claim"));
    if (!version) return px_refuse("unknown_claim", "a retry names a requirement of no earlier version", "name an existing requirement in of", named("claim", of));
    if (strcmp(recorded_kind(version, of), "new"))
        return px_refuse("retry_of_retry", "a retry names the original claim, not an earlier retry of it", "set of to the original requirement that the earlier retry named", named("claim", of));
    original = record(version->compiled, "requirements", of);
    check = proposal_record(proposal, "checks", f_string(requirement, "check"));
    earlier = record(version->compiled, "checks", f_string(original, "check"));
    if (!same_text(f_string(requirement, "criterion"), f_string(original, "criterion")) || !same_rule(check, earlier))
        return px_refuse("claim_changed", "a retry restates the original criterion and acceptance rule exactly; a changed criterion or acceptance rule is a new question",
                         "restate the original claim unchanged, or declare a new claim with its own criterion", named("claim", of));
    *attempt = retries_of(chain, of) + 1;
    if (*attempt > budget) {
        json_object *details = named("bound", "max_repairs_per_claim"); f_string_add(details, "claim", of);
        json_object_object_add(details, "used", json_object_new_int(*attempt)); json_object_object_add(details, "max", json_object_new_int64(budget));
        return px_refuse("expansion_bound_exceeded", "the repair budget of the original claim is exhausted; nothing is admitted", "the policy's expansion bounds are not enlarged by a proposal; ask a new question or accept the recorded outcome", details);
    }
    return retry_outcome(version, original, of);
}
/* IDs referenced across versions stay unique over the whole chain. */
static json_object *unique_ids(const struct px_chain *chain, json_object *proposal) {
    const char *collections[] = {"deliverables", "requirements", "checks"};
    for (size_t c = 0; c < 3; c++) {
        json_object *rows = f_field(proposal, collections[c]);
        for (size_t i = 0; json_object_is_type(rows, json_type_array) && i < json_object_array_length(rows); i++) {
            const char *id = f_string(json_object_array_get_idx(rows, i), "id");
            if (id && px_find(chain, collections[c], id)) {
                json_object *details = named("collection", collections[c]); f_string_add(details, "id", id);
                return px_refuse("duplicate_id", "deliverable, requirement and check IDs are unique across every version of the chain", "rename the record so later versions can reference it unambiguously", details);
            }
        }
    }
    return NULL;
}
json_object *px_classify_claims(const struct px_chain *chain, json_object *proposal, json_object **rows) {
    json_object *claims = f_field(proposal, "claims"), *result = unique_ids(chain, proposal);
    *rows = json_object_new_array();
    for (size_t i = 0; !result && i < json_object_array_length(claims); i++) {
        json_object *claim = json_object_array_get_idx(claims, i), *out = json_object_new_object();
        json_object *requirement = proposal_record(proposal, "requirements", f_string(claim, "id"));
        const char *kind = f_string(claim, "kind"); int attempt = 0;
        f_string_add(out, "id", f_string(claim, "id")); f_string_add(out, "kind", kind);
        if (!strcmp(kind, "new")) { result = new_claim(chain, requirement); json_object_object_add(out, "of", NULL); }
        else {
            result = retry_claim(chain, proposal, requirement, f_string(claim, "of"), &attempt);
            f_string_add(out, "of", f_string(claim, "of")); json_object_object_add(out, "repair_attempt", json_object_new_int(attempt));
        }
        json_object_array_add(*rows, out);
    }
    if (result) { json_object_put(*rows); *rows = NULL; }
    return result;
}

static json_object *dependency_refusal(const char *status, const char *deliverable) {
    if (!strcmp(status, "unknown_deliverable")) return px_refuse("unknown_dependency", "a dependency names no deliverable of an earlier version", "depend only on declared deliverables of the base run or an admitted round", named("deliverable", deliverable));
    if (!strcmp(status, "unsupported_artifact")) return px_refuse("unsupported_artifact", "the sealed deliverable cannot travel as a bundle input: it must be UTF-8 text of at most 64 KiB without NUL bytes", "produce a bounded text artifact or consume a smaller deliverable", named("deliverable", deliverable));
    if (!strcmp(status, "fail") || !strcmp(status, "invalid")) return px_refuse("dependency_failed", "the deliverable's verification did not pass in its version; failed evidence is not an input", "retry the claim that owns the deliverable, or depend on a verified deliverable", named("deliverable", deliverable));
    if (strcmp(status, "pass")) return px_refuse("unknown_outcome", "the deliverable's outcome is not a recorded terminal verdict; dependents are blocked until it is", "finish, resume or inspect the run that produces the deliverable", named("deliverable", deliverable));
    return NULL;
}
static json_object *dependency_row(const struct px_version *version, json_object *dependency, const char *effects) {
    json_object *d = record(version->compiled, "deliverables", f_string(dependency, "deliverable")), *file, *out = json_object_new_object(); char path[F_PATH];
    if (!(file = plan_artifact(version->compiled, version->run, f_string(d, "step"), f_string(d, "output"), path))) { json_object_put(out); return NULL; }
    f_string_add(out, "input", f_string(dependency, "input")); f_string_add(out, "deliverable", f_string(dependency, "deliverable"));
    json_object_object_add(out, "version", json_object_new_int(version->round));
    f_string_add(out, "sha256", f_string(file, "sha256")); json_object_object_add(out, "bytes", json_object_get(f_field(file, "bytes")));
    f_string_add(out, "effects", effects);
    json_object_put(file); return out;
}
json_object *px_resolve_dependencies(const struct px_chain *chain, json_object *proposal, json_object **rows, json_object **assets, json_object **inputs) {
    json_object *dependencies = f_field(proposal, "dependencies"), *result = NULL;
    *rows = json_object_new_array(); *assets = json_object_new_object(); *inputs = json_object_new_object();
    for (size_t i = 0; !result && i < json_object_array_length(dependencies); i++) {
        json_object *dependency = json_object_array_get_idx(dependencies, i), *text = NULL, *declaration = NULL, *input, *row;
        const char *id = f_string(dependency, "deliverable"); const struct px_version *version = px_find(chain, "deliverables", id);
        const char *status = version ? px_deliverable_status(version, id, &text, &declaration) : "unknown_deliverable";
        if ((result = dependency_refusal(status, id))) { json_object_put(text); break; }
        if (!(row = dependency_row(version, dependency, px_deliverable_effects(f_field(version->compiled, "plan"), id)))) {
            json_object_put(text); result = px_refuse("unknown_outcome", "the sealed receipt of the deliverable could not be read", "inspect the run record", named("deliverable", id)); break;
        }
        json_object_array_add(*rows, row);
        json_object_object_add(*assets, f_string(dependency, "input"), text);
        input = json_object_new_object(); f_string_add(input, "asset", f_string(dependency, "input"));
        f_string_add(input, "type", f_string(declaration, "type")); json_object_object_add(input, "max_bytes", json_object_get(f_field(declaration, "max_bytes")));
        json_object_object_add(*inputs, f_string(dependency, "input"), input);
    }
    if (result) { json_object_put(*rows); json_object_put(*assets); json_object_put(*inputs); *rows = *assets = *inputs = NULL; }
    return result;
}
