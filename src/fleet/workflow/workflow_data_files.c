#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/task/task.h"
#include "fleet/plan/plan.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Called only on a private snapshot produced by task_file_copy. */
json_object *wd_file(const char *path, json_object *declaration) {
    const char *type = f_string(declaration, "type"), *expected = f_string(declaration, "sha256");
    int64_t limit = json_object_get_int64(f_field(declaration, "max_bytes"));
    struct stat st; char digest[65], *bytes = NULL; json_object *parsed = NULL, *result = NULL, *value;
    bool valid = false;
    if (!type || lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > limit ||
        f_hash(path, digest) || (expected && strcmp(expected, digest))) goto done;
    if (strcmp(type, "file")) {
        size_t length = (size_t)st.st_size;
        bytes = f_read(path, WD_LIMIT);
        if (!bytes || strlen(bytes) != length || !(parsed = f_parse_value(bytes))) goto done;
        value = parsed;
        if (!strcmp(type, "object")) valid = json_object_is_type(value, json_type_object);
        else if (!strcmp(type, "array")) valid = json_object_is_type(value, json_type_array);
        else if (!strcmp(type, "string")) valid = json_object_is_type(value, json_type_string);
        else if (!strcmp(type, "boolean")) valid = json_object_is_type(value, json_type_boolean);
        else if (!strcmp(type, "number")) valid = json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double);
        if (!valid) goto done;
    }
    result = json_object_new_object(); f_string_add(result, "type", type); f_string_add(result, "sha256", digest);
    json_object_object_add(result, "bytes", json_object_new_int64(st.st_size));
