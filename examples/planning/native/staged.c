#include "precompile.h"
#include <stdlib.h>
#include <string.h>

/* Returned result borrows no subprocess buffers; caller releases the object. */
static json_object *accepted_result(const char *run_id) {
    const char *hydra = getenv("HYDRA_BIN");
    char *argv[] = {(char *)(hydra && *hydra ? hydra : "hydra"), "workflow", "plan", "result", (char *)run_id, NULL};
    struct f_capture capture = {0};
    json_object *envelope = NULL, *result = NULL;
    if (f_run(argv, NULL, 0, 30, &capture) || capture.status) {
        precompile_error("public workflow result refused or unavailable"); goto done;
    }
    if (!capture.out || capture.out_bytes != strlen(capture.out) || !plan_json_unique(capture.out) ||
        !(envelope = f_parse(capture.out)) || !json_object_is_type(f_field(envelope, "data"), json_type_object)) {
        precompile_error("public workflow result malformed"); goto done;
    }
    result = f_field(envelope, "data");
    if (json_object_object_length(result) != 5 || !f_field(result, "plan_sha256") ||
        !f_number_is(result, "schema_version", 1) || !precompile_text_is(result, "verdict", "pass") ||
        !json_object_is_type(f_field(result, "deliverables"), json_type_object) ||
        !json_object_is_type(f_field(result, "checks"), json_type_object)) {
        precompile_error("workflow result is not an accepted passing delivery"); result = NULL; goto done;
    }
    result = json_object_get(result);
done:
    json_object_put(envelope); f_capture_free(&capture);
    return result;
}
static bool same_value(json_object *left, json_object *right) {
    json_object *a = plan_canonical(left), *b = plan_canonical(right);
    bool same = a && b && !strcmp(json_object_to_json_string_ext(a, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE),
                                  json_object_to_json_string_ext(b, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE));
    json_object_put(a); json_object_put(b);
    return same;
}
static int finding_semantics(json_object *finding, const struct manifest *manifest) {
    json_object *selected = json_object_new_array(), *results = json_object_new_array();
    json_object *items = f_field(manifest->value, "items");
    char hash[65];
    const char *error = "finding schema";
    size_t i;
    int status = -1;
    const char *keys[] = {"limitations", "result_sha256", "results", "schema_version", "selected_ids", "source_path", "source_sha256", "status"};
    if (!json_object_is_type(finding, json_type_object) || json_object_object_length(finding) != 8) goto done;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        json_object *field;
        if (!json_object_object_get_ex(finding, keys[i], &field)) goto done;
    }
    error = "finding status/schema";
    if (!f_number_is(finding, "schema_version", 3) || !precompile_text_is(finding, "status", "pass")) goto done;
    error = "stale or changed finding source";
    if (!precompile_text_is(finding, "source_path", "manifest.json") ||
        precompile_hash(manifest->raw, manifest->size, hash) || !precompile_text_is(finding, "source_sha256", hash)) goto done;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *row;
        int value = json_object_get_int(f_field(item, "value"));
        if (!json_object_get_boolean(f_field(item, "enabled"))) continue;
        json_object_array_add(selected, json_object_get(f_field(item, "id")));
        row = json_object_new_object();
        f_string_add(row, "id", f_string(item, "id"));
        json_object_object_add(row, "value", json_object_new_int(value));
        json_object_object_add(row, "square", json_object_new_int(value * value));
        json_object_array_add(results, row);
    }
    error = "finding selection mismatch";
    if (!same_value(f_field(finding, "selected_ids"), selected)) goto done;
    error = "finding results mismatch";
    if (!same_value(f_field(finding, "results"), results)) goto done;
    error = "finding result hash mismatch";
    if (plan_digest(results, hash) || !precompile_text_is(finding, "result_sha256", hash)) goto done;
    status = 0;
done:
    json_object_put(selected); json_object_put(results);
    return status ? precompile_error(error) : 0;
}
int staged_validate(const struct manifest *manifest, const char *run_id) {
    json_object *result = accepted_result(run_id), *finding = NULL, *local = NULL;
    json_object *delivery, *check;
    char *bytes = NULL, *local_bytes = NULL, hash[65];
    size_t size = 0, local_size = 0;
    const char *path, *error = "workflow result lacks passing stage-1 check";
    int status = -1;
    if (!result) return -1;
    delivery = f_field(f_field(result, "deliverables"), "report");
    check = f_field(f_field(result, "checks"), "check");
    if (!json_object_is_type(delivery, json_type_object) || !precompile_text_is(check, "verdict", "pass")) goto done;
    error = "workflow result finding path unavailable";
    path = f_string(delivery, "path");
    if (!path || !precompile_text_is(delivery, "type", "file")) goto done;
    error = "invalid, duplicate or oversized finding (maximum 8192 bytes)";
    finding = precompile_read(path, 8192, &bytes, &size);
    if (!finding) goto done;
    error = "workflow result finding hash mismatch";
    if (precompile_hash(bytes, size, hash) || !precompile_text_is(delivery, "sha256", hash)) goto done;
    local = precompile_read("finding.json", 8192, &local_bytes, &local_size);
    error = "local finding does not match accepted stage-1 bytes";
    if (!local || size != local_size || memcmp(bytes, local_bytes, size)) goto done;
    status = finding_semantics(finding, manifest);
    error = NULL;
done:
    if (status && error) precompile_error(error);
    free(bytes); free(local_bytes);
    json_object_put(local); json_object_put(finding); json_object_put(result);
    return status;
}
