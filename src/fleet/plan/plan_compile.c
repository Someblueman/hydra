#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/agent/agent.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void list(FILE *file, json_object *array) {
    size_t i;
    for (i = 0; i < json_object_array_length(array); i++) fprintf(file, "%s%s", i ? "," : "", f_text(json_object_array_get_idx(array, i)));
}
static void task_graph_args(FILE *graph, json_object *step) {
    if (strcmp(f_string(step, "kind"), "task")) return;
    json_object *args = f_field(step, "args");
    fprintf(graph, "task_args\t%s\t%s", f_string(step, "id"), f_string(args, "task_input"));
    if (f_string(args, "source_step")) fprintf(graph, "\t%s", f_string(args, "source_step"));
    fputc('\n', graph);
}
/* Lower only already checked values. This projection uses the published YAML
 * syntax; its graph is also checked by the existing workflow data validator. */
int plan_lower(json_object *plan, const char *directory) {
    char path[F_PATH], graph_path[F_PATH]; FILE *yaml = NULL, *graph = NULL; int status = -1; size_t i;
    json_object *steps = f_field(plan, "steps"), *env = f_field(plan, "envelope");
    if (f_path(path, sizeof(path), directory, "workflow.yml") || f_path(graph_path, sizeof(graph_path), directory, "graph.tsv") ||
        !(yaml = fopen(path, "wx")) || !(graph = fopen(graph_path, "wx"))) goto done;
    fprintf(yaml, "version: 1\nid: %s\ndata: data.json\nparallelism: %d\nresources:\n  disk_mb: %d\n  max_heads: %d\nsteps:\n",
        f_string(plan, "id"), json_object_get_int(f_field(env, "parallelism")), json_object_get_int(f_field(env, "disk_mb")), json_object_get_int(f_field(env, "max_heads")));
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i), *args = f_field(step, "args"), *needs = f_field(step, "needs");
        fprintf(yaml, "  - id: %s\n    kind: %s\n    needs: [", f_string(step, "id"), f_string(step, "kind")); list(yaml, needs);
        fprintf(yaml, "]\n    retry: 0\n    idempotent: false\n    args:\n");
        json_object_object_foreach(args, key, value) {
            fprintf(yaml, "      %s: ", key);
            if (!strcmp(key, "argv")) { fputc('[', yaml); list(yaml, value); fputc(']', yaml); }
            else if (json_object_is_type(value, json_type_int)) fprintf(yaml, "%d", json_object_get_int(value));
            else fputs(f_text(value), yaml);
            fputc('\n', yaml);
        }
        fprintf(graph, "step\t%s\t%s\t", f_string(step, "id"), f_string(step, "kind"));
        if (json_object_array_length(needs)) list(graph, needs); else fputc('-', graph);
        fputc('\n', graph);
        task_graph_args(graph, step);
    }
    if (ferror(yaml) || ferror(graph)) goto done;
    status = task_write_json(directory, "data.json", f_field(plan, "data"), false);
done:
    if (yaml && fclose(yaml)) status = -1;
    if (graph && fclose(graph)) status = -1;
    return status;
}
static json_object *source_binding(const char *source) {
    struct f_capture cap = {0}; char root[F_PATH], digest[65]; json_object *out = NULL;
    char *clean[] = {"diff", "--quiet", "HEAD", "--", NULL}, *head[] = {"rev-parse", "HEAD", NULL};
    if (!realpath(source, root) || task_git(root, clean, &cap)) goto done;
    f_capture_free(&cap);
    if (task_git(root, head, &cap) || wd_fingerprint(root, digest)) goto done;
    cap.out[strcspn(cap.out, "\r\n")] = '\0';
    out = json_object_new_object(); f_string_add(out, "root", root); f_string_add(out, "commit", cap.out); f_string_add(out, "sha256", digest);
done:
    f_capture_free(&cap); return out;
}
/* Resolve only explicitly declared data and adapter contracts. No model calls,
 * probes, head creation, or recipe execution occur during compilation. */
