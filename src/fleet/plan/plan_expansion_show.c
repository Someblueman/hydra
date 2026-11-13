#include "fleet/plan/plan_expansion.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Read-only views of a chain and the execution gate of one round. Statuses
 * are recomputed from the run records on every call, so two calls over
 * unchanged state print identical bytes and a changed artifact shows as such. */

static json_object *record_row(json_object *compiled, const char *collection, const char *id) {
    json_object *rows = f_field(f_field(compiled, "plan"), collection); int index = plan_index(rows, id);
    return index < 0 ? NULL : json_object_array_get_idx(rows, (size_t)index);
}
/* intact when the dependency still passes with the recorded bytes. */
static const char *dependency_status(const struct px_chain *chain, json_object *row) {
    int version = json_object_get_int(f_field(row, "version")); const char *id = f_string(row, "deliverable"), *status;
    const struct px_version *v; json_object *d, *file; char path[F_PATH]; bool same;
    if (version < 0 || (size_t)version >= chain->count || !chain->versions[version].compiled) return "unknown";
    v = &chain->versions[version];
    status = px_deliverable_status(v, id, NULL, NULL);
    if (strcmp(status, "pass")) return status;
    d = record_row(v->compiled, "deliverables", id);
    if (!(file = plan_artifact(v->compiled, v->run, f_string(d, "step"), f_string(d, "output"), path))) return "unknown";
    same = f_string(file, "sha256") && f_string(row, "sha256") && !strcmp(f_string(file, "sha256"), f_string(row, "sha256"));
    json_object_put(file); return same ? "intact" : "changed";
}
static json_object *claim_rows(const struct px_version *v) {
    json_object *out = json_object_new_array(), *requirements = f_field(f_field(v->compiled, "plan"), "requirements"), *recorded = f_field(v->record, "claims");
    for (size_t i = 0; json_object_is_type(requirements, json_type_array) && i < json_object_array_length(requirements); i++) {
        json_object *r = json_object_array_get_idx(requirements, i), *row = json_object_new_object(), *claim = NULL;
        const char *id = f_string(r, "id");
        for (size_t j = 0; recorded && j < json_object_array_length(recorded); j++)
            if (!strcmp(f_string(json_object_array_get_idx(recorded, j), "id"), id)) claim = json_object_array_get_idx(recorded, j);
        f_string_add(row, "id", id); f_string_add(row, "kind", claim ? f_string(claim, "kind") : "new");
        if (claim && f_string(claim, "of")) { f_string_add(row, "of", f_string(claim, "of")); json_object_object_add(row, "repair_attempt", json_object_get(f_field(claim, "repair_attempt"))); }
        else json_object_object_add(row, "of", NULL);
        f_string_add(row, "deliverable", f_string(r, "deliverable"));
        f_string_add(row, "status", px_deliverable_status(v, f_string(r, "deliverable"), NULL, NULL));
        json_object_array_add(out, row);
    }
    return out;
}
static json_object *dependency_rows(const struct px_chain *chain, const struct px_version *v) {
    json_object *out = json_object_new_array(), *recorded = f_field(v->record, "dependencies");
    for (size_t i = 0; recorded && i < json_object_array_length(recorded); i++) {
        json_object *row = plan_canonical(json_object_array_get_idx(recorded, i));
        f_string_add(row, "status", dependency_status(chain, row));
        json_object_array_add(out, row);
    }
    return out;
}
static json_object *version_row(const struct px_chain *chain, const struct px_version *v) {
    json_object *out = json_object_new_object(); char path[F_PATH];
    json_object_object_add(out, "round", json_object_new_int(v->round));
    f_string_add(out, "compiled_sha256", v->sha256);
    if (v->run_id[0]) f_string_add(out, "run_id", v->run_id); else json_object_object_add(out, "run_id", NULL);
    f_string_add(out, "run_state", v->run_state);
    if (v->round) {
        if (!f_path(path, sizeof(path), v->directory, "compiled.json")) f_string_add(out, "compiled", path);
        json_object_object_add(out, "recorded_at", json_object_get(f_field(v->record, "recorded_at")));
        json_object_object_add(out, "joins", json_object_get(f_field(v->record, "joins")));
    }
    json_object_object_add(out, "claims", v->compiled ? claim_rows(v) : json_object_new_array());
    json_object_object_add(out, "dependencies", v->compiled ? dependency_rows(chain, v) : json_object_new_array());
    return out;
}
static json_object *usage(int64_t used, int64_t max) {
    json_object *out = json_object_new_object();
    json_object_object_add(out, "used", json_object_new_int64(used)); json_object_object_add(out, "max", json_object_new_int64(max)); return out;
}
static json_object *chain_bounds(const struct px_chain *chain) {
    json_object *policy = f_field(f_field(chain->versions[0].compiled, "policy"), "expansion"), *out = json_object_new_object();
    int64_t nodes = 0, work = 0, bytes = 0;
    for (size_t v = 0; v < chain->count; v++) {
        json_object *plan = f_field(chain->versions[v].compiled, "plan");
        if (!plan) continue;
        nodes += (int64_t)json_object_array_length(f_field(plan, "steps")); work += px_work_seconds(plan);
        bytes += json_object_get_int64(f_field(f_field(plan, "envelope"), "artifact_bytes"));
    }
    json_object_object_add(out, "max_nodes", usage(nodes, json_object_get_int64(f_field(policy, "max_nodes"))));
    json_object_object_add(out, "max_work_seconds", usage(work, json_object_get_int64(f_field(policy, "max_work_seconds"))));
    json_object_object_add(out, "max_artifact_bytes", usage(bytes, json_object_get_int64(f_field(policy, "max_artifact_bytes"))));
    json_object_object_add(out, "max_rounds", usage((int64_t)chain->count - 1, json_object_get_int64(f_field(policy, "max_rounds"))));
    json_object_object_add(out, "max_repairs_per_claim", usage(0, json_object_get_int64(f_field(policy, "max_repairs_per_claim"))));
    json_object_object_del(f_field(out, "max_repairs_per_claim"), "used");
    return out;
}
json_object *px_show(const struct px_chain *chain) {
    json_object *out = json_object_new_object(), *versions = json_object_new_array();
    json_object_object_add(out, "schema_version", json_object_new_int(1));
    f_string_add(out, "base_run_id", chain->base_run_id);
    f_string_add(out, "base_sha256", chain->versions[0].sha256);
    f_string_add(out, "latest_sha256", chain->versions[chain->count - 1].sha256);
    json_object_object_add(out, "rounds", json_object_new_int64((int64_t)chain->count - 1));
    f_string_add(out, "integrity", json_object_array_length(chain->errors) ? "invalid" : "intact");
    json_object_object_add(out, "diagnostics", json_object_get(chain->errors));
    if (chain->versions[0].compiled) {
        json_object_object_add(out, "policy", json_object_get(f_field(f_field(chain->versions[0].compiled, "policy"), "expansion")));
        json_object_object_add(out, "bounds", chain_bounds(chain));
        for (size_t v = 0; v < chain->count; v++) json_object_array_add(versions, version_row(chain, &chain->versions[v]));
    }
    json_object_object_add(out, "versions", versions);
    f_string_add(out, "limits", "Statuses are recomputed from the run records; pass means sealed bytes with passing checks in a terminal run, unknown means no recorded terminal verdict. Nothing here approves or executes.");
    return out;
}

