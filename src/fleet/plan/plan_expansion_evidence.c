#include "fleet/plan/plan_expansion.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Evidence of earlier versions is read from their durable run records and
 * never cached as authoritative: every status is recomputed from the sealed
 * receipts, the verify reports and the recorded step and run states. */

static char *scalar(const char *directory, const char *name) {
    char path[F_PATH], *text;
    if (f_path(path, sizeof(path), directory, name) || !(text = f_read(path, 128))) return NULL;
    text[strcspn(text, "\r\n")] = '\0'; return text;
}
bool px_terminal(const char *state) {
    return state && (!strcmp(state, "succeeded") || !strcmp(state, "failed") || !strcmp(state, "cancelled"));
}
/* A recorded terminal state is reported as such; anything else is unknown. */
static const char *run_state(const char *run) {
    char *state = scalar(run, "state"); const char *out = "unknown";
    if (!strcmp(state ? state : "", "succeeded")) out = "succeeded";
    else if (!strcmp(state ? state : "", "failed")) out = "failed";
    else if (!strcmp(state ? state : "", "cancelled")) out = "cancelled";
    free(state); return out;
}
static void chain_error(struct px_chain *chain, int round, const char *code, const char *message) {
    char path[32]; snprintf(path, sizeof(path), "round-%d", round);
    plan_error(chain->errors, path, code, message);
}
/* A round's run is bound by its recorded ID and must carry the admitted
 * artifact and record; a link to a run that was never published means that
 * no work started. */
