#define _XOPEN_SOURCE 700
#include "fleet/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *f_home, *f_hydra;
static void rejected(json_object *envelope, const char *scratch) {
    char hash[65]; json_object *checked;
    /* Recompute the outer checksum, so each probe reaches the inner boundary. */
    assert(!task_json_hash(f_field(envelope, "result"), scratch, hash));
    f_string_add(envelope, "result_sha256", hash);
    checked = task_result_verify(envelope);
    assert(!json_object_get_boolean(f_field(checked, "ok")));
    assert(!strcmp(f_string(f_field(checked, "error"), "code"), "invalid_result"));
    json_object_put(checked); json_object_put(envelope);
}
static json_object *copy(json_object *original) {
    return f_parse(json_object_to_json_string_ext(original, JSON_C_TO_STRING_PLAIN));
}
int main(int argc, char **argv) {
    char scratch[] = "/tmp/hydra-result-probes.XXXXXX";
    json_object *original, *envelope, *result, *file, *checked;
    assert(argc == 2 && mkdtemp(scratch));
    original = f_read_json(argv[1], TASK_PACKAGE_LIMIT + 1024); assert(original);
    checked = task_result_verify(original); assert(json_object_get_boolean(f_field(checked, "ok"))); json_object_put(checked);
    envelope = copy(original); result = f_field(envelope, "result");
    file = json_object_array_get_idx(f_field(result, "artifacts"), 0); assert(file);
    f_string_add(file, "path", "../escape"); rejected(envelope, scratch);
    envelope = copy(original); result = f_field(envelope, "result");
    file = json_object_array_get_idx(f_field(result, "artifacts"), 0);
    f_string_add(file, "sha256", "0000000000000000000000000000000000000000000000000000000000000000"); rejected(envelope, scratch);
    envelope = copy(original); result = f_field(envelope, "result");
    f_string_add(result, "bundle_sha256", "0000000000000000000000000000000000000000000000000000000000000000"); rejected(envelope, scratch);
    envelope = copy(original); result = f_field(envelope, "result");
    file = json_object_array_get_idx(f_field(result, "heads"), 0);
    f_string_add(file, "bundle_ref", "refs/heads/main"); rejected(envelope, scratch);
    envelope = copy(original); result = f_field(envelope, "result");
    file = json_object_array_get_idx(f_field(result, "evidence"), 0); assert(file);
    f_string_add(file, "path", ".git/config"); rejected(envelope, scratch);
    envelope = copy(original); result = f_field(envelope, "result");
    json_object_object_add(result, "evidence", json_object_new_array()); rejected(envelope, scratch);
    json_object_put(original); assert(!f_remove_tree(scratch));
    puts("Result probes: valid recorded evidence, unsafe paths, file/bundle digest mismatch, and foreign refs passed");
    return 0;
}
