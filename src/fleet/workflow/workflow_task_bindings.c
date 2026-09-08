#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/transport/remote.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

json_object *wt_destination(const char *alias) {
    struct f_remote remote; json_object *out; char home[F_PATH];
    if (!alias) return NULL;
    if (!strcmp(alias, "local")) {
        if (!realpath(f_home, home)) return NULL;
        out = json_object_new_object(); f_string_add(out, "kind", "local");
        f_string_add(out, "home", home); f_string_add(out, "hydra", f_hydra); return out;
    }
    if (f_remote_load(alias, &remote)) return NULL;
    out = json_object_new_object(); f_string_add(out, "kind", "ssh");
    f_string_add(out, "alias", alias); f_string_add(out, "target", remote.target);
    f_string_add(out, "home", remote.home); f_string_add(out, "hydra", remote.hydra);
    json_object_object_add(out, "multiplex", json_object_new_boolean(remote.multiplex)); return out;
}
static bool output_selected(json_object *outputs, const char *path) {
    for (size_t i = 0; i < json_object_array_length(outputs); i++)
        if (!strcmp(path, f_text(json_object_array_get_idx(outputs, i)))) return true;
    return false;
}
static bool io_matches(json_object *spec, json_object *declarations, const char *descriptor) {
    json_object *inputs = f_field(declarations, "inputs"), *outputs = f_field(declarations, "outputs");
    json_object *selected = f_field(spec, "inputs"), *produced = f_field(spec, "outputs");
    if (!json_object_is_type(inputs, json_type_object) || !json_object_is_type(outputs, json_type_object) ||
        json_object_array_length(selected) + 1 != (size_t)json_object_object_length(inputs) ||
        json_object_array_length(produced) != (size_t)json_object_object_length(outputs)) return false;
    for (size_t i = 0; i < json_object_array_length(selected); i++) {
        const char *name = f_text(json_object_array_get_idx(selected, i));
        if (!wd_name(name) || !strcmp(name, descriptor) || !f_field(inputs, name)) return false;
    }
    json_object_object_foreach(outputs, name, value) {
        (void)name;
        if (!output_selected(produced, f_string(value, "path"))) return false;
    }
    return true;
}
json_object *wt_step_binding(const char *run, json_object *data, const char *id, const char *descriptor) {
    json_object *declarations = f_field(f_field(data, "steps"), id), *draft = NULL, *spec = NULL, *destination = NULL, *out = NULL;
    const char *input = f_string(f_field(f_field(declarations, "inputs"), descriptor), "input"); char path[F_PATH];
    if (!wd_name(input) || snprintf(path, sizeof(path), "%s/artifacts/%s", run, input) >= (int)sizeof(path)) goto done;
    draft = f_read_json(path, WD_LIMIT); spec = task_spec(draft, false);
    if (!spec || !io_matches(spec, declarations, descriptor) || !(destination = wt_destination(f_string(spec, "host")))) goto done;
    out = json_object_new_object(); json_object_object_add(out, "spec", json_object_get(spec));
    json_object_object_add(out, "destination", json_object_get(destination)); f_string_add(out, "descriptor", descriptor);
done:
    json_object_put(destination); json_object_put(spec); json_object_put(draft); return out;
}
static int digest_field(json_object *record, const char *run, const char *file) {
    char path[F_PATH], hash[65];
    if (f_path(path, sizeof(path), run, file) || f_hash(path, hash)) return -1;
    f_string_add(record, file, hash); return 0;
}
static json_object *coordinator(void) {
    char hostname[256], home[F_PATH]; json_object *out;
    if (gethostname(hostname, sizeof(hostname)) || !memchr(hostname, '\0', sizeof(hostname)) || !realpath(f_home, home)) return NULL;
    out = json_object_new_object(); f_string_add(out, "host", hostname); f_string_add(out, "home", home);
    json_object_object_add(out, "uid", json_object_new_int64((int64_t)geteuid())); return out;
}
static bool compiled_matches(const char *run, json_object *steps) {
    char path[F_PATH];
    if (f_path(path, sizeof(path), run, "compiled.json")) return false;
    if (access(path, F_OK)) return true;
    json_object *compiled = f_read_json(path, F_LIMIT);
    bool matches = compiled && json_object_equal(steps, f_field(compiled, "tasks"));
    json_object_put(compiled); return matches;
}
static int bindings_write(const char *run, json_object *record) {
    char path[F_PATH], hash[65];
    if (digest_field(record, run, "graph.tsv") || digest_field(record, run, "data.json") ||
        task_write_json(run, "tasks.json", record, false) || f_path(path, sizeof(path), run, "tasks.json") || f_hash(path, hash) ||
        f_path(path, sizeof(path), run, "tasks-sha256") || f_write(path, hash, 64, false)) return -1;
    return task_sync_dir(run);
}
int wt_initialize(const char *run, const char *source) {
    char path[F_PATH], root[F_PATH], *graph = NULL, *line, *save = NULL;
    json_object *data = NULL, *record = NULL, *owner = coordinator(), *steps; int status = -1;
    if (!owner || !realpath(source, root) || f_path(path, sizeof(path), run, "graph.tsv") || !(graph = f_read(path, F_LIMIT))) goto done;
    if (f_path(path, sizeof(path), run, "data.json") || !(data = f_read_json(path, WD_LIMIT)) || wd_verify(data, run)) goto done;
    record = json_object_new_object(); steps = json_object_new_object();
    json_object_object_add(record, "schema_version", json_object_new_int(1)); f_string_add(record, "source", root);
    json_object_object_add(record, "coordinator", json_object_get(owner)); json_object_object_add(record, "steps", steps);
    for (line = strtok_r(graph, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *fields = NULL, *tag = strtok_r(line, "\t", &fields), *id, *descriptor; json_object *binding;
        if (!tag || strcmp(tag, "task_args")) continue;
        id = strtok_r(NULL, "\t", &fields); descriptor = strtok_r(NULL, "\t", &fields);
        if (!wd_name(id) || !wd_name(descriptor) || f_field(steps, id) ||
            !(binding = wt_step_binding(run, data, id, descriptor))) goto done;
        json_object_object_add(steps, id, binding);
    }
    if (compiled_matches(run, steps)) status = bindings_write(run, record);
done:
    free(graph); json_object_put(data); json_object_put(record); json_object_put(owner); return status;
}
json_object *wt_bindings(const char *run) {
    char path[F_PATH], hash[65], *expected = NULL; json_object *record = NULL, *owner = coordinator();
    const char *files[] = {"graph.tsv", "data.json"};
    if (!owner || f_path(path, sizeof(path), run, "tasks-sha256") || !(expected = f_read(path, 64)) ||
        f_path(path, sizeof(path), run, "tasks.json") || f_hash(path, hash) || strcmp(hash, expected) ||
        !(record = f_read_json(path, F_LIMIT)) || !f_number_is(record, "schema_version", 1) ||
        !json_object_equal(owner, f_field(record, "coordinator"))) goto bad;
    for (size_t i = 0; i < 2; i++) {
        const char *digest = f_string(record, files[i]);
        if (!digest || f_path(path, sizeof(path), run, files[i]) || f_hash(path, hash) || strcmp(hash, digest)) goto bad;
    }
    free(expected); json_object_put(owner); return record;
bad:
    free(expected); json_object_put(owner); json_object_put(record); return NULL;
}
