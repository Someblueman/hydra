#include "fleet/workflow_data.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *f_home, *f_hydra;
static void typed_files(const char *root) {
    const char *types[] = {"object", "array", "string", "number", "boolean"};
    const char *values[] = {"{\"a\":1}", "[1,2]", "\"text\"", "42.5", "true"};
    const char *bad[] = {"1,\"value\":2", "1 trailing", "{\"x\":NaN}", "{\"x\":1e999}", "null", "[", NULL};
    char path[F_PATH]; json_object *declaration = f_parse("{\"type\":\"file\",\"max_bytes\":128}"), *file; size_t i;
    assert(!f_path(path, sizeof(path), root, "typed"));
    assert(!f_write(path, "a\0b\377", 4, true));
    file = wd_file(path, declaration); assert(file && f_number_is(file, "bytes", 4)); json_object_put(file);
    for (i = 0; i < 5; i++) {
        f_string_add(declaration, "type", types[i]); assert(!f_write(path, values[i], strlen(values[i]), true));
        file = wd_file(path, declaration); assert(file); json_object_put(file);
    }
    for (i = 0; bad[i]; i++) {
        f_string_add(declaration, "type", "number"); assert(!f_write(path, bad[i], strlen(bad[i]), true)); assert(!wd_file(path, declaration));
    }
    f_string_add(declaration, "type", "string"); assert(!f_write(path, "\"x\"\0", 4, true)); assert(!wd_file(path, declaration));
    f_string_add(declaration, "type", "object"); assert(!f_write(path, "[]", 2, true)); assert(!wd_file(path, declaration));
    f_string_add(declaration, "type", "file"); json_object_object_add(declaration, "max_bytes", json_object_new_int(1)); assert(!wd_file(path, declaration));
    json_object_object_add(declaration, "max_bytes", json_object_new_int(128));
    f_string_add(declaration, "sha256", "0000000000000000000000000000000000000000000000000000000000000000"); assert(!wd_file(path, declaration));
    json_object_put(declaration);
}
static void manifests(const char *root) {
    char graph[F_PATH], path[F_PATH]; json_object *manifest, *checked, *output;
    const char *rows = "workflow\tsample\t1\t1\t1\nstep\tproducer\texec\t-\t0\tfalse\nstep\tconsumer\texec\tproducer\t0\tfalse\n";
    manifest = f_parse("{\"schema_version\":1,\"inputs\":{},\"steps\":{\"producer\":{\"outputs\":{\"report\":{\"path\":\"report\",\"type\":\"object\",\"max_bytes\":128}}},\"consumer\":{\"inputs\":{\"report\":{\"step\":\"producer\",\"output\":\"report\"}}}}}");
    assert(!f_path(graph, sizeof(graph), root, "graph.tsv") && !f_path(path, sizeof(path), root, "data.json"));
    assert(!f_write(graph, rows, strlen(rows), false) && !task_write_json(root, "data.json", manifest, false));
    checked = wd_manifest(path, graph); assert(checked); json_object_put(checked);
    output = f_field(f_field(f_field(f_field(manifest, "steps"), "producer"), "outputs"), "report");
    f_string_add(output, "path", "../escape"); assert(!task_write_json(root, "data.json", manifest, true)); assert(!wd_manifest(path, graph));
    f_string_add(output, "path", "report"); json_object_object_add(output, "max_bytes", json_object_new_int(65537));
    assert(!task_write_json(root, "data.json", manifest, true)); assert(!wd_manifest(path, graph));
    json_object_object_add(output, "max_bytes", json_object_new_int(128)); f_string_add(output, "source", "task");
    assert(!task_write_json(root, "data.json", manifest, true)); assert(!wd_manifest(path, graph));
    json_object_object_del(output, "source"); f_string_add(output, "unknown", "value");
    assert(!task_write_json(root, "data.json", manifest, true)); assert(!wd_manifest(path, graph));
    json_object_put(manifest);
}
int main(void) {
    char root[] = "/tmp/hydra-workflow-data-test.XXXXXX";
    assert(mkdtemp(root)); f_home = root; f_hydra = "hydra";
    typed_files(root); manifests(root); assert(!f_remove_tree(root));
    puts("Workflow data: typed bytes, strict JSON, bounds, digests and manifest boundaries passed"); return 0;
}
