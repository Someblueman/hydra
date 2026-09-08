#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include <stdlib.h>
#include <string.h>

static json_object *diagnostics(json_object *errors, json_object *plan) {
    json_object *data = json_object_new_object(), *result;
    json_object_object_add(data, "diagnostics", json_object_get(errors));
    if (plan) json_object_object_add(data, "coverage", json_object_get(f_field(plan, "requirements")));
    result = f_success("workflow plan", data);
    json_object_object_add(result, "ok", json_object_new_boolean(json_object_array_length(errors) == 0));
    if (json_object_array_length(errors)) {
        json_object *error = json_object_new_object(); f_string_add(error, "code", "invalid_plan");
        f_string_add(error, "message", "plan compilation failed; see data.diagnostics");
        f_string_add(error, "recovery", "revise the indicated fields without weakening the objective or policy, then validate again");
        json_object_object_add(result, "error", error);
    }
    return result;
}
static json_object *compile_command(const char *plan_path, const char *policy_path, const char *source, const char *output) {
    json_object *plan, *policy, *compiled = NULL, *errors = json_object_new_array(), *result;
    char digest[65];
    plan = plan_read(plan_path); policy = plan_read(policy_path);
    if (!plan || !policy) plan_error(errors, "$", "invalid_json", "expected bounded UTF-8 JSON objects without duplicate members");
    else compiled = plan_compile(plan, policy, source, errors);
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
            return f_error("workflow plan schema", "io_error", "cannot write schema");
    }
    return NULL;
}

/* Operation handlers borrow argv; returned JSON belongs to the caller.
 * printed distinguishes successful text output from a failed operation. */
static json_object *preview_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;

    compiled = plan_read(argv[1]);
    if (compiled && !plan_preview(compiled)) { json_object_put(compiled); *printed = true; return NULL; }
    json_object_put(compiled);
    return result;
}

static json_object *result_command(char **argv, bool *printed) {
    json_object *result = NULL;
    char path[F_PATH];
    (void)printed;

    char *state = NULL;
    if (!f_path(path, sizeof(path), argv[1], "state")) state = f_read(path, 64);
    if (state && !strcmp(state, "succeeded\n")) {
        json_object *delivery = plan_delivery(argv[1]);
        if (delivery) result = f_success("workflow plan result", delivery);
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

static json_object *admit_command(char **argv, bool *printed) {
    json_object *result = NULL;
    json_object *compiled = NULL;
    (void)printed;

    compiled = plan_read(argv[1]);
    if (compiled && !plan_admit(compiled, argv[2], argv[3]) && !plan_heads_available(compiled, argv[2]) && !plan_materialize(compiled, argv[4])) result = f_success("workflow plan admit", json_object_new_object());
    json_object_put(compiled);
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

static json_object *check_context_command(char **argv, bool *printed) {
    json_object *compiled = plan_read(argv[1]), *data = plan_validation_context(compiled, argv[2]), *result = NULL;
    (void)printed;
    if (data) result = f_success("workflow plan check-context", json_object_get(data));
    json_object_put(data); json_object_put(compiled); return result;
}
static json_object *step_check_command(char **argv, bool *printed) {
    (void)printed;
    return plan_step_check(argv[1], argv[2]) ?
        f_error("workflow plan step-check", "validation_rejected", "required evidence is missing, invalid, failed or inconclusive") :
        f_success("workflow plan step-check", json_object_new_object());
}

json_object *plan_cli(int argc, char **argv) {
    static const struct {
        const char *name;
        int argc;
        json_object *(*call)(char **argv, bool *printed);
    } commands[] = {
        {"preview", 2, preview_command},
        {"result", 2, result_command},
        {"show", 2, show_command},
        {"admit", 5, admit_command},
        {"finish", 2, finish_command},
        {"heads", 2, heads_command},
        {"timeout", 2, timeout_command},
        {"projection", 2, projection_command},
        {"bindings", 4, bindings_command},
        {"data-match", 3, data_match_command},
        {"check-definition", 3, check_definition_command},
        {"check-context", 3, check_context_command},
        {"step-check", 3, step_check_command},
    };
    json_object *result = NULL;
    bool printed = false;
    if (argc == 1 && !strcmp(argv[0], "schema")) return schema_command();
    if ((argc == 4 && !strcmp(argv[0], "validate")) || (argc == 5 && !strcmp(argv[0], "compile")))
        return compile_command(argv[1], argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (argc != commands[i].argc || strcmp(argv[0], commands[i].name)) continue;
        result = commands[i].call(argv, &printed);
        break;
    }
    if (printed) return NULL;
    if (!result) result = f_error("workflow plan", "invalid_or_stale_plan", "plan is malformed, unaccepted, changed, unsupported, or lacks passing artifact-bound verification");
    if (f_field(result, "error") && !f_field(result, "data"))
        f_string_add(f_field(result, "error"), "recovery", "inspect the compiled artifact, source, inputs, head availability and verification; compile and accept fresh scope when bindings change");
    return result;
}