static void load_run(struct px_chain *chain, struct px_version *v, const char *runs) {
    char *id = scalar(v->directory, "run-id"); json_object *compiled, *record = NULL; char digest[65], path[F_PATH];
    v->run_state = "not_executed";
    if (!id) return;
    if (strlen(id) >= sizeof(v->run_id) || !f_name(id) || f_path(v->run, sizeof(v->run), runs, id)) {
        v->run[0] = '\0'; free(id); chain_error(chain, v->round, "run_mismatch", "the recorded run ID is invalid"); return;
    }
    memcpy(v->run_id, id, strlen(id) + 1); free(id);
    if (access(v->run, F_OK)) { v->run[0] = '\0'; v->run_state = "absent"; return; }
    compiled = plan_run_definition(v->run);
    if (!f_path(path, sizeof(path), v->run, "plan-expansion.json")) record = plan_read(path);
    if (!compiled || plan_digest(compiled, digest) || strcmp(digest, v->sha256) || !record || !json_object_equal(record, v->record))
        chain_error(chain, v->round, "run_mismatch", "the round's run does not carry its admitted, accepted artifact and record");
    v->run_state = run_state(v->run);
    json_object_put(compiled); json_object_put(record);
}
static bool record_links(const struct px_chain *chain, json_object *record, int round, const char *previous, const char *previous_record) {
    const char *name = f_string(record, "record"), *base = f_string(record, "base_sha256"), *link = f_string(record, "previous_sha256");
    json_object *record_link = NULL; bool present = json_object_object_get_ex(record, "previous_record_sha256", &record_link);
    if (!name || strcmp(name, PLAN_EXPANSION_RECORD) || !f_number_is(record, "schema_version", 1) || !f_number_is(record, "round", round) ||
        !base || strcmp(base, chain->versions[0].sha256) || !link || strcmp(link, previous) ||
        !task_hex(f_string(record, "compiled_sha256"), 64) || !task_hex(f_string(record, "proposal_sha256"), 64)) return false;
    if (round == 1) return present && json_object_is_type(record_link, json_type_null);
    return f_text(record_link) && !strcmp(f_text(record_link), previous_record);
}
/* The admitted artifact must still validate, carry the admitted digest and the base policy verbatim. */
static bool compiled_intact(const struct px_chain *chain, struct px_version *v) {
    char path[F_PATH], digest[65]; json_object *errors = json_object_new_array(); bool ok;
    if (f_path(path, sizeof(path), v->directory, "compiled.json") || !(v->compiled = plan_read(path))) { json_object_put(errors); return false; }
    ok = !plan_validate(f_field(v->compiled, "plan"), f_field(v->compiled, "policy"), errors) && !plan_digest(v->compiled, digest) &&
        !strcmp(digest, v->sha256) && json_object_equal(f_field(v->compiled, "policy"), f_field(chain->versions[0].compiled, "policy"));
    json_object_put(errors);
    if (!ok) { json_object_put(v->compiled); v->compiled = NULL; }
    return ok;
}
/* Returns false when no further round exists. */
static bool load_round(struct px_chain *chain, const char *expansions, const char *runs, char previous[65], char previous_record[65]) {
    struct px_version *v = &chain->versions[chain->count]; char name[32], path[F_PATH], digest[65];
    snprintf(name, sizeof(name), "round-%zu", chain->count);
    memset(v, 0, sizeof(*v)); v->round = (int)chain->count; v->run_state = "not_executed";
    if (f_path(v->directory, sizeof(v->directory), expansions, name) || f_path(path, sizeof(path), v->directory, "expansion.json") || access(path, F_OK)) return false;
    chain->count++;
    if (!(v->record = plan_read(path)) || f_hash(path, digest) || !record_links(chain, v->record, v->round, previous, previous_record)) {
        chain_error(chain, v->round, "expansion_changed", "the expansion record is not the next link of the chain"); return false;
    }
    memcpy(v->sha256, f_string(v->record, "compiled_sha256"), 65);
    memcpy(previous, v->sha256, 65); memcpy(previous_record, digest, 65);
    if (!compiled_intact(chain, v)) chain_error(chain, v->round, "expansion_changed", "compiled.json no longer validates against the admitted digest and base policy");
    if (f_path(path, sizeof(path), v->directory, "proposal.json") || f_hash(path, digest) || strcmp(digest, f_string(v->record, "proposal_sha256")))
        chain_error(chain, v->round, "expansion_changed", "proposal.json no longer matches the admitted digest");
    load_run(chain, v, runs);
    return true;
}
void px_chain_load(struct px_chain *chain, const char *expansions, const char *base_run, const char *runs) {
    struct px_version *base = &chain->versions[0]; char previous[65] = "", previous_record[65] = ""; char *id;
    memset(chain, 0, sizeof(*chain)); chain->errors = json_object_new_array();
    chain->count = 1; base->round = 0; base->run_state = "unknown";
    if (f_copy(base->run, sizeof(base->run), base_run) || !(base->compiled = plan_run_definition(base_run)) || plan_digest(base->compiled, base->sha256)) {
        chain_error(chain, 0, "invalid_run", "the base run has no accepted compiled plan matching its approval digest"); return;
    }
    id = scalar(base_run, "run-id");
    f_copy(base->run_id, sizeof(base->run_id), id ? id : (strrchr(base_run, '/') ? strrchr(base_run, '/') + 1 : base_run)); free(id);
    memcpy(chain->base_run_id, base->run_id, sizeof(chain->base_run_id));
    base->run_state = run_state(base_run);
    if (!f_number_is(f_field(base->compiled, "plan"), "schema_version", 1))
        chain_error(chain, 0, "unsupported_base", "expansions are qualified for schema 1 local plans only");
    if (!plan_expansion_policy(f_field(base->compiled, "policy"), chain->errors) || !f_field(f_field(base->compiled, "policy"), "expansion"))
        chain_error(chain, 0, "expansion_not_authorized", "the base run's policy declares no expansion bounds; nothing authorizes an expansion");
    memcpy(previous, base->sha256, 65);
    while (chain->count <= PLAN_EXPANSION_ROUNDS && load_round(chain, expansions, runs, previous, previous_record)) {}
}
void px_chain_free(struct px_chain *chain) {
    for (size_t i = 0; i < chain->count; i++) { json_object_put(chain->versions[i].compiled); json_object_put(chain->versions[i].record); }
    json_object_put(chain->errors); memset(chain, 0, sizeof(*chain));
}

static json_object *row(json_object *compiled, const char *collection, const char *id) {
    json_object *rows = f_field(f_field(compiled, "plan"), collection); int index = plan_index(rows, id);
    return index < 0 ? NULL : json_object_array_get_idx(rows, (size_t)index);
}
const struct px_version *px_find(const struct px_chain *chain, const char *collection, const char *id) {
    for (size_t i = chain->count; i-- > 0;) if (row(chain->versions[i].compiled, collection, id)) return &chain->versions[i];
    return NULL;
}
static char *step_state(const char *run, const char *step) {
    char directory[F_PATH];
    if (snprintf(directory, sizeof(directory), "%s/steps/%s", run, step) >= (int)sizeof(directory)) return NULL;
    return scalar(directory, "state");
}
/* Bounded UTF-8 text of the sealed artifact, or NULL when it cannot travel as a bundle input. */
static json_object *sealed_text(const char *path) {
    char *bytes = f_read(path, PLAN_ASSET_LIMIT); json_object *text = NULL;
    if (bytes && plan_utf8(bytes, strlen(bytes))) text = json_object_new_string(bytes);
    free(bytes); return text;
}
/* Worst status wins: unknown over fail over invalid over pass. */
static const char *worse(const char *a, const char *b) {
    const char *order[] = {"unknown", "fail", "invalid", "pass"};
    for (size_t i = 0; i < 4; i++) if (!strcmp(a, order[i]) || !strcmp(b, order[i])) return order[i];
    return a;
}
/* In a terminal run a step that did not succeed will never produce anything:
 * that is a recorded failure, not uncertainty. */
