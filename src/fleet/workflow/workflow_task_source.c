#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>

static json_object *record(const char *directory, const char *name) {
    char path[F_PATH];
    return f_path(path, sizeof(path), directory, name) ? NULL : f_read_json(path, F_LIMIT);
}
static bool same_receipt(json_object *left, json_object *right) {
    const char *fields[] = {"task_id", "spec_sha256", "submission_key"};
    for (size_t i = 0; i < 3; i++) {
        const char *a = f_string(left, fields[i]), *b = f_string(right, fields[i]);
        if (!a || !b || strcmp(a, b)) return false;
    }
    return true;
}
json_object *wt_source_binding(const char *run, json_object *bindings, json_object *binding) {
    const char *producer = f_string(binding, "source_step"); char directory[F_PATH], attempt[F_PATH];
    json_object *envelope = NULL, *receipt = NULL, *collection = NULL, *verified = NULL, *stored = NULL, *out = NULL;
    if (!wd_name(producer) || !f_field(f_field(bindings, "steps"), producer) || wd_producer_directory(run, producer, attempt) ||
        f_path(directory, sizeof(directory), attempt, "remote")) goto done;
    envelope = record(directory, "result.json"); receipt = record(directory, "receipt.json"); collection = record(directory, "collection.json");
    if (!wt_parent_receipt(run, producer, attempt, f_field(f_field(bindings, "steps"), producer), receipt)) goto done;
    verified = task_result_verify(envelope);
    if (!json_object_get_boolean(f_field(verified, "ok")) || !same_receipt(receipt, f_field(f_field(envelope, "result"), "receipt"))) goto done;
    const char *id = f_string(f_field(collection, "data"), "collection_id");
    if (!id) goto done;
    stored = task_collected(f_string(bindings, "source"), id);
    const char *digest = f_string(f_field(stored, "data"), "result_sha256");
    if (!json_object_get_boolean(f_field(stored, "ok")) || !digest || strcmp(digest, f_string(envelope, "result_sha256"))) goto done;
    json_object *heads = f_field(f_field(envelope, "result"), "heads");
    if (json_object_array_length(heads) != 1) goto done;
    json_object *head = json_object_array_get_idx(heads, 0);
    if (json_object_get_boolean(f_field(head, "dirty"))) goto done;
    out = json_object_new_object(); f_string_add(out, "step", producer); f_string_add(out, "collection_id", id);
    f_string_add(out, "task_id", f_string(receipt, "task_id")); f_string_add(out, "result_sha256", digest);
    f_string_add(out, "head_id", f_string(head, "head_id")); f_string_add(out, "commit", f_string(head, "commit"));
done:
    json_object_put(envelope); json_object_put(receipt); json_object_put(collection); json_object_put(verified); json_object_put(stored); return out;
}
