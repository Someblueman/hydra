#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/retention/retention.h"
#include <stdlib.h>
#include <string.h>

/* A diagnostic with its own recovery (such as invalid_source) knows the next
 * step better than the generic revise-the-plan advice. */
static const char *diagnostic_recovery(json_object *errors) {
    const char *recovery = f_string(json_object_array_get_idx(errors, 0), "recovery");
    return recovery ? recovery : "revise the indicated fields without weakening the objective or policy, then validate again";
}
static json_object *diagnostics(json_object *errors, json_object *plan) {
    json_object *data = json_object_new_object(), *result;
    json_object_object_add(data, "diagnostics", json_object_get(errors));
    if (plan) {
        json_object *obligations = plan_obligations_projection(plan), *reviews = plan_obligations_reviews(plan), *analysis = json_object_new_object();
        json_object_object_add(data, "coverage", json_object_get(f_field(plan, "requirements")));
        json_object_object_add(data, "obligations", obligations);
        json_object_object_add(data, "semantic_reviews", reviews);
        f_string_add(analysis, "structural", json_object_array_length(errors) ? "not_satisfiable" : "satisfiable");
        f_string_add(analysis, "obligations", f_field(plan, "obligations") ? (json_object_array_length(errors) ? "not_satisfiable" : "satisfiable") : "legacy_projection");
        f_string_add(analysis, "runtime", "required");
        f_string_add(analysis, "semantic", f_field(plan, "obligations") ? (json_object_array_length(reviews) ? "review_required" : "not_assessed") : "unknown");
        json_object_object_add(data, "analysis", analysis);
    }
    result = f_success("workflow plan", data);
    json_object_object_add(result, "ok", json_object_new_boolean(json_object_array_length(errors) == 0));
    if (json_object_array_length(errors)) {
        json_object *error = json_object_new_object(); f_string_add(error, "code", "invalid_plan");
        f_string_add(error, "message", "plan compilation failed; see data.diagnostics");
        f_string_add(error, "recovery", diagnostic_recovery(errors));
        json_object_object_add(result, "error", error);
    }
    return result;
}
/* Assets come from an explicit directory: a proposal's assets/ or an expert's own. */
static json_object *compile_plan(json_object *plan, json_object *policy, const char *source, const char *assets_dir, json_object *errors) {
    json_object *assets = plan_assets_load(plan, assets_dir, errors), *compiled = NULL;
    if (!json_object_array_length(errors)) compiled = plan_compile(plan, policy, source, assets, errors);
    json_object_put(assets); return compiled;
}
static json_object *compile_command(const char *plan_path, const char *policy_path, const char *source, const char *output, const char *assets_dir) {
    json_object *plan, *policy, *compiled = NULL, *errors = json_object_new_array(), *result;
    char digest[65];
    plan = plan_read(plan_path); policy = plan_read(policy_path);
    if (!plan || !policy) plan_error(errors, "$", "invalid_json", "expected bounded UTF-8 JSON objects without duplicate members");
    else {
        json_object *lowered = plan_lowered(plan, policy, errors);
        json_object_put(plan); plan = lowered;
        if (plan) compiled = compile_plan(plan, policy, source, assets_dir, errors);
    }
    if (!compiled && !json_object_array_length(errors)) plan_error(errors, "$", "compile_failed", "compiler could not resolve the plan");
    if (compiled && output) {
        const char *text; json_object *canonical = plan_canonical(compiled);
        text = json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN);
        if (strlen(text) > PLAN_LIMIT || plan_digest(compiled, digest) || f_write(output, text, strlen(text), false))
            plan_error(errors, "$", "artifact_write_failed", "compiled artifact must fit 256 KiB and use a new writable output path");
        json_object_put(canonical);
    }
    result = diagnostics(errors, plan);
    if (compiled && !json_object_array_length(errors) && !plan_digest(compiled, digest)) f_string_add(f_field(result, "data"), "sha256", digest);
    json_object_put(plan); json_object_put(policy); json_object_put(compiled); json_object_put(errors);
    return result;
}

