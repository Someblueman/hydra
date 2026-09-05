#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static json_object *file(json_object *files, const char *path) {
    size_t i;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *entry = json_object_array_get_idx(files, i);
        if (!strcmp(f_string(entry, "path"), path)) return entry;
    }
    return NULL;
}
static json_object *decode(json_object *files, const char *relative, const char *scratch) {
    json_object *entry = file(files, relative), *result; char path[F_PATH];
    if (!entry || f_path(path, sizeof(path), scratch, "evidence.json") || f_hex_write(path, f_string(entry, "hex"), 0600)) return NULL;
    result = f_read_json(path, TASK_FILE_LIMIT); unlink(path); return result;
}
static bool equal(json_object *a, const char *key, json_object *b, const char *other) {
    const char *left = f_string(a, key), *right = f_string(b, other);
    return left && right && !strcmp(left, right);
}
static bool scalar(json_object *files, const char *path, const char *value) {
    json_object *entry = file(files, path); const char *hex = f_string(entry, "hex");
    const char *digits = "0123456789abcdef"; size_t i, n = strlen(value);
    if (!hex || strlen(hex) != (n + 1) * 2) return false;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)value[i];
        if (hex[i*2] != digits[c >> 4] || hex[i*2+1] != digits[c & 15]) return false;
    }
    return !strcmp(hex + n*2, "0a");
}
/* Checksums preserve bytes; these checks bind the claimed successful policy to
 * its existing engine evidence instead of accepting an empty evidence array. */
int task_result_bindings(json_object *result, const char *scratch) {
    json_object *state = f_field(f_field(result, "receipt"), "runtime"), *work = f_field(f_field(result, "spec"), "work");
    json_object *files = f_field(result, "evidence"), *heads = f_field(result, "heads");
    const char *run = f_string(state, "run_id");
    if (!equal(state, "work_kind", work, "kind")) return -1;
    if (!strcmp(f_string(work, "kind"), "exec")) {
        json_object *head = NULL, *provenance = decode(files, "provenance.json", scratch);
        json_object *attempt = decode(files, "attempt.json", scratch), *data = f_field(attempt, "data"), *rows = f_field(data, "results");
        size_t i; bool valid;
        for (i = 0; i < json_object_array_length(heads); i++) {
            json_object *candidate = json_object_array_get_idx(heads, i);
            if (equal(candidate, "head_id", state, "execution_head_id")) head = candidate;
        }
        valid = head && f_number_is(provenance, "schema_version", 1) &&
            equal(f_field(provenance, "data"), "head_id", head, "head_id") && equal(f_field(provenance, "data"), "branch", head, "branch") && equal(f_field(provenance, "data"), "instance_id", head, "instance_id") &&
            equal(state, "execution_head_id", head, "head_id") && equal(state, "execution_project_id", f_field(provenance, "data"), "project_id");
        if (!strcmp(f_string(state, "state"), "succeeded")) {
            valid = valid && f_number_is(attempt, "schema_version", 1) && equal(data, "run_id", state, "run_id") &&
                json_object_is_type(rows, json_type_array) && json_object_array_length(rows) == 1 &&
                equal(json_object_array_get_idx(rows, 0), "head_id", head, "head_id") && f_number_is(json_object_array_get_idx(rows, 0), "exit_code", 0);
        }
        json_object_put(provenance); json_object_put(attempt); return valid ? 0 : -1;
    } else {
        char path[F_PATH];
        if (snprintf(path, sizeof(path), "workflows/runs/%s/run-id", run) >= (int)sizeof(path) || !scalar(files, path, run)) return -1;
        if (snprintf(path, sizeof(path), "workflows/runs/%s/graph.tsv", run) >= (int)sizeof(path) || !file(files, path)) return -1;
        if (!strcmp(f_string(state, "state"), "succeeded")) {
            if (snprintf(path, sizeof(path), "workflows/runs/%s/state", run) >= (int)sizeof(path) || !scalar(files, path, "succeeded")) return -1;
        }
    }
    return 0;
}