static void view_claims(json_object *rows) {
    for (size_t i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (f_string(row, "of")) printf("  claim %s (retry of %s, attempt %d): %s\n", f_string(row, "id"), f_string(row, "of"), json_object_get_int(f_field(row, "repair_attempt")), f_string(row, "status"));
        else printf("  claim %s (new): %s\n", f_string(row, "id"), f_string(row, "status"));
    }
}
static void view_version(json_object *row) {
    json_object *dependencies = f_field(row, "dependencies"), *joins = f_field(row, "joins");
    if (!json_object_get_int(f_field(row, "round"))) printf("Version 0 (base run %s, %s): %.12s\n", f_string(row, "run_id") ? f_string(row, "run_id") : "-", f_string(row, "run_state"), f_string(row, "compiled_sha256"));
    else if (f_string(row, "run_id")) printf("Round %d: %.12s, run %s (%s)\n", json_object_get_int(f_field(row, "round")), f_string(row, "compiled_sha256"), f_string(row, "run_id"), f_string(row, "run_state"));
    else printf("Round %d: %.12s, not executed; run: hydra workflow plan run %s --accept %s\n", json_object_get_int(f_field(row, "round")), f_string(row, "compiled_sha256"), f_string(row, "compiled") ? f_string(row, "compiled") : "<compiled.json>", f_string(row, "compiled_sha256"));
    view_claims(f_field(row, "claims"));
    for (size_t i = 0; i < json_object_array_length(dependencies); i++) {
        json_object *d = json_object_array_get_idx(dependencies, i);
        printf("  dependency %s <- %s@v%d (%s)\n", f_string(d, "input"), f_string(d, "deliverable"), json_object_get_int(f_field(d, "version")), f_string(d, "status"));
    }
    if (!joins) return;
    json_object_object_foreach(joins, join, members) {
        printf("  join %s <-", join);
        for (size_t i = 0; i < json_object_array_length(members); i++) printf(" %s", f_text(json_object_array_get_idx(members, i)));
        putchar('\n');
    }
}
static void view(json_object *data) {
    json_object *bounds = f_field(data, "bounds"), *versions = f_field(data, "versions"), *diagnostics = f_field(data, "diagnostics");
    printf("Expansions of run %s: %lld admitted, integrity %s\n", f_string(data, "base_run_id"), (long long)json_object_get_int64(f_field(data, "rounds")), f_string(data, "integrity"));
    if (bounds) {
        const char *names[] = {"max_nodes", "max_work_seconds", "max_artifact_bytes", "max_rounds"};
        printf("Bounds:");
        for (size_t i = 0; i < 4; i++) printf(" %s %lld/%lld", names[i] + 4, (long long)json_object_get_int64(f_field(f_field(bounds, names[i]), "used")), (long long)json_object_get_int64(f_field(f_field(bounds, names[i]), "max")));
        printf(" repairs_per_claim %lld\n", (long long)json_object_get_int64(f_field(f_field(bounds, "max_repairs_per_claim"), "max")));
    }
    for (size_t i = 0; i < json_object_array_length(versions); i++) view_version(json_object_array_get_idx(versions, i));
    for (size_t i = 0; i < json_object_array_length(diagnostics); i++) {
        json_object *d = json_object_array_get_idx(diagnostics, i);
        printf("Diagnostic %s %s: %s\n", f_string(d, "path"), f_string(d, "code"), f_string(d, "message"));
    }
}