static json_object *schema_command(void) {
    static const char *const schema[] = {
#include "plan_schema.inc"
    };
    for (size_t i = 0; i < sizeof(schema) / sizeof(schema[0]); i++) {
        if (fputs(schema[i], stdout) == EOF)
            return plan_failure("workflow plan schema", "io_error", "cannot write schema", "write the schema to a writable output, for example hydra workflow plan schema > schema.json");
    }
    return NULL;
}

/* Operation handlers borrow argv; returned JSON belongs to the caller.
 * printed distinguishes successful text output from a failed operation. */
static json_object *preview_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;

    compiled = plan_read(argv[1]);
    if (compiled && !plan_preview(compiled, stdout)) { json_object_put(compiled); *printed = true; return NULL; }
    json_object_put(compiled);
    return result;
}

static json_object *tui_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]);
    if (compiled && !plan_tui(compiled)) *printed = true;
    json_object_put(compiled);
    return NULL;
}

static json_object *result_command(char **argv, bool *printed) {
    json_object *result = NULL;
    char path[F_PATH];
    (void)printed;
    if (retention_expired(argv[1])) return plan_failure("workflow plan result", "evidence_expired", "the declared audit window expired; retained state is not a currently verifiable outcome", "run an accepted plan again for an outcome that can be verified now");

    char *state = NULL;
    if (!f_path(path, sizeof(path), argv[1], "state")) state = f_read(path, 64);
    if (state && !strcmp(state, "succeeded\n")) {
        json_object *delivery = plan_delivery(argv[1]);
        if (delivery && !strcmp(argv[0], "result-view")) {
            plan_delivery_view(delivery); json_object_put(delivery); *printed = true;
        } else if (delivery) result = f_success("workflow plan result", delivery);
    }
    free(state);
    return result;
}

static json_object *show_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;
    char digest[65];
    (void)printed;

    compiled = plan_read(argv[1]);
    if (compiled && f_number_is(compiled, "schema_version", 1) && f_string(compiled, "compiler") && !strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) && !plan_digest(compiled, digest)) {
        json_object *data = json_object_new_object(); f_string_add(data, "sha256", digest);
        json_object_object_add(data, "resolved", json_object_get(compiled));
        f_string_add(data, "acceptance", "run requires --accept with this exact digest; scopes are declarations, not OS isolation");
        result = f_success("workflow plan show", data);
    }
    json_object_put(compiled);
    return result;
}

static json_object *obligations_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]), *errors = json_object_new_array(), *result = NULL;
    char digest[65];
    (void)printed;
    if ((argv[2] && strcmp(argv[2], "--json")) || !compiled || !f_number_is(compiled, "schema_version", 1) ||
        !f_string(compiled, "compiler") || strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) ||
        plan_validate(f_field(compiled, "plan"), f_field(compiled, "policy"), errors) || plan_digest(compiled, digest)) goto done;
    {
        json_object *data = json_object_new_object();
        json_object_object_add(data, "schema_version", json_object_new_int(1));
        f_string_add(data, "plan_sha256", digest);
        json_object_object_add(data, "obligations", plan_obligations_projection(f_field(compiled, "plan")));
        json_object_object_add(data, "semantic_reviews", plan_obligations_reviews(f_field(compiled, "plan")));
        f_string_add(data, "structural_proof", f_field(f_field(compiled, "plan"), "obligations") ? "satisfiable" : "legacy_projection");
        f_string_add(data, "semantic_proof", f_field(f_field(compiled, "plan"), "obligations") ? "not_claimed" : "unknown");
        result = f_success("workflow plan obligations", data);
    }
done:
    json_object_put(errors); json_object_put(compiled); return result;
}

