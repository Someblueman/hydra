#define _XOPEN_SOURCE 700
#include "task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int collection_id(json_object *envelope, const char *scratch, char id[80]) {
    json_object *binding = json_object_new_object(), *result = f_field(envelope, "result"), *receipt = f_field(result, "receipt"); char hash[65]; int status;
    f_string_add(binding, "host", f_string(f_field(result, "spec"), "host"));
    f_string_add(binding, "task_id", f_string(receipt, "task_id")); f_string_add(binding, "spec_sha256", f_string(receipt, "spec_sha256"));
    status = task_json_hash(binding, scratch, hash); json_object_put(binding);
    if (!status) snprintf(id, 80, "collection_%s", hash);
    return status;
}
static int materialized(const char *directory, const char *category, json_object *files, const char *scratch, bool install) {
    size_t i;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); char relative[F_PATH], path[F_PATH], copy[F_PATH], hash[65]; struct stat st;
        if (f_path(relative, sizeof(relative), category, f_string(file, "path")) || f_path(path, sizeof(path), directory, relative)) return -1;
        if (lstat(path, &st)) {
            if (!install || errno != ENOENT || task_collection_file(directory, relative, f_string(file, "hex"))) return -1;
        }
        if (f_path(copy, sizeof(copy), scratch, "copied-file") || task_file_copy(directory, relative, copy)) return -1;
        int status = f_hash(copy, hash); unlink(copy);
        if (status || strcmp(hash, f_string(file, "sha256"))) return -1;
    }
    return 0;
}
static json_object *summary(const char *directory, const char *id, json_object *envelope) {
    json_object *out = json_object_new_object(), *refs = json_object_new_array(), *result = f_field(envelope, "result"), *heads = f_field(result, "heads");
    char ref[256]; size_t i;
    f_string_add(out, "collection_id", id); f_string_add(out, "path", directory);
    f_string_add(out, "result_sha256", f_string(envelope, "result_sha256"));
    json_object_object_add(out, "receipt", json_object_get(f_field(result, "receipt")));
    snprintf(ref, sizeof(ref), "refs/hydra/tasks/%s/source", id); f_string_add(out, "source_ref", ref);
    for (i = 0; i < json_object_array_length(heads); i++) {
        json_object *head = json_object_array_get_idx(heads, i), *entry = f_parse(json_object_to_json_string_ext(head, JSON_C_TO_STRING_PLAIN));
        snprintf(ref, sizeof(ref), "refs/hydra/tasks/%s/%s", id, f_string(head, "head_id")); f_string_add(entry, "ref", ref); json_object_array_add(refs, entry);
    }
    f_string_add(out, "source_commit", f_string(f_field(f_field(result, "spec"), "source"), "commit"));
    json_object_object_add(out, "heads", refs); return f_success("fleet-task-collected", out);
}
json_object *task_collected(const char *project, const char *id) {
    char root[F_PATH], directory[F_PATH], path[F_PATH], scratch[] = "/tmp/hydra-collected.XXXXXX", expected[80];
    json_object *envelope = NULL, *checked = NULL, *out = NULL; struct stat st; bool staged = false;
    if (!id || strncmp(id, "collection_", 11) || !task_hex(id + 11, 64) || task_collection_root(project, root, false) ||
        f_path(directory, sizeof(directory), root, id) || lstat(directory, &st) || !S_ISDIR(st.st_mode) ||
        f_path(path, sizeof(path), directory, "result.json") || !(envelope = f_read_json(path, TASK_PACKAGE_LIMIT + 1024))) goto done;
    checked = task_result_verify(envelope); if (!json_object_get_boolean(f_field(checked, "ok")) || !mkdtemp(scratch)) goto done;
    staged = true;
    if (collection_id(envelope, scratch, expected) || strcmp(expected, id) ||
        materialized(directory, "artifacts", f_field(f_field(envelope, "result"), "artifacts"), scratch, false) ||
        materialized(directory, "evidence", f_field(f_field(envelope, "result"), "evidence"), scratch, false) || task_collection_refs(project, id, f_field(envelope, "result"), NULL, false)) goto done;
    out = summary(directory, id, envelope);
done:
    if (staged) f_remove_tree(scratch);
    json_object_put(envelope); json_object_put(checked);
    return out ? out : f_error("fleet-task-collected", "invalid_collection", "collection bindings or isolated refs are missing or changed");
}
json_object *task_collect(const char *project, json_object *envelope) {
    json_object *checked = task_result_verify(envelope), *out = NULL, *existing = NULL; const char *digest = f_string(envelope, "result_sha256"), *failure = "storage";
    char root[F_PATH], stage[F_PATH] = "", directory[F_PATH], path[F_PATH], id[80]; struct stat st;
    if (!json_object_get_boolean(f_field(checked, "ok"))) { out = checked; checked = NULL; goto done; }
    if (task_collection_root(project, root, true) || snprintf(stage, sizeof(stage), "%s/.collect.XXXXXX", root) >= (int)sizeof(stage) || !mkdtemp(stage)) { stage[0] = '\0'; goto done; }
    failure = "snapshot_binding";
    if (collection_id(envelope, stage, id) || f_path(directory, sizeof(directory), root, id)) goto done;
    if (!lstat(directory, &st)) {
        if (!S_ISDIR(st.st_mode) || f_path(path, sizeof(path), directory, "result.json") || !(existing = f_read_json(path, TASK_PACKAGE_LIMIT + 1024)) ||
            !f_string(existing, "result_sha256") || strcmp(f_string(existing, "result_sha256"), digest)) goto done;
        json_object *valid = task_result_verify(existing); bool matches = json_object_get_boolean(f_field(valid, "ok")); json_object_put(valid); if (!matches) goto done;
    } else {
        if (errno != ENOENT || materialized(stage, "artifacts", f_field(f_field(envelope, "result"), "artifacts"), stage, true) ||
            materialized(stage, "evidence", f_field(f_field(envelope, "result"), "evidence"), stage, true) || task_write_json(stage, "result.json", envelope, false)) goto done;
        if (rename(stage, directory)) goto done;
        stage[0] = '\0'; if (task_sync_dir(root)) goto done;
    }
    failure = "materialized_files";
    if (!stage[0]) {
        if (snprintf(stage, sizeof(stage), "%s/.collect.XXXXXX", root) >= (int)sizeof(stage) || !mkdtemp(stage)) { stage[0] = '\0'; goto done; }
    }
    if (materialized(directory, "artifacts", f_field(f_field(envelope, "result"), "artifacts"), stage, true) ||
        materialized(directory, "evidence", f_field(f_field(envelope, "result"), "evidence"), stage, true)) goto done;
    /* Object import does not touch HEAD, the index, FETCH_HEAD, or working files.
     * A compare-and-set ref transaction refuses concurrent ref movement. */
    failure = "bundle_file";
    if (f_path(path, sizeof(path), directory, "source-results.bundle")) goto done;
    if (lstat(path, &st)) {
        if (errno != ENOENT || task_collection_file(directory, "source-results.bundle", f_string(f_field(envelope, "result"), "bundle_hex")) || task_sync_dir(directory)) goto done;
    } else {
        char hash[65]; if (!S_ISREG(st.st_mode) || f_hash(path, hash) || strcmp(hash, f_string(f_field(envelope, "result"), "bundle_sha256"))) goto done;
    }
    failure = "isolated_refs";
    if (task_collection_refs(project, id, f_field(envelope, "result"), path, true)) goto done;
    out = task_collected(project, id);
done:
    if (stage[0]) f_remove_tree(stage);
    json_object_put(checked); json_object_put(existing);
    if (!out) { out = f_error("fleet-task-collect", "collection_conflict", "cannot install this verified snapshot into private storage and unchanged isolated refs; checkout files were not modified"); f_string_add(f_field(out, "error"), "stage", failure); }
    return out;
}
