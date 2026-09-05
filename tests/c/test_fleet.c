#include "fleet/fleet.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
const char *f_home, *f_hydra;
int main(void) {
    char dir[] = "/tmp/hydra-fleet-unit.XXXXXX", path[F_PATH]; json_object *obj, *bundle, *files, *file, *result;
    struct f_capture cap = {0}; char *quoted, *text;
    char *echo[] = {(char *)"sh", (char *)"-c", (char *)"cat; printf error >&2", NULL};
    char *hang[] = {(char *)"sleep", (char *)"10", NULL};
    assert(mkdtemp(dir)); f_home = dir; f_hydra = "hydra";
    assert(!f_parse("{broken}")); assert(!f_parse("{} {}")); assert(!f_parse("{\"a\":NaN}"));
    obj = f_parse("{\"a\":\"x\\u0000y\"}"); assert(obj && !f_string(obj, "a")); json_object_put(obj);
    quoted = f_quote("a'b;$(touch nope)"); assert(quoted && !strcmp(quoted, "'a'\\''b;$(touch nope)'")); free(quoted);
    assert(!f_target("-oProxyCommand=evil") && !f_target("host;evil") && f_target("ubuntu@example.test"));
    assert(!f_run(echo, "literal input", 13, 2, &cap));
    assert(cap.status == 0 && !strcmp(cap.out, "literal input") && !strcmp(cap.err, "error")); f_capture_free(&cap);
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