/* Admission errors keep the command name runs have always reported. */
static json_object *run_failure(const char *code, const char *message, const char *recovery) {
    return plan_failure("workflow plan", code, message, recovery);
}
static json_object *admission_digest_error(enum plan_admission status, const char *accepted) {
    char message[256];
    if (status == PLAN_DIGEST_FORMAT) {
        (void)snprintf(message, sizeof(message), "--accept takes the full 64-character lowercase hex digest; the %zu-character value given is not one", strlen(accepted));
        return run_failure("digest_mismatch", message, "copy the whole Acceptance digest line from workflow plan show <compiled.json>");
    }
    return run_failure("digest_mismatch", "the accepted digest is not this artifact's digest: it was accepted for another compilation, or the file changed since",
                        "review workflow plan show <compiled.json> and accept the digest it prints");
}
/* A recompiled artifact whose digest differs: name the binding that moved when it is the source. */
static json_object *admission_changed_error(json_object *compiled, json_object *fresh) {
    json_object *was = f_field(compiled, "source"), *now = f_field(fresh, "source");
    const char *before = f_string(was, "commit"), *after = f_string(now, "commit");
    const char *root = f_string(was, "root"), *here = f_string(now, "root");
    char message[2 * F_PATH + 128];
    if (before && after && strcmp(before, after))
        (void)snprintf(message, sizeof(message), "the source checkout moved from commit %.12s to %.12s since this artifact was compiled", before, after);
    else if (root && here && strcmp(root, here))
        (void)snprintf(message, sizeof(message), "this artifact was compiled from %s, not from the checkout %s it is run in", root, here);
    else (void)snprintf(message, sizeof(message), "recompiling this artifact against the source gives another digest: its repository inputs, context files or agent profiles changed since it was compiled");
    return run_failure("source_changed", message, "compile the plan again from this checkout, review it with workflow plan show and accept the new digest");
}
static json_object *admission_error(enum plan_admission status, json_object *compiled, json_object *fresh, json_object *errors, const char *accepted) {
    json_object *result, *data;
    if (status == PLAN_DIGEST_FORMAT || status == PLAN_DIGEST_MISMATCH) return admission_digest_error(status, accepted);
    if (status == PLAN_CHANGED) return admission_changed_error(compiled, fresh);
    if (status == PLAN_UNDIGESTIBLE || !json_object_array_length(errors))
        return run_failure("invalid_or_stale_plan", "the compiled artifact is not a supported plan compilation", "pass the file workflow plan compile wrote, or compile the plan again");
    result = run_failure("source_changed", "this artifact no longer compiles against the source; see data.diagnostics", diagnostic_recovery(errors));
    data = json_object_new_object();
    json_object_object_add(data, "diagnostics", json_object_get(errors));
    json_object_object_add(result, "data", data);
    return result;
}
static json_object *heads_error(int status, const char *source, const char *ref) {
    char message[F_PATH + 256], recovery[512];
    if (status < 0) {
        (void)snprintf(message, sizeof(message), "could not check whether %s exists in %s", ref, source);
        return run_failure("invalid_source", message, "run the plan from the project's main checkout");
    }
    /* refs/heads/<branch> is named as the branch, refs/remotes/<remote>/<branch> as <remote>/<branch>. */
    const char *name = !strncmp(ref, "refs/heads/", 11) ? ref + 11 : !strncmp(ref, "refs/remotes/", 13) ? ref + 13 : ref;
    (void)snprintf(message, sizeof(message), "the spawn branch %s already exists in %s", name, source);
    (void)snprintf(recovery, sizeof(recovery), "give the spawn step an unused branch name, then compile and accept again; or delete the branch %s once any work on it is kept", name);
    return run_failure("branch_exists", message, recovery);
}
/* Admission refuses before anything runs; each refusal names its own cause. */
static json_object *admit_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]), *errors, *fresh = NULL, *result;
    enum plan_admission status; char conflict[PLAN_REF] = ""; int heads;
    (void)printed;
    if (!compiled)
        return run_failure("invalid_or_stale_plan", "the compiled artifact is missing, is not a bounded JSON object without duplicate members, or exceeds 256 KiB",
                            "pass the file workflow plan compile wrote");
    errors = json_object_new_array();
    status = plan_admission(compiled, argv[2], argv[3], errors, &fresh);
    if (status != PLAN_ADMITTED) result = admission_error(status, compiled, fresh, errors, argv[3]);
    else if ((heads = plan_heads_available(compiled, argv[2], conflict))) result = heads_error(heads, argv[2], conflict);
    else if (plan_materialize(compiled, argv[4]))
        result = run_failure("materialize_failed", "could not stage the admitted definition, data and inputs for the run",
                              "check free space and permissions in the temporary directory, then run the plan again");
    else result = f_success("workflow plan admit", json_object_new_object());
    json_object_put(fresh); json_object_put(errors); json_object_put(compiled);
    return result;
}