static json_object *bind_inputs(json_object *data, const char *source, const char *scratch, int64_t limit, int64_t rounds) {
    char path[F_PATH]; int64_t total = 0; json_object *bound = plan_canonical(data), *inputs = f_field(bound, "inputs");
    if (f_path(path, sizeof(path), scratch, "input")) goto bad;
    json_object_object_foreach(inputs, name, declaration) {
        json_object *file; const char *origin = f_string(declaration, "source"); (void)name;
        if (origin && strcmp(origin, "repository")) goto bad;
        if (task_file_copy(source, f_string(declaration, "path"), path) || !(file = wd_file(path, declaration))) goto bad;
        f_string_add(declaration, "sha256", f_string(file, "sha256")); json_object_put(file); unlink(path);
        total += json_object_get_int64(f_field(declaration, "max_bytes"));
    }
    {
        json_object *steps = f_field(bound, "steps");
        json_object_object_foreach(steps, id, step) {
            json_object *outputs = f_field(step, "outputs"); (void)id;
            if (!outputs) continue;
            json_object_object_foreach(outputs, name, declaration) { (void)name; total += rounds * json_object_get_int64(f_field(declaration, "max_bytes")); }
        }
    }
    if (total > limit) goto bad;
    return bound;
bad:
    json_object_put(bound); return NULL;
}
static json_object *profiles(json_object *plan) {
    json_object *steps = f_field(plan, "steps"), *out = json_object_new_object(); size_t i;
    for (i = 0; i < json_object_array_length(steps); i++) {
        json_object *args = f_field(json_object_array_get_idx(steps, i), "args"); const char *name = f_string(args, "profile"); json_object *profile;
        if (!name || f_field(out, name)) continue;
        profile = agent_profile(name); if (!profile || !agent_capability(profile, "prompt")) { json_object_put(profile); json_object_put(out); return NULL; }
        json_object_object_add(out, name, profile);
    }
    return out;
}
static json_object *context_files(json_object *plan, const char *source, const char *scratch, int64_t *bytes) {
    json_object *refs = f_field(plan, "context"), *files = json_object_new_object(); size_t i; char path[F_PATH];
    json_object *decl = f_parse("{\"type\":\"file\",\"max_bytes\":524288}");
    if (f_path(path, sizeof(path), scratch, "context")) goto bad;
    for (i = 0; i < json_object_array_length(refs); i++) {
        const char *ref = f_text(json_object_array_get_idx(refs, i)); json_object *file;
        if (!task_path(ref) || task_file_copy(source, ref, path) || !(file = wd_file(path, decl))) goto bad;
        *bytes += json_object_get_int64(f_field(file, "bytes")); json_object_object_add(files, ref, file); unlink(path);
    }
    json_object_put(decl); return files;
bad:
    json_object_put(decl); json_object_put(files); return NULL;
}
static bool complete_binding(json_object *plan, const char *source, const char *scratch, json_object *compiled, json_object *errors) {
    json_object *binding = f_field(compiled, "source"), *after; bool stable;
    if (f_number_is(plan, "schema_version", 2)) {
        json_object *tasks = plan_task_bindings(plan, f_field(compiled, "data"), binding, scratch, errors);
        if (!tasks) return false;
        json_object_object_add(compiled, "tasks", tasks);
    }
    after = source_binding(source); stable = after && json_object_equal(binding, after); json_object_put(after);
    if (!stable) { plan_error(errors, "source", "source_changed", "source changed while resolving inputs"); return false; }
    if (strlen(json_object_to_json_string_ext(compiled, JSON_C_TO_STRING_PLAIN)) > PLAN_LIMIT) {
        plan_error(errors, "$", "compiled_limit", "resolved artifact exceeds 256 KiB"); return false;
    }
    return true;
}
json_object *plan_compile(json_object *plan, json_object *policy, const char *source, json_object *errors) {
    char scratch[] = "/tmp/hydra-plan-compile.XXXXXX", data_path[F_PATH], graph_path[F_PATH], yaml_path[F_PATH];
    json_object *compiled = NULL, *manifest = NULL, *binding = NULL, *adapters = NULL, *data = NULL, *normalized = NULL, *context = NULL; char *yaml = NULL;
    int64_t context_bytes = 0;
    if (plan_validate(plan, policy, errors)) return NULL;
    if (!mkdtemp(scratch)) { plan_error(errors, "$", "io_error", "cannot create compiler scratch directory"); return NULL; }
    normalized = plan_canonical(plan); plan = normalized;
    if (plan_lower(plan, scratch) || f_path(data_path, sizeof(data_path), scratch, "data.json") || f_path(graph_path, sizeof(graph_path), scratch, "graph.tsv") ||
        !(manifest = wd_manifest(data_path, graph_path))) { plan_error(errors, "data", "invalid_handoff", "invalid artifact types, bounds, paths or direct producer dependencies"); goto done; }
    if (!(binding = source_binding(source))) { plan_error(errors, "source", "invalid_source", "source must be a Git repository with no tracked changes and bounded readable content"); goto done; }
    if (!(context = context_files(plan, source, scratch, &context_bytes))) { plan_error(errors, "context", "invalid_context", "context references must be bounded existing repository files; snapshot external sources first"); goto done; }
    if (!(data = bind_inputs(manifest, source, scratch, json_object_get_int64(f_field(f_field(plan, "envelope"), "artifact_bytes")) - context_bytes,
        1 + json_object_get_int64(f_field(f_field(plan, "envelope"), "repair_budget"))))) {
        plan_error(errors, "data", "invalid_inputs_or_budget", "repository inputs must exist and match their type/digest; declared inputs and every reserved output round must fit the artifact envelope"); goto done;
    }
    if (strlen(json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN)) > WD_LIMIT) { plan_error(errors, "data", "bound_data_limit", "input digests make the resolved data manifest exceed 64 KiB"); goto done; }
    if (!(adapters = profiles(plan))) { plan_error(errors, "steps.args.profile", "unsupported_profile", "headless prompt profile is unavailable"); goto done; }
    if (f_path(yaml_path, sizeof(yaml_path), scratch, "workflow.yml") || !(yaml = f_read(yaml_path, PLAN_LIMIT))) goto done;
    compiled = json_object_new_object(); json_object_object_add(compiled, "schema_version", json_object_new_int(1));
    f_string_add(compiled, "compiler", PLAN_COMPILER);
    json_object_object_add(compiled, "plan", plan_canonical(plan)); json_object_object_add(compiled, "policy", plan_canonical(policy));
    json_object_object_add(compiled, "source", json_object_get(binding)); json_object_object_add(compiled, "profiles", json_object_get(adapters));
    json_object_object_add(compiled, "context", json_object_get(context));
    json_object_object_add(compiled, "data", json_object_get(data)); f_string_add(compiled, "workflow", yaml);
    if (!complete_binding(plan, source, scratch, compiled, errors)) { json_object_put(compiled); compiled = NULL; }
