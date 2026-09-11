#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/attention_tui.h"
#include "fleet/task/task.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int attempt_command(const char *action, json_object *manifest, const char *run, const char *step, const char *attempt) {
    if (!strcmp(action, "prepare")) return wd_prepare(manifest, run, step, attempt);
    if (!strcmp(action, "verify-output")) return wd_verify_output(manifest, step, attempt);
    if (!strcmp(action, "seal")) return wd_seal(manifest, step, attempt);
    return -1;
}
static json_object *attention_data(const char *project)
{
    json_object *result = wd_attention(project);
    if (!json_object_get_boolean(f_field(result, "ok"))) return result;
    if (f_attention_tui_data(result)) {
        json_object_put(result);
        return f_error("workflow-attention", "projection_failed", "attention snapshot could not be encoded for the native TUI");
    }
    json_object_put(result);
    return NULL;
}
json_object *wd_cli(int argc, char **argv) {
    json_object *manifest = NULL, *result; char data[F_PATH], graph[F_PATH]; int status = -1;
    if (argc == 2 && !strcmp(argv[0], "fingerprint")) {
        char digest[65];
        if (wd_fingerprint(argv[1], digest)) return f_error("workflow-data", "binding_failed", "cannot fingerprint the bounded worktree evidence");
        puts(digest); return NULL;
    }
    if (argc == 2 && !strcmp(argv[0], "attention")) return wd_attention(argv[1]);
    if (argc == 2 && !strcmp(argv[0], "attention-data")) return attention_data(argv[1]);
    if (argc == 4 && !strcmp(argv[0], "validate")) {
        char scratch[] = "/tmp/hydra-workflow-data.XXXXXX";
        if (mkdtemp(scratch)) {
            if (!f_path(data, sizeof(data), scratch, "data.json") && !task_file_copy(argv[1], argv[2], data)) manifest = wd_manifest(data, argv[3]);
            status = manifest ? 0 : -1; f_remove_tree(scratch);
        }
    } else if (argc >= 2 && !f_path(data, sizeof(data), argv[1], "data.json") && !f_path(graph, sizeof(graph), argv[1], "graph.tsv")) {
        if (argc == 5 && !strcmp(argv[0], "init") && task_file_copy(argv[3], argv[4], data))
            return f_error("workflow-data", "invalid_data", "cannot snapshot workflow data manifest");
        manifest = wd_manifest(data, graph);
        if (manifest) {
            if (argc == 5 && !strcmp(argv[0], "init")) status = wd_initialize(manifest, argv[2], argv[1]);
            else if (argc == 2 && !strcmp(argv[0], "verify")) status = wd_verify(manifest, argv[1]);
            else if (argc == 4 && wd_name(argv[2])) status = attempt_command(argv[0], manifest, argv[1], argv[2], argv[3]);
        }
    }
    result = status ? f_error("workflow-data", "invalid_data", "workflow data is invalid, missing, changed, oversized, or has unsafe paths; inspect declarations and sealed artifacts") :
        f_success("workflow-data", json_object_new_object());
    json_object_put(manifest); return result;
}