static json_object *finish_command(char **argv, bool *printed) {
    json_object *result = NULL;
    (void)printed;

    if (!plan_finish(argv[1])) result = f_success("workflow plan finish", json_object_new_object());
    return result;
}

static json_object *heads_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;

    compiled = plan_read(argv[1]);
    json_object *steps = f_field(f_field(compiled, "plan"), "steps"); size_t i;
    if (plan_list(steps, 1, PLAN_STEPS)) {
        for (i = 0; i < json_object_array_length(steps); i++) {
            const char *branch = f_string(f_field(json_object_array_get_idx(steps, i), "args"), "branch");
            if (branch && plan_id(branch)) puts(branch);
        }
        json_object_put(compiled); *printed = true; return NULL;
    }
    json_object_put(compiled);
    return result;
}

/* One "step<TAB>role<TAB>kind<TAB>head" line per step: the head is the branch
 * a spawn step creates or the branch another step runs on, "-" otherwise. */
static json_object *roles_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]);
    json_object *steps = f_field(f_field(compiled, "plan"), "steps");
    if (plan_list(steps, 1, PLAN_STEPS)) {
        for (size_t i = 0; i < json_object_array_length(steps); i++) {
            json_object *step = json_object_array_get_idx(steps, i), *args = f_field(step, "args");
            const char *id = f_string(step, "id"), *role = f_string(step, "role"), *kind = f_string(step, "kind");
            const char *head = f_string(args, "head") ? f_string(args, "head") : f_string(args, "branch");
            if (!plan_id(id) || !role || !kind) continue;
            printf("%s\t%s\t%s\t%s\n", id, role, kind, head && plan_id(head) ? head : "-");
        }
        *printed = true;
    }
    json_object_put(compiled);
    return NULL;
}

static json_object *hooks_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]);
    if (compiled && !plan_hooks(compiled, stdout)) *printed = true;
    json_object_put(compiled);
    return NULL;
}

/* The commit a run's agent steps left on a worker head, as recorded. */
static json_object *head_commit_command(char **argv, bool *printed) {
    char path[F_PATH], commit[65];
    json_object *compiled = f_path(path, sizeof(path), argv[1], "compiled.json") ? NULL : plan_read(path);
    if (compiled && !plan_head_commit(argv[1], f_field(compiled, "plan"), argv[2], commit)) { puts(commit); *printed = true; }
    json_object_put(compiled);
    return NULL;
}

static json_object *command_report_command(char **argv, bool *printed) {
    (void)printed;
    return plan_command_report(argv[1], argv[2], argv[3]) ?
        plan_failure("workflow plan command-report", "check_unbound", "the check did not start and end on the commit it checks, or its plan is not the accepted one",
                     "inspect the worker head: a command check must not move the head it runs on") :
        f_success("workflow plan command-report", json_object_new_object());
}

static json_object *timeout_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;

    compiled = plan_read(argv[1]);
    if (compiled && f_field(f_field(compiled, "plan"), "envelope")) {
        printf("%d\n", json_object_get_int(f_field(f_field(f_field(compiled, "plan"), "envelope"), "timeout_seconds")));
        json_object_put(compiled); *printed = true; return NULL;
    }
    json_object_put(compiled);
    return result;
}

