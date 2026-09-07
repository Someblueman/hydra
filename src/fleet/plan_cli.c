#include "plan.h"
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
json_object *plan_cli(int argc, char **argv) {
    json_object *plan = NULL, *policy = NULL, *compiled = NULL, *errors = json_object_new_array(), *result = NULL;
    char digest[65], path[F_PATH];
    if (argc == 1 && !strcmp(argv[0], "schema")) {
        static const char *const schema[] = {
#include "plan_schema.inc"
        };
        size_t i;
        for (i = 0; i < sizeof(schema) / sizeof(schema[0]); i++) if (fputs(schema[i], stdout) == EOF) {
            json_object_put(errors); return f_error("workflow plan schema", "io_error", "cannot write schema");
        }
        json_object_put(errors); return NULL;
    }
    if ((argc == 4 && !strcmp(argv[0], "validate")) || (argc == 5 && !strcmp(argv[0], "compile"))) {
        plan = plan_read(argv[1]); policy = plan_read(argv[2]);
        if (!plan || !policy) plan_error(errors, "$", "invalid_json", "expected bounded UTF-8 JSON objects without duplicate members");
        else compiled = plan_compile(plan, policy, argv[3], errors);
        if (!compiled && !json_object_array_length(errors)) plan_error(errors, "$", "compile_failed", "compiler could not resolve the plan");
        if (compiled && !strcmp(argv[0], "compile")) {
            const char *text; json_object *canonical = plan_canonical(compiled);
            text = json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN);
            if (strlen(text) > PLAN_LIMIT || plan_digest(compiled, digest) || f_write(argv[4], text, strlen(text), false))
                plan_error(errors, "$", "artifact_write_failed", "compiled artifact must fit 256 KiB and use a new writable output path");
            json_object_put(canonical);
        }
        result = diagnostics(errors, plan);
        if (compiled && !json_object_array_length(errors) && !plan_digest(compiled, digest)) f_string_add(f_field(result, "data"), "sha256", digest);
    } else if (argc == 2 && !strcmp(argv[0], "preview")) {
        compiled = plan_read(argv[1]);
        if (compiled && !plan_preview(compiled)) { json_object_put(compiled); json_object_put(errors); return NULL; }
    } else if (argc == 2 && !strcmp(argv[0], "result")) {
        char *state = NULL;
        if (!f_path(path, sizeof(path), argv[1], "state")) state = f_read(path, 64);
        if (state && !strcmp(state, "succeeded\n")) {
            json_object *delivery = plan_delivery(argv[1]);
            if (delivery) result = f_success("workflow plan result", delivery);
        }
        free(state);
    } else if (argc == 2 && !strcmp(argv[0], "show")) {
        compiled = plan_read(argv[1]);
        if (compiled && f_number_is(compiled, "schema_version", 1) && f_string(compiled, "compiler") && !strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) && !plan_digest(compiled, digest)) {
            json_object *data = json_object_new_object(); f_string_add(data, "sha256", digest);
            json_object_object_add(data, "resolved", json_object_get(compiled));
            f_string_add(data, "acceptance", "run requires --accept with this exact digest; scopes are declarations, not OS isolation");
            result = f_success("workflow plan show", data);
        }
    } else if (argc == 5 && !strcmp(argv[0], "admit")) {
        compiled = plan_read(argv[1]);
        if (compiled && !plan_admit(compiled, argv[2], argv[3]) && !plan_heads_available(compiled, argv[2]) && !plan_materialize(compiled, argv[4])) result = f_success("workflow plan admit", json_object_new_object());
    } else if (argc == 2 && !strcmp(argv[0], "finish")) {
        if (!plan_finish(argv[1])) result = f_success("workflow plan finish", json_object_new_object());
    } else if (argc == 2 && !strcmp(argv[0], "heads")) {
        compiled = plan_read(argv[1]);
        json_object *steps = f_field(f_field(compiled, "plan"), "steps"); size_t i;
        if (plan_list(steps, 1, PLAN_STEPS)) {
            for (i = 0; i < json_object_array_length(steps); i++) {
                const char *branch = f_string(f_field(json_object_array_get_idx(steps, i), "args"), "branch");
                if (branch && plan_id(branch)) puts(branch);
            }
            json_object_put(compiled); json_object_put(errors); return NULL;
        }
    } else if (argc == 2 && !strcmp(argv[0], "timeout")) {
        compiled = plan_read(argv[1]);
        if (compiled && f_field(f_field(compiled, "plan"), "envelope")) {
            printf("%d\n", json_object_get_int(f_field(f_field(f_field(compiled, "plan"), "envelope"), "timeout_seconds")));
            json_object_put(compiled); json_object_put(errors); return NULL;
        }
    } else if (argc == 2 && !strcmp(argv[0], "projection")) {
        compiled = plan_read(argv[1]);
        if (compiled && f_string(compiled, "workflow")) { fputs(f_string(compiled, "workflow"), stdout); result = f_success("workflow plan projection", NULL); }
        if (result) { json_object_put(result); json_object_put(compiled); json_object_put(errors); return NULL; }
    } else if (argc == 4 && !strcmp(argv[0], "bindings")) {
        compiled = plan_read(argv[1]);
        if (compiled && !plan_admit(compiled, argv[2], argv[3])) result = f_success("workflow plan bindings", json_object_new_object());
    } else if (argc == 3 && !strcmp(argv[0], "data-match")) {
        compiled = plan_read(argv[1]);
        if (!f_path(path, sizeof(path), argv[2], "data.json")) plan = plan_read(path);
        if (compiled && plan && json_object_equal(f_field(compiled, "data"), plan)) result = f_success("workflow plan data-match", json_object_new_object());
    }
    if (!result) result = f_error("workflow plan", "invalid_or_stale_plan", "plan is malformed, unaccepted, changed, unsupported, or lacks passing artifact-bound verification");
    if (f_field(result, "error") && !f_field(result, "data")) f_string_add(f_field(result, "error"), "recovery", "inspect the compiled artifact, source, inputs, head availability and verification; compile and accept fresh scope when bindings change");
    json_object_put(plan); json_object_put(policy); json_object_put(compiled); json_object_put(errors); return result;
}