static const char *step_outcome(const char *run, const char *step) {
    char *state = step_state(run, step); const char *out = "fail";
    if (state && !strcmp(state, "succeeded")) out = "pass";
    else if (state && !strcmp(state, "recovery-required")) out = "unknown";
    free(state); return out;
}
static const char *checks_status(const struct px_version *version, json_object *checks, const char *deliverable) {
    const char *status = "pass";
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i); const char *outcome;
        if (strcmp(f_string(check, "deliverable"), deliverable)) continue;
        outcome = step_outcome(version->run, f_string(check, "step"));
        if (!strcmp(outcome, "pass")) switch (plan_check_result(version->compiled, version->run, check)) {
            case PLAN_PASS: break;
            case PLAN_FAIL: case PLAN_INCONCLUSIVE: outcome = "fail"; break;
            default: outcome = "invalid"; break;
        }
        status = worse(status, outcome);
    }
    return status;
}
const char *px_deliverable_status(const struct px_version *version, const char *deliverable, json_object **text, json_object **declaration) {
    json_object *compiled = version->compiled, *d = row(compiled, "deliverables", deliverable), *file = NULL; char path[F_PATH];
    const char *status, *producer;
    if (text) *text = NULL;
    if (declaration) *declaration = NULL;
    if (!d) return "unknown_deliverable";
    if (!version->run[0]) return "not_executed";
    if (!px_terminal(version->run_state)) return "unknown";
    producer = f_string(d, "step");
    status = step_outcome(version->run, producer);
    if (!strcmp(status, "pass") && !(file = plan_artifact(compiled, version->run, producer, f_string(d, "output"), path))) status = "invalid";
    else if (!strcmp(status, "pass")) json_object_put(file);
    status = worse(status, checks_status(version, f_field(f_field(compiled, "plan"), "checks"), deliverable));
    if (strcmp(status, "pass") || !text) return status;
    if (!(*text = sealed_text(path))) return "unsupported_artifact";
    if (declaration) *declaration = f_field(f_field(f_field(f_field(f_field(compiled, "data"), "steps"), producer), "outputs"), f_string(d, "output"));
    return status;
}
/* Predecessor closure of one step over `needs`, marked in visited. */
static void predecessors(json_object *steps, int index, bool visited[PLAN_STEPS]) {
    json_object *needs = f_field(json_object_array_get_idx(steps, (size_t)index), "needs");
    if (visited[index]) return;
    visited[index] = true;
    for (size_t i = 0; i < json_object_array_length(needs); i++) {
        int next = plan_index(steps, f_text(json_object_array_get_idx(needs, i)));
        if (next >= 0) predecessors(steps, next, visited);
    }
}
const char *px_deliverable_effects(json_object *plan, const char *deliverable) {
    json_object *steps = f_field(plan, "steps"), *rows = f_field(plan, "deliverables"); bool visited[PLAN_STEPS] = {false};
    int d = plan_index(rows, deliverable), producer = d < 0 ? -1 : plan_index(steps, f_string(json_object_array_get_idx(rows, (size_t)d), "step"));
    const char *effects = "artifact_only";
    if (producer < 0) return "unknown";
    predecessors(steps, producer, visited);
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (!visited[i]) continue;
        if (f_string(f_field(step, "args"), "profile")) return "agent";
        if (json_object_array_length(f_field(step, "writes"))) effects = "declared_writes";
    }
    return effects;
}
int64_t px_work_seconds(json_object *plan) {
    json_object *steps = f_field(plan, "steps"); int64_t seconds = 0;
    for (size_t i = 0; i < json_object_array_length(steps); i++)
        seconds += json_object_get_int64(f_field(f_field(json_object_array_get_idx(steps, i), "args"), "timeout"));
    return seconds;
}