static bool dependencies_intact(const struct px_chain *chain, const struct px_version *v) {
    json_object *rows = f_field(v->record, "dependencies");
    for (size_t i = 0; rows && i < json_object_array_length(rows); i++)
        if (strcmp(dependency_status(chain, json_object_array_get_idx(rows, i)), "intact")) return false;
    return true;
}
static bool chain_intact_through(const struct px_chain *chain, int round) {
    for (size_t i = 0; i < json_object_array_length(chain->errors); i++) {
        const char *path = f_string(json_object_array_get_idx(chain->errors, i), "path");
        if (path && !strncmp(path, "round-", 6) && atoi(path + 6) <= round) return false;
    }
    return true;
}
/* The refusal that keeps one admitted round from executing, or NULL. */
static json_object *bind_refusal(const struct px_chain *chain, const struct px_version *v, json_object *record, int number, const char *accepted) {
    json_object *data;
    if (!v || !v->record || !json_object_equal(v->record, record) || !chain_intact_through(chain, number)) {
        data = json_object_new_object(); json_object_object_add(data, "diagnostics", json_object_get(chain->errors));
        return px_refuse("invalid_chain", "the round is not an intact link of its chain; see data.diagnostics", "restore the recorded files or admit a new round; nothing executes on a broken chain", data);
    }
    if (!task_hex(accepted, 64) || strcmp(accepted, v->sha256))
        return px_refuse("acceptance_mismatch", "the accepted digest is not this round's admitted digest; a changed round needs a new admission and approval", "review hydra workflow plan show on the round's compiled.json and accept its exact digest", NULL);
    if (!dependencies_intact(chain, v))
        return px_refuse("dependency_changed", "a sealed dependency of this round no longer passes with its admitted bytes; the admission evidence is invalid", "inspect the run that produced the dependency and admit a new round", NULL);
    if (strcmp(v->run_state, "not_executed") && strcmp(v->run_state, "absent"))
        return px_refuse("round_executed", "this round already has a run; a version executes once and is never replayed", "inspect that run, resume it if it was interrupted, or admit a new round", NULL);
    return NULL;
}
json_object *px_bind(const char *round, const char *accepted, const char *runs) {
    struct px_chain chain; json_object *record, *result, *data; char path[F_PATH], expansions[F_PATH], base[F_PATH]; int number; const struct px_version *v;
    if (f_path(path, sizeof(path), round, "expansion.json") || !(record = plan_read(path)))
        return px_refuse("invalid_expansion", "the compiled artifact's directory carries no readable expansion record", "run the admitted round-N/compiled.json of hydra workflow plan expand", NULL);
    number = json_object_get_int(f_field(record, "round"));
    if (f_copy(expansions, sizeof(expansions), round) || !strrchr(expansions, '/') || !f_name(f_string(record, "base_run_id")) ||
        f_path(base, sizeof(base), runs, f_string(record, "base_run_id")) || number < 1 || number > (int)PLAN_EXPANSION_ROUNDS) {
        json_object_put(record); return px_refuse("invalid_expansion", "the expansion record names no valid base run or round", "run an admitted round of an existing base run", NULL);
    }
    *strrchr(expansions, '/') = '\0';
    px_chain_load(&chain, expansions, base, runs);
    v = (size_t)number < chain.count ? &chain.versions[number] : NULL;
    if (!(result = bind_refusal(&chain, v, record, number, accepted)) && v) {
        data = json_object_new_object(); json_object_object_add(data, "round", json_object_new_int(number)); f_string_add(data, "compiled_sha256", v->sha256);
        if (!strcmp(v->run_state, "absent")) f_string_add(data, "previous_run", v->run_id); else json_object_object_add(data, "previous_run", NULL);
        result = f_success("workflow plan expand", data);
    }
    px_chain_free(&chain); json_object_put(record); return result;
}

