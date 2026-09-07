#include "plan.h"
#include <string.h>

static void items(const char *label, json_object *array) {
    size_t i; printf("%s:", label);
    for (i = 0; i < json_object_array_length(array); i++) printf(" %s%s", i ? ", " : "", task_text(json_object_array_get_idx(array, i)));
    putchar('\n');
}
int plan_preview(json_object *compiled) {
    json_object *plan = f_field(compiled, "plan"), *errors = json_object_new_array(), *env = f_field(plan, "envelope");
    char digest[65]; size_t i; int status = -1;
    if (plan_validate(plan, f_field(compiled, "policy"), errors) || plan_digest(compiled, digest) || !f_number_is(compiled, "schema_version", 1) ||
        !f_string(compiled, "compiler") || strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) || !f_string(f_field(compiled, "source"), "root") ||
        !f_string(f_field(compiled, "source"), "commit") || !task_hex(f_string(f_field(compiled, "source"), "sha256"), 64)) goto done;
    printf("Plan %s (%s)\n%s\n\n", f_string(plan, "id"), f_string(compiled, "compiler"), f_string(plan, "objective"));
    items("Context", f_field(plan, "context")); items("Assumptions", f_field(plan, "assumptions")); items("Open questions", f_field(plan, "questions"));
    items("Hosts", f_field(env, "hosts")); items("Tools", f_field(env, "tools")); items("Effects", f_field(env, "effects")); items("Declared repository writes", f_field(env, "writes"));
    printf("Budgets: parallelism %d; wall time %d seconds; artifacts %lld bytes; heads %d; disk floor %d MiB; retries 0; repairs 0\n",
        json_object_get_int(f_field(env, "parallelism")), json_object_get_int(f_field(env, "timeout_seconds")),
        (long long)json_object_get_int64(f_field(env, "artifact_bytes")), json_object_get_int(f_field(env, "max_heads")), json_object_get_int(f_field(env, "disk_mb")));
    {
        json_object *steps = f_field(plan, "steps"), *deliverables = f_field(plan, "deliverables"), *requirements = f_field(plan, "requirements");
        puts("\nWork:");
        for (i = 0; i < json_object_array_length(steps); i++) {
            json_object *step = json_object_array_get_idx(steps, i); const char *head = f_string(f_field(step, "args"), "head");
            printf("  %s: %s/%s on local:%s; ", f_string(step, "id"), f_string(step, "role"), f_string(step, "kind"), head ? head : f_string(f_field(step, "args"), "branch"));
            items("after", f_field(step, "needs"));
            printf("    recipe: %s\n", json_object_to_json_string_ext(f_field(step, "args"), JSON_C_TO_STRING_PLAIN));
        }
        puts("\nFinal deliverables:");
        for (i = 0; i < json_object_array_length(deliverables); i++) {
            json_object *d = json_object_array_get_idx(deliverables, i);
            printf("  %s: %s (%s/%s -> sealed run artifact)\n", f_string(d, "id"), f_string(d, "description"), f_string(d, "step"), f_string(d, "output"));
        }
        puts("\nRequired acceptance:");
        for (i = 0; i < json_object_array_length(requirements); i++) {
            json_object *r = json_object_array_get_idx(requirements, i);
            printf("  %s: %s [deliverable %s, check %s]\n", f_string(r, "id"), f_string(r, "criterion"), f_string(r, "deliverable"), f_string(r, "check"));
        }
        {
            json_object *checks = f_field(plan, "checks");
            for (i = 0; i < json_object_array_length(checks); i++) {
                json_object *c = json_object_array_get_idx(checks, i);
                printf("  Check %s (%s): %s; %s/%s evaluates %s\n", f_string(c, "id"), f_string(c, "method"), f_string(c, "definition"), f_string(c, "step"), f_string(c, "report"), f_string(c, "deliverable"));
            }
        }
    }
    printf("\nSource: %s\nSource commit: %s\nSource fingerprint: %s\nCompiler: %s\nAcceptance digest: %s\n",
        f_string(f_field(compiled, "source"), "root"), f_string(f_field(compiled, "source"), "commit"), f_string(f_field(compiled, "source"), "sha256"), PLAN_COMPILER, digest);
    {
        json_object *inputs = f_field(f_field(compiled, "data"), "inputs");
        if (inputs) {
            if (!json_object_is_type(inputs, json_type_object)) goto done;
            json_object_object_foreach(inputs, name, decl) {
                if (!f_string(decl, "path") || !task_hex(f_string(decl, "sha256"), 64)) goto done;
                printf("Input %s: %s (%s)\n", name, f_string(decl, "path"), f_string(decl, "sha256"));
            }
        }
    }
    puts("Run requires --accept with this exact digest. Policy is bound in the artifact.");
    puts("Write scopes are declarations, not OS isolation. Coverage is traceability, not proof that the objective is satisfied.");
    status = ferror(stdout) ? -1 : 0;
done:
    json_object_put(errors); return status;
}
