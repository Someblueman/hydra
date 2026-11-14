#include "fleet/plan/plan_expansion.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>

/* Admission lowers a classified proposal into an ordinary schema 1 plan whose
 * inputs are the exact sealed bytes of its dependencies, compiles it with the
 * one compiler under the base run's policy, applies the declared bounds over
 * the whole chain and records version N+1. Every refusal is typed and whole:
 * nothing is written unless every check passed, and expansion.json is written
 * last so an interrupted admission leaves no admitted round. */

static json_object *details(const char *key, const char *value) {
    json_object *out = json_object_new_object(); f_string_add(out, key, value); return out;
}
static json_object *bound_exceeded(const char *bound, int64_t used, int64_t max) {
    json_object *out = details("bound", bound);
    json_object_object_add(out, "used", json_object_new_int64(used)); json_object_object_add(out, "max", json_object_new_int64(max));
    return px_refuse("expansion_bound_exceeded", "admitting the proposal would exceed a declared expansion bound; nothing is admitted",
                     "the policy's expansion bounds are not enlarged by a proposal; propose less work or accept the recorded outcome", out);
}
/* Scopes and budgets only shrink: sets are subsets of the base plan envelope,
 * a write scope may additionally name a head the expansion itself spawns. */
static const char *enlarged(json_object *base, json_object *env, json_object *steps) {
    const char *sets[] = {"hosts", "tools", "effects", "writes"};
    const char *smaller[] = {"parallelism", "timeout_seconds", "artifact_bytes", "max_heads", "retry_budget", "repair_budget"};
    for (size_t i = 0; i < 4; i++) {
        json_object *set = f_field(env, sets[i]);
        for (size_t j = 0; json_object_is_type(set, json_type_array) && j < json_object_array_length(set); j++) {
            const char *s = f_text(json_object_array_get_idx(set, j));
            bool allowed = i == 3 ? plan_scope_permitted(f_field(base, "writes"), s) || plan_scope_spawned(steps, s) : plan_has(f_field(base, sets[i]), s);
            if (!allowed) return sets[i];
        }
    }
    for (size_t i = 0; i < 6; i++)
        if (json_object_get_int64(f_field(env, smaller[i])) > json_object_get_int64(f_field(base, smaller[i]))) return smaller[i];
    if (json_object_get_int64(f_field(env, "disk_mb")) < json_object_get_int64(f_field(base, "disk_mb"))) return "disk_mb";
    return NULL;
}
static json_object *lowered_plan(const struct px_chain *chain, json_object *proposal, int round, json_object *inputs) {
    json_object *base = f_field(chain->versions[0].compiled, "plan"), *plan = json_object_new_object(), *data = json_object_new_object();
    const char *copied[] = {"steps", "deliverables", "requirements", "checks", "obligations"}; char id[80];
    if (snprintf(id, sizeof(id), "%s-r%d", f_string(base, "id"), round) >= (int)sizeof(id) || !plan_id(id)) { json_object_put(plan); json_object_put(data); return NULL; }
    json_object_object_add(plan, "schema_version", json_object_new_int(1));
    f_string_add(plan, "id", id); f_string_add(plan, "objective", f_string(base, "objective"));
    json_object_object_add(plan, "context", json_object_new_array());
    json_object_object_add(plan, "assumptions", plan_canonical(f_field(base, "assumptions")));
    json_object_object_add(plan, "questions", json_object_new_array());
    for (size_t i = 0; i < 5; i++) if (f_field(proposal, copied[i])) json_object_object_add(plan, copied[i], plan_canonical(f_field(proposal, copied[i])));
    json_object_object_add(data, "schema_version", json_object_new_int(1));
    json_object_object_add(data, "inputs", json_object_get(inputs));
    json_object_object_add(data, "steps", plan_canonical(f_field(f_field(proposal, "data"), "steps")));
    json_object_object_add(plan, "data", data);
    json_object_object_add(plan, "envelope", plan_canonical(f_field(proposal, "envelope") ? f_field(proposal, "envelope") : f_field(base, "envelope")));
    return plan;
}
static void reachability(json_object *steps, bool reach[PLAN_STEPS][PLAN_STEPS]) {
    size_t n = json_object_array_length(steps);
    for (size_t i = 0; i < n; i++) {
        json_object *needs = f_field(json_object_array_get_idx(steps, i), "needs");
        for (size_t j = 0; json_object_is_type(needs, json_type_array) && j < json_object_array_length(needs); j++) {
            int index = plan_index(steps, f_text(json_object_array_get_idx(needs, j)));
            if (index >= 0) reach[i][index] = true;
        }
    }
    for (size_t k = 0; k < n; k++) for (size_t i = 0; i < n; i++) for (size_t j = 0; j < n; j++) reach[i][j] = reach[i][j] || (reach[i][k] && reach[k][j]);
}
/* Every join has members and every other node feeds or follows a join, so
 * the admitted work converges into the membership recorded here. */
