#include "fleet/support/json.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <string.h>
#include <stdlib.h>

static void items(FILE *out, const char *label, json_object *array) {
    size_t i; fprintf(out, "%s:", label);
    for (i = 0; i < json_object_array_length(array); i++) fprintf(out, " %s%s", i ? ", " : "", f_text(json_object_array_get_idx(array, i)));
    fputc('\n', out);
}
int plan_preview(json_object *compiled, FILE *out) {
    json_object *plan = f_field(compiled, "plan"), *errors = json_object_new_array(), *env = f_field(plan, "envelope");
    char digest[65]; size_t i; int status = -1;
    if (plan_validate(plan, f_field(compiled, "policy"), errors) || plan_digest(compiled, digest) || !f_number_is(compiled, "schema_version", 1) ||
        !f_string(compiled, "compiler") || strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) || !f_string(f_field(compiled, "source"), "root") ||
        !f_string(f_field(compiled, "source"), "commit") || !task_hex(f_string(f_field(compiled, "source"), "sha256"), 64)) goto done;
    fprintf(out, "Plan %s (%s)\n%s\n\n", f_string(plan, "id"), f_string(compiled, "compiler"), f_string(plan, "objective"));
    items(out, "Context", f_field(plan, "context")); items(out, "Assumptions", f_field(plan, "assumptions")); items(out, "Open questions", f_field(plan, "questions"));
    items(out, "Hosts", f_field(env, "hosts")); items(out, "Tools", f_field(env, "tools")); items(out, "Effects", f_field(env, "effects")); items(out, "Declared repository writes", f_field(env, "writes"));
    fprintf(out, "Budgets: parallelism %d; wall time %d seconds; artifacts %lld bytes; heads %d; disk floor %d MiB; retries 0; repairs 0\n",
        json_object_get_int(f_field(env, "parallelism")), json_object_get_int(f_field(env, "timeout_seconds")),
        (long long)json_object_get_int64(f_field(env, "artifact_bytes")), json_object_get_int(f_field(env, "max_heads")), json_object_get_int(f_field(env, "disk_mb")));
    {
        json_object *steps = f_field(plan, "steps"), *deliverables = f_field(plan, "deliverables"), *requirements = f_field(plan, "requirements");
        fprintf(out, "%s\n", "\nWork:");
        for (i = 0; i < json_object_array_length(steps); i++) {
            json_object *step = json_object_array_get_idx(steps, i); const char *head = f_string(f_field(step, "args"), "head");
            fprintf(out, "  %s: %s/%s on local:%s; ", f_string(step, "id"), f_string(step, "role"), f_string(step, "kind"), head ? head : f_string(f_field(step, "args"), "branch"));
            items(out, "after", f_field(step, "needs"));
            fprintf(out, "    recipe: %s\n", json_object_to_json_string_ext(f_field(step, "args"), JSON_C_TO_STRING_PLAIN));
        }
        fprintf(out, "%s\n", "\nFinal deliverables:");
        for (i = 0; i < json_object_array_length(deliverables); i++) {
            json_object *d = json_object_array_get_idx(deliverables, i);
            fprintf(out, "  %s: %s (%s/%s -> sealed run artifact)\n", f_string(d, "id"), f_string(d, "description"), f_string(d, "step"), f_string(d, "output"));
        }
        fprintf(out, "%s\n", "\nRequired acceptance:");
        for (i = 0; i < json_object_array_length(requirements); i++) {
            json_object *r = json_object_array_get_idx(requirements, i);
            fprintf(out, "  %s: %s [deliverable %s, check %s]\n", f_string(r, "id"), f_string(r, "criterion"), f_string(r, "deliverable"), f_string(r, "check"));
        }
        {
            json_object *checks = f_field(plan, "checks");
            for (i = 0; i < json_object_array_length(checks); i++) {
                json_object *c = json_object_array_get_idx(checks, i);
                fprintf(out, "  Check %s (%s): %s; %s/%s evaluates %s\n", f_string(c, "id"), f_string(c, "method"), f_string(c, "definition"), f_string(c, "step"), f_string(c, "report"), f_string(c, "deliverable"));
            }
        }
    }
    fprintf(out, "\nSource: %s\nSource commit: %s\nSource fingerprint: %s\nCompiler: %s\nAcceptance digest: %s\n",
        f_string(f_field(compiled, "source"), "root"), f_string(f_field(compiled, "source"), "commit"), f_string(f_field(compiled, "source"), "sha256"), PLAN_COMPILER, digest);
    {
        json_object *inputs = f_field(f_field(compiled, "data"), "inputs");
        if (inputs) {
            if (!json_object_is_type(inputs, json_type_object)) goto done;
            json_object_object_foreach(inputs, name, decl) {
                if (!f_string(decl, "path") || !task_hex(f_string(decl, "sha256"), 64)) goto done;
                fprintf(out, "Input %s: %s (%s)\n", name, f_string(decl, "path"), f_string(decl, "sha256"));
            }
        }
    }
    fprintf(out, "%s\n", "Run requires --accept with this exact digest. Policy is bound in the artifact.");
    fprintf(out, "%s\n", "Write scopes are declarations, not OS isolation. Coverage is traceability, not proof that the objective is satisfied.");
    status = ferror(out) ? -1 : 0;
done:
    json_object_put(errors); return status;
}

/* A read-only projection for the dependency-free native UI. The full preview
 * is produced by the same implementation used by `plan show`, before emitting
 * any protocol bytes. Control bytes cannot introduce records or terminal input. */
static void tui_text(const char *text) {
    const unsigned char *p=(const unsigned char *)text;
    for (; *p; p++) putchar(*p<32 || *p==127 ? ' ' : *p);
}
int plan_tui(json_object *compiled) {
    FILE *preview=tmpfile();
    json_object *plan=f_field(compiled,"plan"), *steps=f_field(plan,"steps");
    char digest[65], *line=NULL;
    size_t capacity=0, i, lines=0;
    ssize_t length;
    int status=-1;
    if (!preview) return -1;
    if (plan_preview(compiled,preview) || plan_digest(compiled,digest) || fflush(preview) || fseek(preview,0,SEEK_SET)) goto done;
    puts("HYDRA_PLAN_TUI\t1");
    printf("P\t%s\t%s\t",digest,f_string(plan,"id")); tui_text(f_string(plan,"objective")); putchar('\n');
    for (i=0;i<json_object_array_length(steps);i++) {
        json_object *step=json_object_array_get_idx(steps,i), *needs=f_field(step,"needs");
        size_t j;
        printf("N\t%s\t%s\t%s\t",f_string(step,"id"),f_string(step,"kind"),f_string(step,"role"));
        if (!json_object_array_length(needs)) putchar('-');
        for (j=0;j<json_object_array_length(needs);j++) printf("%s%s",j ? "," : "",f_text(json_object_array_get_idx(needs,j)));
        putchar('\n');
    }
    while ((length=getline(&line,&capacity,preview))>=0) {
        if (length && line[length-1]=='\n') line[length-1]='\0';
        fputs("T\t",stdout); tui_text(line); putchar('\n'); lines++;
    }
    if (ferror(preview)) goto done;
    printf("Z\t%zu\t%zu\n",json_object_array_length(steps),lines);
    status=ferror(stdout) ? -1 : 0;
done:
    free(line); fclose(preview); return status;
}
