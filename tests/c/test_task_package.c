#include "fleet/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *f_home, *f_hydra;
static json_object *draft(void) {
    return f_parse("{\"schema_version\":1,\"host\":\"build\",\"project\":\"/srv/project\","
        "\"source\":{\"commit\":\"0123456789012345678901234567890123456789\"},"
        "\"work\":{\"kind\":\"exec\",\"argv\":[\"printf\",\"%s\",\"literal ; $(text)\"]},"
        "\"inputs\":[],\"outputs\":[\"result.txt\"],\"capabilities\":[\"exec\"],\"completion\":\"command-exit\","
        "\"limits\":{\"transport_seconds\":30,\"queue_seconds\":60,\"startup_seconds\":60,"
        "\"execution_seconds\":120,\"cancellation_seconds\":10,\"log_bytes\":4096,\"artifact_bytes\":4096}}");
}
static void validation(void) {
    const char *bad[] = {"", "/abs", "a/", "a//b", "a/../b", "../b", ".git/config", ".GIT/config", "a\\b", "a\nb", ".hydra-task/x", NULL};
    json_object *obj = draft(), *copy = task_spec(obj, false), *value; size_t i;
    assert(copy); json_object_put(copy);
    assert(task_path("context/task prompt.txt") && task_path("result.txt"));
    for (i = 0; bad[i]; i++) assert(!task_path(bad[i]));
    f_string_add(obj, "unknown", "field"); assert(!task_spec(obj, false)); json_object_object_del(obj, "unknown");
    f_string_add(obj, "completion", "idle"); assert(!task_spec(obj, false)); f_string_add(obj, "completion", "command-exit");
    value = f_field(obj, "limits"); json_object_object_add(value, "execution_seconds", json_object_new_int64(4294967416LL));
    assert(!task_spec(obj, false)); json_object_object_add(value, "execution_seconds", json_object_new_int(120));
    json_object_object_add(value, "queue_seconds", json_object_new_string("60")); assert(!task_spec(obj, false));
    json_object_object_add(value, "queue_seconds", json_object_new_int(60));
    json_object_array_add(f_field(obj, "outputs"), json_object_new_string("result.txt")); assert(!task_spec(obj, false));
    json_object_put(obj);
    obj = draft(); value = f_field(f_field(obj, "work"), "argv");
    json_object_array_add(value, json_object_new_string_len("a\0b", 3)); assert(!task_spec(obj, false));
    json_object_put(obj);
}
static char *run(char *const argv[]) {
    struct f_capture cap = {0}; char *out;
    assert(!f_run(argv, NULL, 0, 30, &cap) && cap.status == 0);
    out = cap.out; cap.out = NULL; f_capture_free(&cap); return out;
}
static void exact_source(const char *dir) {
    char code[F_PATH], bundle[F_PATH], *out, commit[65]; json_object *spec = draft(), *response, *package, *inspected;
    char *init[] = {"git", "-C", (char *)dir, "init", "--template=", NULL};
    char *add[] = {"git", "-C", (char *)dir, "add", "code", NULL};
    char *save[] = {"git", "-C", (char *)dir, "-c", "user.name=Task", "-c", "user.email=task@example.invalid", "-c", "core.hooksPath=/dev/null", "-c", "commit.gpgSign=false", "commit", "-m", "source", NULL};
    char *head[] = {"git", "-C", (char *)dir, "rev-parse", "HEAD", NULL};
    char *fetch[] = {"git", "-C", (char *)dir, "fetch", bundle, "refs/heads/task-source", NULL};
    char *show[] = {"git", "-C", (char *)dir, "show", "FETCH_HEAD:code", NULL};
    free(run(init)); assert(!f_path(code, sizeof(code), dir, "code"));
    assert(!f_write(code, "committed\n", 10, false)); free(run(add)); free(run(save)); out = run(head);
    out[strcspn(out, "\n")] = '\0'; assert(!f_copy(commit, sizeof(commit), out)); free(out);
    f_string_add(f_field(spec, "source"), "commit", commit);
    assert(!f_write(code, "dirty\n", 6, true));
    response = task_prepare(dir, spec); assert(json_object_get_boolean(f_field(response, "ok"))); package = f_field(response, "data");
    inspected = task_inspect(package); assert(json_object_get_boolean(f_field(inspected, "ok"))); json_object_put(inspected);
    assert(!f_path(bundle, sizeof(bundle), dir, "received.bundle")); assert(!f_hex_write(bundle, f_string(package, "bundle_hex"), 0600));
    free(run(fetch)); out = run(show); assert(!strcmp(out, "committed\n")); free(out);
    out = f_read(code, 100); assert(out && !strcmp(out, "dirty\n")); free(out);
    /* A checksum-valid payload that is not a Git bundle still fails inspection. */
    f_string_add(package, "bundle_hex", "626164");
    assert(!f_write(bundle, "bad", 3, true)); assert(!f_hash(bundle, commit));
    f_string_add(f_field(f_field(package, "spec"), "source"), "bundle_sha256", commit);
    assert(!task_json_hash(f_field(package, "spec"), dir, commit)); f_string_add(package, "spec_sha256", commit);
    inspected = task_inspect(package); assert(!json_object_get_boolean(f_field(inspected, "ok"))); json_object_put(inspected);
    json_object_put(response); json_object_put(spec);
}
int main(void) {
    char dir[] = "/tmp/hydra-task-unit.XXXXXX";
    assert(mkdtemp(dir)); f_home = dir; f_hydra = "hydra";
    validation(); exact_source(dir); assert(!f_remove_tree(dir));
    puts("Task specification bounds and independently fetched exact-source bundle passed"); return 0;
}