done:
    free(bytes); json_object_put(parsed); return result;
}
static json_object *receipt_new(void) {
    json_object *receipt = json_object_new_object();
    json_object_object_add(receipt, "schema_version", json_object_new_int(1));
    json_object_object_add(receipt, "files", json_object_new_object()); return receipt;
}
static int snapshot(json_object *declarations, const char *source, const char *destination, const char *receipt_name) {
    char directory[F_PATH], path[F_PATH]; json_object *receipt = receipt_new(); int status = -1;
    if (f_path(directory, sizeof(directory), destination, "artifacts") || mkdir(directory, 0700)) goto done;
    if (declarations) {
        json_object_object_foreach(declarations, name, declaration) {
            json_object *file;
            const char *origin = source;
            if (f_string(declaration, "source") && !strcmp(f_string(declaration, "source"), "task")) origin = getenv("HYDRA_TASK_INPUT_DIR");
            if (!origin || !*origin) goto done;
            if (f_path(path, sizeof(path), directory, name) || task_file_copy(origin, f_string(declaration, "path"), path) ||
                !(file = wd_file(path, declaration))) goto done;
            json_object_object_add(f_field(receipt, "files"), name, file);
        }
    }
    status = task_write_json(destination, receipt_name, receipt, false);
done:
    json_object_put(receipt); return status;
}
int wd_initialize(json_object *manifest, const char *source, const char *run) {
    return snapshot(f_field(manifest, "inputs"), source, run, "inputs.json");
}
int wd_seal(json_object *manifest, const char *step, const char *attempt) {
    char source[F_PATH]; json_object *declarations = f_field(f_field(f_field(manifest, "steps"), step), "outputs");
    if (f_path(source, sizeof(source), attempt, "outputs")) return -1;
    return snapshot(declarations, source, attempt, "outputs.json");
}
static bool same_file(json_object *a, json_object *b) {
    const char *digest = f_string(a, "sha256"), *type = f_string(a, "type");
    return digest && type && f_string(b, "sha256") && f_string(b, "type") &&
        !strcmp(digest, f_string(b, "sha256")) && !strcmp(type, f_string(b, "type")) &&
        json_object_is_type(f_field(a, "bytes"), json_type_int) &&
        json_object_get_int64(f_field(a, "bytes")) == json_object_get_int64(f_field(b, "bytes"));
}
/* Verify a copied snapshot against both the declaration and its sealed receipt. */
static int receive(const char *source, const char *receipt_name, const char *name, json_object *declaration, const char *destination) {
    char relative[128]; json_object *receipt = task_read_record(source, receipt_name), *file = NULL; int status = -1;
    if (!receipt || !f_number_is(receipt, "schema_version", 1) || snprintf(relative, sizeof(relative), "artifacts/%s", name) >= (int)sizeof(relative) ||
        task_file_copy(source, relative, destination) || !(file = wd_file(destination, declaration)) ||
        !same_file(f_field(f_field(receipt, "files"), name), file)) goto done;
    status = 0;
done:
    json_object_put(receipt); json_object_put(file); return status;
}
static bool valid_attempt(const char *attempt) {
    if (!*attempt || strlen(attempt) > 2) return false;
    for (const char *p = attempt; *p; p++) if (*p < '0' || *p > '9') return false;
    return atoi(attempt) >= 1 && atoi(attempt) <= 11;
}
int wd_producer_directory(const char *run, const char *step, char directory[F_PATH]) {
    char path[F_PATH], *attempt = NULL, *state = NULL, *p; int status = -1;
    if (snprintf(path, sizeof(path), "%s/steps/%s/state", run, step) >= (int)sizeof(path)) goto done;
    state = f_read(path, 64);
    if (!state || strcmp(state, "succeeded\n")) goto done;
    if (snprintf(path, sizeof(path), "%s/steps/%s/authoritative-attempt", run, step) >= (int)sizeof(path)) goto done;
    attempt = f_read(path, 16);
    if (!attempt) goto done;
    p = strchr(attempt, '\n'); if (p) *p = '\0';
    if (!valid_attempt(attempt) ||
        snprintf(directory, F_PATH, "%s/steps/%s/attempt-%s", run, step, attempt) >= F_PATH) goto done;
    status = 0;
done:
    free(attempt); free(state); return status;
}
static int prepare_input(json_object *manifest, const char *run, const char *step, const char *inputs, const char *name, json_object *reference) {
    const char *input = f_string(reference, "input"), *producer = f_string(reference, "step"), *output = f_string(reference, "output");
    json_object *declaration; char destination[F_PATH], source[F_PATH];
    if (f_path(destination, sizeof(destination), inputs, name)) return -1;
    if (f_field(reference, "repair")) return plan_repair_write(run, inputs, name);
    if (f_field(reference, "validation")) return plan_validation_write(run, step, inputs, name);
    if (input) {
        declaration = f_field(f_field(manifest, "inputs"), input);
        return receive(run, "inputs.json", input, declaration, destination);
    }
    declaration = f_field(f_field(f_field(f_field(manifest, "steps"), producer), "outputs"), output);
    if (wd_producer_directory(run, producer, source)) return -1;
    return receive(source, "outputs.json", output, declaration, destination);
}
int wd_prepare(json_object *manifest, const char *run, const char *step, const char *attempt) {
    char inputs[F_PATH], outputs[F_PATH];
    json_object *refs = f_field(f_field(f_field(manifest, "steps"), step), "inputs");
    if (f_path(inputs, sizeof(inputs), attempt, "inputs") || f_path(outputs, sizeof(outputs), attempt, "outputs") ||
        mkdir(inputs, 0700) || mkdir(outputs, 0700)) return -1;
    if (!refs) return 0;
    json_object_object_foreach(refs, name, reference) {
        if (prepare_input(manifest, run, step, inputs, name, reference)) return -1;
    }
    return 0;
}
static int verify_map(json_object *map, const char *source, const char *receipt, const char *scratch) {
    char destination[F_PATH];
    if (!map) return 0;
    if (f_path(destination, sizeof(destination), scratch, "file")) return -1;
    json_object_object_foreach(map, name, declaration) {
        if (receive(source, receipt, name, declaration, destination)) return -1;
        if (unlink(destination)) return -1;
    }
    return 0;
}
int wd_verify_output(json_object *manifest, const char *step, const char *attempt) {
    char scratch[] = "/tmp/hydra-workflow-output.XXXXXX"; int status;
    if (!mkdtemp(scratch)) return -1;
    status = verify_map(f_field(f_field(f_field(manifest, "steps"), step), "outputs"), attempt, "outputs.json", scratch);
    f_remove_tree(scratch); return status;
}
int wd_verify(json_object *manifest, const char *run) {
    char scratch[] = "/tmp/hydra-workflow-verify.XXXXXX", directory[F_PATH], path[F_PATH]; int status = -1;
    json_object *steps = f_field(manifest, "steps");
    if (!mkdtemp(scratch)) return -1;
    if (verify_map(f_field(manifest, "inputs"), run, "inputs.json", scratch)) goto done;
    json_object_object_foreach(steps, step, value) {
        char *state;
        if (snprintf(path, sizeof(path), "%s/steps/%s/state", run, step) >= (int)sizeof(path)) goto done;
        state = f_read(path, 64);
        if (!state) goto done;
        if (!strcmp(state, "succeeded\n") && f_field(value, "outputs")) {
            free(state);
            if (wd_producer_directory(run, step, directory) || verify_map(f_field(value, "outputs"), directory, "outputs.json", scratch)) goto done;
        } else free(state);
    }
    status = 0;
done:
    f_remove_tree(scratch); return status;
}