static json_object *frozen_joins(json_object *proposal) {
    json_object *steps = f_field(proposal, "steps"), *joins = f_field(proposal, "joins"), *out = json_object_new_object();
    bool reach[PLAN_STEPS][PLAN_STEPS] = {{false}}, is_join[PLAN_STEPS] = {false}; size_t n = json_object_array_length(steps);
    if (n > PLAN_STEPS) goto invalid;
    reachability(steps, reach);
    for (size_t j = 0; j < json_object_array_length(joins); j++) {
        const char *id = f_text(json_object_array_get_idx(joins, j)); int index = plan_index(steps, id);
        json_object *needs = f_field(json_object_array_get_idx(steps, (size_t)index), "needs");
        if (!json_object_is_type(needs, json_type_array) || !json_object_array_length(needs)) goto invalid;
        is_join[index] = true; json_object_object_add(out, id, plan_canonical(needs));
    }
    for (size_t i = 0; i < n; i++) {
        bool connected = is_join[i];
        for (size_t j = 0; !connected && j < n; j++) connected = is_join[j] && (reach[i][j] || reach[j][i]);
        if (!connected) goto invalid;
    }
    return out;
invalid:
    json_object_put(out); return NULL;
}
static json_object *usage(int64_t used, int64_t max) {
    json_object *out = json_object_new_object();
    json_object_object_add(out, "used", json_object_new_int64(used)); json_object_object_add(out, "max", json_object_new_int64(max)); return out;
}
/* Totals over every version including the one being admitted. */
static json_object *bounds(const struct px_chain *chain, json_object *lowered, int round, json_object *claims, json_object **refusal) {
    json_object *policy = f_field(f_field(chain->versions[0].compiled, "policy"), "expansion"), *out = json_object_new_object(), *repairs = json_object_new_object();
    const char *names[] = {"max_nodes", "max_work_seconds", "max_artifact_bytes", "max_rounds"}; int64_t used[4] = {0, 0, 0, round};
    for (size_t v = 0; v < chain->count; v++) {
        json_object *plan = f_field(chain->versions[v].compiled, "plan");
        used[0] += (int64_t)json_object_array_length(f_field(plan, "steps")); used[1] += px_work_seconds(plan);
        used[2] += json_object_get_int64(f_field(f_field(plan, "envelope"), "artifact_bytes"));
    }
    used[0] += (int64_t)json_object_array_length(f_field(lowered, "steps")); used[1] += px_work_seconds(lowered);
    used[2] += json_object_get_int64(f_field(f_field(lowered, "envelope"), "artifact_bytes"));
    for (size_t i = 0; i < 4; i++) {
        int64_t max = json_object_get_int64(f_field(policy, names[i]));
        if (used[i] > max && !*refusal) *refusal = bound_exceeded(names[i], used[i], max);
        json_object_object_add(out, names[i], usage(used[i], max));
    }
    for (size_t i = 0; i < json_object_array_length(claims); i++) {
        json_object *claim = json_object_array_get_idx(claims, i);
        if (f_string(claim, "of")) json_object_object_add(repairs, f_string(claim, "of"), usage(json_object_get_int64(f_field(claim, "repair_attempt")), json_object_get_int64(f_field(policy, "max_repairs_per_claim"))));
    }
    json_object_object_add(out, "max_repairs_per_claim", repairs);
    return out;
}
static json_object *compile_round(const struct px_chain *chain, json_object *lowered, const char *source, json_object *assets, json_object **compiled) {
    json_object *errors = json_object_new_array(), *data, *result;
    *compiled = plan_compile(lowered, f_field(chain->versions[0].compiled, "policy"), source, assets, errors);
    if (*compiled && json_object_equal(f_field(*compiled, "source"), f_field(chain->versions[0].compiled, "source"))) { json_object_put(errors); return NULL; }
    if (*compiled) {
        json_object_put(*compiled); *compiled = NULL; json_object_put(errors);
        return px_refuse("source_changed", "the checkout is not at the base run's bound source; an expansion extends the same source, a changed source needs a new plan", "compile and accept a new plan for the changed source, or restore the base commit", NULL);
    }
    data = json_object_new_object(); json_object_object_add(data, "diagnostics", errors);
    result = px_refuse("invalid_plan", "the lowered expansion did not compile under the base policy; see data.diagnostics", "revise the indicated fields without weakening the base objective or policy, then expand again", data);
    return result;
}
static json_object *record_data(const struct px_chain *chain, int round, long recorded_at, json_object *claims, json_object *dependencies, json_object *joins, json_object *bound) {
    const struct px_version *previous = &chain->versions[chain->count - 1]; json_object *out = json_object_new_object(); char digest[65], path[F_PATH];
    json_object_object_add(out, "schema_version", json_object_new_int(1)); f_string_add(out, "record", PLAN_EXPANSION_RECORD);
    json_object_object_add(out, "round", json_object_new_int(round)); json_object_object_add(out, "recorded_at", json_object_new_int64(recorded_at));
    f_string_add(out, "compiler", PLAN_COMPILER); f_string_add(out, "base_run_id", chain->base_run_id);
    f_string_add(out, "base_sha256", chain->versions[0].sha256); f_string_add(out, "previous_sha256", previous->sha256);
    if (previous->round == 0 || f_path(path, sizeof(path), previous->directory, "expansion.json") || f_hash(path, digest)) json_object_object_add(out, "previous_record_sha256", NULL);
    else f_string_add(out, "previous_record_sha256", digest);
    if (plan_digest(f_field(chain->versions[0].compiled, "policy"), digest)) { json_object_put(out); return NULL; }
    f_string_add(out, "policy_sha256", digest);
    json_object_object_add(out, "claims", json_object_get(claims)); json_object_object_add(out, "dependencies", json_object_get(dependencies));
    json_object_object_add(out, "joins", json_object_get(joins)); json_object_object_add(out, "bounds", json_object_get(bound));
    f_string_add(out, "limits", "The sealed bytes of each dependency travel inside the compiled artifact and are bound by its digest. Admission grants no approval: execution needs hydra workflow plan run with this exact digest. Joins keep the recorded membership; nothing joins or leaves at run time.");
    return out;
}
static int write_canonical(const char *directory, const char *name, json_object *value) {
    char path[F_PATH]; json_object *canonical = plan_canonical(value); const char *text; int status = -1;
    text = json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN);
    if (strlen(text) <= PLAN_LIMIT && !f_path(path, sizeof(path), directory, name)) status = f_write(path, text, strlen(text), false);
    json_object_put(canonical); return status;
}
/* proposal.json and compiled.json first, the record that admits them last. */
static json_object *write_round(const char *directory, json_object *proposal, json_object *compiled, json_object *record, char proposal_sha[65]) {
    char path[F_PATH];
    if (write_canonical(directory, "proposal.json", proposal) || f_path(path, sizeof(path), directory, "proposal.json") || f_hash(path, proposal_sha) ||
        write_canonical(directory, "compiled.json", compiled))
        return px_refuse("record_write_failed", "could not write the round into a new private directory", "check the planning directory and retry the admission", NULL);
    f_string_add(record, "proposal_sha256", proposal_sha);
    if (write_canonical(directory, "expansion.json", record))
        return px_refuse("record_write_failed", "could not write the expansion record", "check the planning directory and retry the admission", NULL);
    return NULL;
}
static json_object *admitted(const char *expansions, int round, json_object *record, json_object *compiled) {
    json_object *data = json_object_new_object(); char path[F_PATH], name[32]; const char *sha = f_string(record, "compiled_sha256");
    snprintf(name, sizeof(name), "round-%d/compiled.json", round);
    json_object_object_add(data, "round", json_object_new_int(round));
    f_string_add(data, "compiled_sha256", sha); f_string_add(data, "proposal_sha256", f_string(record, "proposal_sha256"));
    if (!f_path(path, sizeof(path), expansions, name)) f_string_add(data, "compiled", path);
    json_object_object_add(data, "record", json_object_get(record));
    json_object_object_add(data, "resolved", json_object_get(compiled));
    f_string_add(data, "acceptance", "run requires hydra workflow plan run <compiled> --accept with this exact digest; a changed round invalidates it");
    return f_success("workflow plan expand", data);
}
struct admission { json_object *claims, *dependencies, *assets, *inputs, *lowered, *joins, *bound, *compiled, *record; };
static void admission_free(struct admission *a) {
    json_object_put(a->claims); json_object_put(a->dependencies); json_object_put(a->assets); json_object_put(a->inputs);
    json_object_put(a->lowered); json_object_put(a->joins); json_object_put(a->bound); json_object_put(a->compiled); json_object_put(a->record);
}
static json_object *prepare(const struct px_chain *chain, json_object *proposal, int round, struct admission *a) {
    json_object *refusal = NULL; const char *path;
    if ((refusal = px_classify_claims(chain, proposal, &a->claims)) || (refusal = px_resolve_dependencies(chain, proposal, &a->dependencies, &a->assets, &a->inputs))) return refusal;
    if (!(a->lowered = lowered_plan(chain, proposal, round, a->inputs)))
        return px_refuse("invalid_expansion", "the base plan ID leaves no room for a round suffix", "shorten the base plan ID in a new plan", NULL);
    if ((path = enlarged(f_field(f_field(chain->versions[0].compiled, "plan"), "envelope"), f_field(a->lowered, "envelope"), f_field(a->lowered, "steps"))))
        return px_refuse("authority_enlarged", "the expansion envelope exceeds the base plan's envelope; authority never enlarges through an expansion", "remove the enlarged scope or budget, or compile and accept a new plan under a new policy", details("path", path));
    if (!(a->joins = frozen_joins(proposal)))
        return px_refuse("invalid_joins", "every join needs members and every new node must feed or follow a join", "declare the steps where the new branches converge and connect every node to one", NULL);
    a->bound = bounds(chain, a->lowered, round, a->claims, &refusal);
    return refusal;
}
json_object *px_admit(const char *directory, const char *expansions, const struct px_chain *chain, json_object *proposal, const char *source, long recorded_at) {
    struct admission a = {0}; json_object *errors = json_object_new_array(), *refusal = NULL, *data; int round = (int)chain->count; char digest[65], proposal_sha[65];
    if (json_object_array_length(chain->errors)) {
        data = json_object_new_object(); json_object_object_add(data, "diagnostics", json_object_get(chain->errors)); json_object_put(errors);
        return px_refuse("invalid_chain", "the base run or an admitted round no longer validates; see data.diagnostics", "restore the recorded files or start a new plan; nothing is admitted on a broken chain", data);
    }
    if (!px_proposal_validate(proposal, errors)) {
        data = json_object_new_object(); json_object_object_add(data, "diagnostics", errors);
        return px_refuse("invalid_expansion", "the proposal is not a closed hydra-plan-expansion-1 document; see data.diagnostics", "revise the indicated fields and expand again", data);
    }
    json_object_put(errors);
    if (strcmp(f_string(proposal, "base_sha256"), chain->versions[chain->count - 1].sha256))
        return px_refuse("stale_base", "base_sha256 is not the digest of the chain's latest version", "set base_sha256 to the latest compiled digest reported by hydra workflow plan expand <head> --run <run-id> --json", details("latest_sha256", chain->versions[chain->count - 1].sha256));
    if (round > (int)PLAN_EXPANSION_ROUNDS || round > json_object_get_int(f_field(f_field(f_field(chain->versions[0].compiled, "policy"), "expansion"), "max_rounds")))
        return bound_exceeded("max_rounds", round, json_object_get_int64(f_field(f_field(f_field(chain->versions[0].compiled, "policy"), "expansion"), "max_rounds")));
    if (!(refusal = prepare(chain, proposal, round, &a)) && !(refusal = compile_round(chain, a.lowered, source, a.assets, &a.compiled))) {
        if (!(a.record = record_data(chain, round, recorded_at, a.claims, a.dependencies, a.joins, a.bound)) || plan_digest(a.compiled, digest))
            refusal = px_refuse("record_write_failed", "could not build the expansion record", "retry the admission", NULL);
        else { f_string_add(a.record, "compiled_sha256", digest); refusal = write_round(directory, proposal, a.compiled, a.record, proposal_sha); }
    }
    data = refusal ? refusal : admitted(expansions, round, a.record, a.compiled);
    admission_free(&a); return data;
}
