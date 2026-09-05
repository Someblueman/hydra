#define _XOPEN_SOURCE 700
#include "task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int command(const char *root, char *const args[]) {
    struct f_capture cap = {0}; int status = task_git(root, args, &cap); f_capture_free(&cap); return status;
}
static int artifacts(json_object *files, json_object *heads, json_object *spec, const char *scratch) {
    json_object *outputs = f_field(spec, "outputs"); size_t i, j, remaining = (size_t)json_object_get_int(f_field(f_field(spec, "limits"), "artifact_bytes"));
    for (i = 0; i < json_object_array_length(outputs); i++) {
        const char *relative = task_text(json_object_array_get_idx(outputs, i)); json_object *producer = NULL;
        for (j = 0; j < json_object_array_length(heads); j++) {
            json_object *head = json_object_array_get_idx(heads, j); char path[F_PATH]; struct stat st;
            if (f_path(path, sizeof(path), f_string(head, "workspace"), relative)) return -1;
            if (!lstat(path, &st)) { if (producer) return -1; producer = head; }
            else if (errno != ENOENT) return -1;
        }
        if (!producer || task_result_files(files, f_string(producer, "workspace"), relative, scratch, &remaining)) return -1;
        f_string_add(json_object_array_get_idx(files, i), "head_id", f_string(producer, "head_id"));
    }
    return 0;
}
static json_object *snapshot(const char *directory, json_object *receipt, const char *scratch, const char **failure) {
    char path[F_PATH], bundle[F_PATH], hash[65]; json_object *package = NULL, *result = NULL, *heads, *files;
    json_object *state = f_field(receipt, "runtime"), *spec = NULL; char *hex = NULL; const char *source; size_t i;
    if (f_path(path, sizeof(path), directory, "package.json") || !(package = f_read_json(path, TASK_PACKAGE_LIMIT))) goto done;
    spec = task_spec(f_field(package, "spec"), true);
    if (!spec) goto done;
    source = f_string(f_field(spec, "source"), "commit");
    if (task_json_hash(spec, scratch, hash) || strcmp(hash, f_string(receipt, "spec_sha256"))) goto done;
    { char *init[] = {"init", "--bare", "--template=", strlen(source) == 64 ? "--object-format=sha256" : "--object-format=sha1", NULL};
      if (command(scratch, init)) goto done; }
    if (f_path(path, sizeof(path), directory, "source.bundle")) goto done;
    { char *fetch[] = {"fetch", "--no-tags", "--no-write-fetch-head", path, "refs/heads/task-source:refs/heads/task-source", NULL}; if (command(scratch, fetch)) goto done; }
    *failure = "heads_unavailable";
    heads = task_result_heads(directory, state, scratch); if (!heads) goto done;
    result = json_object_new_object(); json_object_object_add(result, "schema_version", json_object_new_int(1));
    json_object_object_add(result, "receipt", f_parse(json_object_to_json_string_ext(receipt, JSON_C_TO_STRING_PLAIN))); json_object_object_add(result, "spec", json_object_get(spec));
    json_object_object_del(f_field(f_field(result, "receipt"), "runtime"), "result_state");
    json_object_object_add(result, "heads", heads);
    files = json_object_new_array(); json_object_object_add(result, "artifacts", files);
    *failure = "artifacts_unavailable";
    if (artifacts(files, heads, spec, scratch)) goto bad;
    files = json_object_new_array(); json_object_object_add(result, "evidence", files);
    *failure = "evidence_unavailable";
    if (task_result_evidence(files, directory, state, heads, scratch)) goto bad;
    *failure = "result_bundle_unavailable";
    for (i = 0; i < json_object_array_length(heads); i++) {
        json_object *head = json_object_array_get_idx(heads, i);
        char *ancestor[] = {"merge-base", "--is-ancestor", (char *)source, (char *)f_string(head, "commit"), NULL};
        if (command(scratch, ancestor)) goto bad;
        json_object_object_del(head, "workspace");
    }
    if (f_path(bundle, sizeof(bundle), scratch, "result.bundle")) goto bad;
    { char *create[] = {"bundle", "create", bundle, "--branches", NULL}; if (command(scratch, create)) goto bad; }
    if (f_hash(bundle, hash) || !(hex = f_hex_read(bundle))) goto bad;
    f_string_add(result, "bundle_sha256", hash); f_string_add(result, "bundle_hex", hex);
    if (strlen(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PLAIN)) > TASK_PACKAGE_LIMIT) goto bad;
    goto done;
bad:
    json_object_put(result); result = NULL;
done:
    free(hex); json_object_put(spec); json_object_put(package); return result;
}
int task_result_seal(const char *directory, json_object *state) {
    json_object *receipt = task_read_record(directory, "acceptance.json"), *result = NULL, *envelope = NULL, *checked = NULL;
    char scratch[] = "/tmp/hydra-task-result.XXXXXX", hash[65]; int status = -1; const char *failure = "source_unavailable";
    if (!receipt || !mkdtemp(scratch)) { json_object_put(receipt); return -1; }
    json_object_object_add(receipt, "runtime", json_object_get(state));
    result = snapshot(directory, receipt, scratch, &failure);
    if (!result || task_json_hash(result, scratch, hash)) goto done;
    envelope = json_object_new_object(); json_object_object_add(envelope, "schema_version", json_object_new_int(1));
    f_string_add(envelope, "result_sha256", hash); json_object_object_add(envelope, "result", json_object_get(result));
    failure = "result_validation_failed"; checked = task_result_verify(envelope);
    if (!json_object_get_boolean(f_field(checked, "ok")) || task_write_json(directory, "result.json", envelope, false)) goto done;
    status = 0;
done:
    if (status) f_string_add(state, "result_error", failure);
    f_remove_tree(scratch); json_object_put(receipt); json_object_put(result); json_object_put(envelope); json_object_put(checked); return status;
}
json_object *task_result(const char *id) {
    json_object *response = task_status(id), *receipt = f_field(response, "data"), *envelope = NULL, *checked = NULL;
    char root[F_PATH], directory[F_PATH], path[F_PATH]; const char *bound_id, *bound_digest;
    if (!json_object_get_boolean(f_field(response, "ok"))) return response;
    if (task_store_root(root) || f_path(directory, sizeof(directory), root, id) || f_path(path, sizeof(path), directory, "result.json") ||
        !(envelope = f_read_json(path, TASK_PACKAGE_LIMIT + 1024))) goto bad;
    checked = task_result_verify(envelope);
    bound_id = f_string(f_field(f_field(envelope, "result"), "receipt"), "task_id");
    bound_digest = f_string(f_field(f_field(envelope, "result"), "receipt"), "spec_sha256");
    if (!json_object_get_boolean(f_field(checked, "ok")) || !bound_id || strcmp(bound_id, id) || !bound_digest || strcmp(bound_digest, f_string(receipt, "spec_sha256"))) goto bad;
    json_object_object_add(receipt, "collection", json_object_get(envelope)); goto done;
bad:
    json_object_put(response); response = f_error("fleet-task-result", "result_unavailable", "no verified receiver-completion snapshot is available; inspect result_state; downloads never recapture changed work");
done:
    json_object_put(envelope); json_object_put(checked); return response;
}