static json_object *projection_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;

    compiled = plan_read(argv[1]);
    if (compiled && f_string(compiled, "workflow")) { fputs(f_string(compiled, "workflow"), stdout); result = f_success("workflow plan projection", NULL); }
    if (result) { json_object_put(result); json_object_put(compiled); *printed = true; return NULL; }
    json_object_put(compiled);
    return result;
}

static json_object *bindings_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;
    (void)printed;

    compiled = plan_read(argv[1]);
    if (compiled && !plan_admit(compiled, argv[2], argv[3])) result = f_success("workflow plan bindings", json_object_new_object());
    json_object_put(compiled);
    return result;
}

static json_object *data_match_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;
    json_object *plan = NULL;
    char path[F_PATH];
    (void)printed;

    compiled = plan_read(argv[1]);
    if (!f_path(path, sizeof(path), argv[2], "data.json")) plan = plan_read(path);
    if (compiled && plan && json_object_equal(f_field(compiled, "data"), plan)) result = f_success("workflow plan data-match", json_object_new_object());
    json_object_put(compiled);
    json_object_put(plan);
    return result;
}

static json_object *check_definition_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]), *result = NULL;
    json_object *errors = json_object_new_array(); char digest[65];
    (void)printed;
    if (compiled && f_number_is(compiled, "schema_version", 1) &&
        !plan_validate(f_field(compiled, "plan"), f_field(compiled, "policy"), errors) &&
        !plan_check_digest(compiled, argv[2], digest)) {
        json_object *data = json_object_new_object();
        f_string_add(data, "check", argv[2]); f_string_add(data, "validator_sha256", digest);
        result = f_success("workflow plan check-definition", data);
    }
    json_object_put(errors); json_object_put(compiled); return result;
}
static json_object *check_recipe_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]), *result = NULL, *errors = json_object_new_array(); char digest[65];
    (void)printed;
    if (compiled && f_number_is(compiled, "schema_version", 1) && !plan_validate(f_field(compiled, "plan"), f_field(compiled, "policy"), errors) && !plan_recipe_digest(compiled, argv[2], digest)) {
        json_object *data = json_object_new_object(); f_string_add(data, "check", argv[2]); f_string_add(data, "validator_recipe_sha256", digest);
        result = f_success("workflow plan check-recipe", data);
    }
    json_object_put(errors); json_object_put(compiled); return result;
}

static json_object *check_context_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]), *data = plan_validation_context(compiled, argv[2]), *result = NULL;
    (void)printed;
    if (data) result = f_success("workflow plan check-context", json_object_get(data));
    json_object_put(data); json_object_put(compiled); return result;
}
static json_object *step_check_command(char **argv, bool *printed) {
    (void)printed;
    return plan_step_check(argv[1], argv[2]) ?
        plan_failure("workflow plan step-check", "validation_rejected", "required evidence is missing, invalid, failed or inconclusive", "inspect the step's report and the evidence it was checked against; the step passes only with fresh passing evidence") :
        f_success("workflow plan step-check", json_object_new_object());
}

static json_object *repair_command(char **argv, bool *printed) {
    int status = plan_repair(argv[1], !strcmp(argv[0], "repair-resume"));
    if (status < 0) return plan_failure("workflow plan repair", "repair_state_invalid", "recorded repair state is invalid; do not start replacement work", "inspect the run's repair records with hydra workflow status before resuming or abandoning the run");
    printf("%d\n", status); *printed = true; return NULL;
}
static json_object *inspect_command(char **argv, bool *printed) {
    int argc = 0; (void)printed;
    while (argv[argc]) argc++;
    return plan_inspect_cli(argc, argv);
}

static json_object *proposal_command(char **argv, bool *printed) {
    (void)printed;
    return plan_proposal_copy(argv[1], argv[2], argv[3]);
}
static json_object *proposal_asset_command(char **argv, bool *printed) {
    (void)printed;
    return plan_proposal_asset(argv[1], argv[2], argv[3]);
}
/* validate <plan> <policy> <source> and compile ... <output>, each with an
 * optional trailing --assets-dir <directory>. */
