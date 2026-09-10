#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/transport/server.h"
#include "fleet/transport/bundle.h"
#include "fleet/fleet.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
const char *f_home, *f_hydra;
static void observe_stdout(void *context, const char *text, size_t size) {
    size_t *observed = context;
    assert(size >= *observed && size <= 6);
    assert(!memcmp(text, "output", size));
    *observed = size;
}
static void capture_streams(void) {
    FILE *out = tmpfile(), *err = tmpfile();
    struct f_capture cap = {0};
    struct f_control control = {0};
    size_t observed = 0;
    char bytes[4] = {0};
    char *command[] = {(char *)"sh", (char *)"-c",
        (char *)"printf output; exec 1>&-; printf error >&2; exit 7", NULL};
    assert(out && err);
    control.log_fd[0] = fileno(out); control.log_fd[1] = fileno(err);
    control.remaining[0] = 2; control.remaining[1] = 3;
    control.observe = observe_stdout; control.context = &observed;
    assert(!f_run_controlled(command, NULL, 0, 2, &cap, &control));
    assert(cap.status == 7 && cap.out_bytes == 6 && cap.err_bytes == 5);
    assert(!strcmp(cap.out, "output") && !strcmp(cap.err, "error"));
    assert(observed == 6 && control.truncated && !control.log_error);
    assert(fseek(out, 0, SEEK_SET) == 0);
    assert(fseek(err, 0, SEEK_SET) == 0);
    assert(fread(bytes, 1, sizeof(bytes), out) == 2 && !memcmp(bytes, "ou", 2));
    assert(fread(bytes, 1, sizeof(bytes), err) == 3 && !memcmp(bytes, "err", 3));
    fclose(out); fclose(err); f_capture_free(&cap);
}
static void capture_partial_input(void) {
    struct f_capture cap = {0};
    char *short_read[] = {"sh", "-c", "dd bs=1 count=3 2>/dev/null", NULL};
    assert(!f_run(short_read, "longer input", 12, 2, &cap));
    assert(cap.in_bytes == 3 && cap.out_bytes == 3 && !cap.input_complete && cap.measurement_complete);
    f_capture_free(&cap);
    char *no_read[] = {"sh", "-c", "exit 7", NULL};
    assert(!f_run(no_read, "unsent", 6, 2, &cap));
    assert(cap.status == 7 && cap.in_bytes == 0 && cap.out_bytes == 0 && !cap.input_complete && cap.measurement_complete);
    f_capture_free(&cap);
}
int main(void) {
    char dir[] = "/tmp/hydra-fleet-unit.XXXXXX", path[F_PATH]; json_object *obj, *bundle, *files, *file, *result;
    struct f_capture cap = {0}; char *quoted, *text;
    char *echo[] = {(char *)"sh", (char *)"-c", (char *)"cat; printf error >&2", NULL};
    char *hang[] = {(char *)"sleep", (char *)"10", NULL};
    assert(mkdtemp(dir)); f_home = dir; f_hydra = "hydra";
    capture_streams();
    assert(!f_parse("{broken}")); assert(!f_parse("{} {}")); assert(!f_parse("{\"a\":NaN}"));
    obj = f_parse("{\"a\":\"x\\u0000y\"}"); assert(obj && !f_string(obj, "a")); json_object_put(obj);
    quoted = f_quote("a'b;$(touch nope)"); assert(quoted && !strcmp(quoted, "'a'\\''b;$(touch nope)'")); free(quoted);
    assert(!f_target("-oProxyCommand=evil") && !f_target("host;evil") && f_target("ubuntu@example.test"));
    assert(!f_run(echo, "literal input", 13, 2, &cap));
    assert(cap.status == 0 && cap.in_bytes == 13 && cap.input_complete && !strcmp(cap.out, "literal input") && !strcmp(cap.err, "error")); f_capture_free(&cap);
    capture_partial_input();
    assert(!f_run(hang, NULL, 0, 1, &cap) && cap.timeout && cap.status == 124); f_capture_free(&cap);
    bundle = json_object_new_object(); files = json_object_new_array(); file = json_object_new_object();
    json_object_object_add(bundle, "schema_version", json_object_new_int(1)); f_string_add(bundle, "kind", "config");
    f_string_add(file, "path", "../outside"); f_string_add(file, "hex", "6162");
    json_object_array_add(files, file); json_object_object_add(bundle, "files", files);
    result = f_bundle_import(dir, bundle); assert(!json_object_get_boolean(f_field(result, "ok"))); json_object_put(result);
    f_string_add(file, "path", "config.yml");
    result = f_bundle_import(dir, bundle); assert(json_object_get_boolean(f_field(result, "ok"))); json_object_put(result);
    assert(!f_path(path, sizeof(path), dir, ".hydra/config.yml")); text = f_read(path, 100); assert(text && !strcmp(text, "ab")); free(text);
    result = f_bundle_import(dir, bundle); assert(!json_object_get_boolean(f_field(result, "ok"))); json_object_put(result);
    f_string_add(bundle, "kind", "history"); f_string_add(file, "path", "owner-pid");
    result = f_bundle_import(dir, bundle); assert(!json_object_get_boolean(f_field(result, "ok"))); json_object_put(result); json_object_put(bundle);
    obj = f_parse("{\"protocol\":1,\"action\":\"doctor\",\"args\":[\"--fix\"]}");
    result = f_serve(obj); assert(!json_object_get_boolean(f_field(result, "ok"))); json_object_put(result); json_object_put(obj);
    obj = f_parse("{\"protocol\":\"1\",\"action\":\"handshake\"}");
    result = f_serve(obj); assert(!json_object_get_boolean(f_field(result, "ok"))); json_object_put(result); json_object_put(obj);
    obj = f_parse("{\"protocol\":1,\"action\":\"doctor\",\"args\":{}}");
    result = f_serve(obj); assert(!json_object_get_boolean(f_field(result, "ok"))); json_object_put(result); json_object_put(obj);
    assert(!f_remove_tree(dir)); puts("Fleet JSON, process deadlines, and bundle boundaries passed"); return 0;
}