/* expansion-admit <tmp> <expansions> <base-run> <runs> <proposal> <source> <time>
 * expansion-show|expansion-view <expansions> <base-run> <runs>
 * expansion-bind <round> <accepted> <runs> */
json_object *plan_expansion_cli(int argc, char **argv, bool *printed) {
    struct px_chain chain; json_object *result = NULL, *proposal; char *end; long recorded_at;
    if (!strcmp(argv[0], "expansion-bind") && argc == 4) return px_bind(argv[1], argv[2], argv[3]);
    if ((!strcmp(argv[0], "expansion-show") || !strcmp(argv[0], "expansion-view")) && argc == 4) {
        px_chain_load(&chain, argv[1], argv[2], argv[3]);
        if (!chain.versions[0].compiled) result = px_refuse("invalid_run", "the base run has no accepted compiled plan matching its approval digest", "name a run created by hydra workflow plan run", NULL);
        else if (!strcmp(argv[0], "expansion-view")) { json_object *data = px_show(&chain); view(data); json_object_put(data); *printed = true; }
        else result = f_success("workflow plan expand", px_show(&chain));
        px_chain_free(&chain); return result;
    }
    if (strcmp(argv[0], "expansion-admit") || argc != 8) return px_refuse("invalid_arguments", "expected expansion-admit, expansion-show, expansion-view or expansion-bind with their arguments", NULL, NULL);
    errno = 0; recorded_at = strtol(argv[7], &end, 10);
    if (errno || *end || recorded_at < 0) return px_refuse("invalid_arguments", "recorded time must be a nonnegative integer", NULL, NULL);
    if (!(proposal = plan_read(argv[5]))) return px_refuse("invalid_expansion", "the proposal must be a bounded UTF-8 JSON object without duplicate members", "write the proposal as hydra-plan-expansion-1 JSON", NULL);
    px_chain_load(&chain, argv[2], argv[3], argv[4]);
    result = px_admit(argv[1], argv[2], &chain, proposal, argv[6], recorded_at);
    px_chain_free(&chain); json_object_put(proposal); return result;
}