static json_object *compile_dispatch(int argc, char **argv, bool *matched) {
    bool compile = !strcmp(argv[0], "compile"); int base = compile ? 5 : 4;
    const char *assets = NULL;
    *matched = compile || !strcmp(argv[0], "validate");
    if (!*matched) return NULL;
    if (argc == base + 2 && !strcmp(argv[base], "--assets-dir")) assets = argv[base + 1];
    else if (argc != base) return NULL;
    return compile_command(argv[1], argv[2], argv[3], compile ? argv[4] : NULL, assets);
}

/* The short planning flow's commands take their own argument counts. */
typedef json_object *(*flow_call)(int argc, char **argv);
static flow_call flow_command(const char *name) {
    static const struct { const char *name; flow_call call; } flows[] = {
        {"check", plan_check_cli}, {"policy-for", plan_policy_for_cli}, {"policy-tool", plan_policy_tool_cli},
        {"policy-text", plan_policy_text_cli}, {"review-text", plan_review_text_cli}, {"summary", plan_summary_cli},
        {"detach", plan_detach_cli}, {"verified-commit", plan_verified_commit_cli},
    };
    for (size_t i = 0; i < sizeof(flows) / sizeof(flows[0]); i++)
        if (!strcmp(name, flows[i].name)) return flows[i].call;
    return NULL;
}

json_object *plan_cli(int argc, char **argv) {
    static const struct {
        const char *name;
        int argc;
        json_object *(*call)(char **argv, bool *printed);
    } commands[] = {
        {"proposal-copy", 3, proposal_command},
        {"proposal-copy", 4, proposal_command},
        {"proposal-asset", 4, proposal_asset_command},
        {"preview", 2, preview_command},
        {"tui-data", 2, tui_command},
        {"result", 2, result_command},
        {"result-view", 2, result_command},
        {"show", 2, show_command},
        {"obligations", 2, obligations_command},
        {"obligations", 3, obligations_command},
        {"explain", 2, inspect_command},
        {"explain", 4, inspect_command},
        {"compare", 3, inspect_command},
        {"compare", 5, inspect_command},
        {"admit", 5, admit_command},
        {"finish", 2, finish_command},
        {"heads", 2, heads_command},
        {"roles", 2, roles_command},
        {"hooks", 2, hooks_command},
        {"head-commit", 3, head_commit_command},
        {"command-report", 4, command_report_command},
        {"timeout", 2, timeout_command},
        {"projection", 2, projection_command},
        {"bindings", 4, bindings_command},
        {"data-match", 3, data_match_command},
        {"check-definition", 3, check_definition_command},
        {"check-recipe", 3, check_recipe_command},
        {"check-context", 3, check_context_command},
        {"step-check", 3, step_check_command},
        {"repair", 2, repair_command},
        {"repair-resume", 2, repair_command},
    };
    json_object *result = NULL;
    bool printed = false, compile = false;
    if (argc == 1 && !strcmp(argv[0], "schema")) return schema_command();
    if (argc >= 1 && flow_command(argv[0])) return flow_command(argv[0])(argc, argv);
    if (argc >= 1) result = compile_dispatch(argc, argv, &compile);
    if (compile) return result ? result : plan_failure("workflow plan", "invalid_arguments", "use validate or compile with an optional trailing --assets-dir <directory>", "run hydra workflow plan --help");
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (argc != commands[i].argc || strcmp(argv[0], commands[i].name)) continue;
        result = commands[i].call(argv, &printed);
        break;
    }
    if (printed) return NULL;
    /* Commands that fail without saying why share this catch-all; errors a
     * command returns already carry their own recovery. */
    if (!result) result = plan_failure("workflow plan", "invalid_or_stale_plan", "plan is malformed, unaccepted, changed, unsupported, or lacks passing artifact-bound verification",
                                       "inspect the compiled artifact, source, inputs, head availability and verification; compile and accept fresh scope when bindings change");
    return result;
}