done:
    free(yaml); json_object_put(manifest); json_object_put(binding); json_object_put(adapters); json_object_put(data); json_object_put(normalized); json_object_put(context); f_remove_tree(scratch);
    return compiled;
}
int plan_materialize(json_object *compiled, const char *directory) {
    char path[F_PATH]; const char *yaml = f_string(compiled, "workflow");
    if (!yaml || f_path(path, sizeof(path), directory, "workflow.yml") || f_write(path, yaml, strlen(yaml), false) ||
        task_write_json(directory, "data.json", f_field(compiled, "data"), false) || task_write_json(directory, "compiled.json", compiled, false)) return -1;
    return 0;
}
int plan_admit(json_object *compiled, const char *source, const char *accepted) {
    char digest[65], expected[65]; json_object *errors = json_object_new_array(), *fresh = NULL; int status = -1;
    if (!task_hex(accepted, 64) || plan_digest(compiled, digest) || strcmp(digest, accepted)) goto done;
    fresh = plan_compile(f_field(compiled, "plan"), f_field(compiled, "policy"), source, errors);
    if (!fresh || plan_digest(fresh, expected) || strcmp(digest, expected)) goto done;
    status = 0;
done:
    json_object_put(errors); json_object_put(fresh); return status;
}
int plan_heads_available(json_object *compiled, const char *source) {
    json_object *steps = f_field(f_field(compiled, "plan"), "steps"); size_t i;
    for (i = 0; i < json_object_array_length(steps); i++) {
        const char *branch = f_string(f_field(json_object_array_get_idx(steps, i), "args"), "branch");
        char ref[128]; struct f_capture cap = {0}; int status;
        char *argv[] = {"show-ref", "--verify", "--quiet", ref, NULL};
        if (!branch) continue;
        if (snprintf(ref, sizeof(ref), "refs/heads/%s", branch) >= (int)sizeof(ref)) return -1;
        status = task_git(source, argv, &cap); f_capture_free(&cap);
        if (status != 1) return -1;
        if (snprintf(ref, sizeof(ref), "refs/remotes/origin/%s", branch) >= (int)sizeof(ref)) return -1;
        status = task_git(source, argv, &cap); f_capture_free(&cap);
        if (status != 1) return -1;
    }
    return 0;
}
