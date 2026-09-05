#define _XOPEN_SOURCE 700
#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool identity(const char *text, const char *prefix) {
    return f_name(text) && strlen(text) < 128 && !strncmp(text, prefix, strlen(prefix));
}
static bool head_exists(json_object *heads, const char *id) {
    size_t i;
    for (i = 0; id && i < json_object_array_length(heads); i++)
        if (!strcmp(f_string(json_object_array_get_idx(heads, i), "head_id"), id)) return true;
    return false;
}
static int heads_valid(json_object *heads, size_t commit_length) {
    const char *const keys[] = {"head_id", "instance_id", "branch", "commit", "bundle_ref", "dirty", NULL}; size_t i, j;
    if (!json_object_is_type(heads, json_type_array) || json_object_array_length(heads) > TASK_FILES) return -1;
    for (i = 0; i < json_object_array_length(heads); i++) {
        json_object *head = json_object_array_get_idx(heads, i); char expected[160];
        const char *id = f_string(head, "head_id"), *branch = f_string(head, "branch"), *ref = f_string(head, "bundle_ref");
        if (!task_keys(head, keys) || !identity(id, "head_") || !identity(f_string(head, "instance_id"), "instance_") ||
            !task_hex(f_string(head, "commit"), commit_length) || !branch || !*branch || strlen(branch) > 1024 ||
            !json_object_is_type(f_field(head, "dirty"), json_type_boolean)) return -1;
        for (j = 0; branch[j]; j++) if ((unsigned char)branch[j] < 32 || (unsigned char)branch[j] == 127) return -1;
        snprintf(expected, sizeof(expected), "refs/heads/%s", id); if (!ref || strcmp(ref, expected)) return -1;
        for (j = 0; j < i; j++) {
            json_object *prior = json_object_array_get_idx(heads, j);
            if (!strcmp(id, f_string(prior, "head_id")) || !strcmp(branch, f_string(prior, "branch"))) return -1;
        }
    }
    return 0;
}
static int files_valid(json_object *files, json_object *outputs, json_object *heads, size_t limit, const char *scratch) {
    const char *const artifact_keys[] = {"path", "hex", "sha256", "bytes", "head_id", NULL};
    const char *const evidence_keys[] = {"path", "hex", "sha256", "bytes", NULL}; size_t i, j;
    if (!json_object_is_type(files, json_type_array) || json_object_array_length(files) > (outputs ? TASK_FILES : 256) ||
        (outputs && json_object_array_length(files) != json_object_array_length(outputs))) return -1;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i), *bytes = f_field(file, "bytes");
        const char *relative = f_string(file, "path"), *hex = f_string(file, "hex"), *digest = f_string(file, "sha256");
        int64_t size = json_object_get_int64(bytes); char path[F_PATH], hash[65]; int status;
        if (!task_keys(file, outputs ? artifact_keys : evidence_keys) || !task_path(relative) || !task_hex(digest, 64) ||
            !json_object_is_type(bytes, json_type_int) || size < 0 || size > (int64_t)limit || !hex || !task_hex(hex, (size_t)size * 2)) return -1;
        if (outputs && (strcmp(relative, task_text(json_object_array_get_idx(outputs, i))) || !head_exists(heads, f_string(file, "head_id")))) return -1;
        for (j = 0; j < i; j++) if (!strcmp(relative, f_string(json_object_array_get_idx(files, j), "path"))) return -1;
        if (f_path(path, sizeof(path), scratch, "file") || f_hex_write(path, hex, 0600)) return -1;
        status = f_hash(path, hash); unlink(path); if (status || strcmp(hash, digest)) return -1;
        limit -= (size_t)size;
    }
    return 0;
}
static int git_ok(const char *root, char *const args[]) {
    struct f_capture cap = {0}; int status = task_git(root, args, &cap); f_capture_free(&cap); return status;
}
static int bundle_valid(json_object *result, const char *source, json_object *heads, const char *scratch) {
    char path[F_PATH], hash[65], expected[128]; struct f_capture cap = {0}; size_t i, count = 0; int status = -1; char *line;
    char *init[] = {"init", "--bare", "--template=", strlen(source) == 64 ? "--object-format=sha256" : "--object-format=sha1", NULL};
    char *verify[] = {"bundle", "verify", "result.bundle", NULL}, *list[] = {"bundle", "list-heads", "result.bundle", NULL};
    char *fetch[] = {"fetch", "--no-tags", "--no-write-fetch-head", "result.bundle", "+refs/heads/*:refs/heads/*", NULL};
    if (f_path(path, sizeof(path), scratch, "result.bundle") || f_hex_write(path, f_string(result, "bundle_hex"), 0600) ||
        f_hash(path, hash) || strcmp(hash, f_string(result, "bundle_sha256")) || git_ok(scratch, init) || git_ok(scratch, verify) || task_git(scratch, list, &cap)) goto done;
    snprintf(expected, sizeof(expected), "%s refs/heads/task-source", source);
    for (line = cap.out; line && *line;) {
        char *end = strchr(line, '\n'); bool matched = false;
        if (!end) goto done;
        *end = '\0';
        if (!strcmp(line, expected)) matched = true;
        for (i = 0; !matched && i < json_object_array_length(heads); i++) {
            json_object *head = json_object_array_get_idx(heads, i); char binding[256];
            snprintf(binding, sizeof(binding), "%s %s", f_string(head, "commit"), f_string(head, "bundle_ref"));
            if (!strcmp(line, binding)) matched = true;
        }
        if (!matched) goto done;
        count++; line = end + 1;
    }
    if (count != json_object_array_length(heads) + 1 || git_ok(scratch, fetch)) goto done;
    for (i = 0; i < json_object_array_length(heads); i++) {
        json_object *head = json_object_array_get_idx(heads, i);
        char *ancestor[] = {"merge-base", "--is-ancestor", (char *)source, (char *)f_string(head, "commit"), NULL};
        char *resolve[] = {"rev-parse", "--verify", (char *)f_string(head, "bundle_ref"), NULL};
        f_capture_free(&cap);
        if (git_ok(scratch, ancestor) || task_git(scratch, resolve, &cap)) goto done;
        cap.out[strcspn(cap.out, "\r\n")] = '\0';
        if (strcmp(cap.out, f_string(head, "commit"))) goto done;
    }
    status = 0;
done:
    f_capture_free(&cap); return status;
}
json_object *task_result_verify(json_object *envelope) {
    const char *const envelope_keys[] = {"schema_version", "result_sha256", "result", NULL};
    const char *const result_keys[] = {"schema_version", "receipt", "spec", "heads", "artifacts", "evidence", "bundle_sha256", "bundle_hex", NULL};
    json_object *result = f_field(envelope, "result"), *receipt = f_field(result, "receipt"), *state = f_field(receipt, "runtime"), *spec = NULL, *out = NULL, *heads = f_field(result, "heads");
    const char *id = f_string(receipt, "task_id"), *current = f_string(state, "state"), *digest = f_string(envelope, "result_sha256");
    char scratch[] = "/tmp/hydra-result-verify.XXXXXX", hash[65]; bool staged = false;
    if (!task_keys(envelope, envelope_keys) || !f_number_is(envelope, "schema_version", 1) || !task_hex(digest, 64) ||
        strlen(json_object_to_json_string_ext(envelope, JSON_C_TO_STRING_PLAIN)) > TASK_PACKAGE_LIMIT + 1024 ||
        !task_keys(result, result_keys) || !f_number_is(result, "schema_version", 1) || !f_number_is(receipt, "schema_version", 1) ||
        !id || strncmp(id, "task_", 5) || !task_hex(id + 5, 64) || !task_hex(f_string(receipt, "spec_sha256"), 64) ||
        !task_runtime_valid(state) || (strcmp(current, "succeeded") && strcmp(current, "failed") && strcmp(current, "cancelled")) ||
        !identity(f_string(state, "run_id"), "run_") || !(spec = task_spec(f_field(result, "spec"), true)) ||
        !task_hex(f_string(result, "bundle_sha256"), 64) || !mkdtemp(scratch)) goto done;
    staged = true;
    if (task_json_hash(result, scratch, hash) || strcmp(hash, digest) || task_json_hash(spec, scratch, hash) || strcmp(hash, f_string(receipt, "spec_sha256")) ||
        heads_valid(heads, strlen(f_string(f_field(spec, "source"), "commit"))) ||
        files_valid(f_field(result, "artifacts"), f_field(spec, "outputs"), heads, (size_t)json_object_get_int(f_field(f_field(spec, "limits"), "artifact_bytes")), scratch) ||
        files_valid(f_field(result, "evidence"), NULL, heads, TASK_FILE_LIMIT, scratch) || task_result_bindings(result, scratch) ||
        bundle_valid(result, f_string(f_field(spec, "source"), "commit"), heads, scratch)) goto done;
    out = f_success("fleet-task-inspect-result", json_object_get(result));
done:
    if (staged) f_remove_tree(scratch);
    json_object_put(spec); return out ? out : f_error("fleet-task-inspect-result", "invalid_result", "result bindings, file checksums/paths, or exact source/result bundle failed verification");
}
