#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include "fleet/workflow/workflow_schedule.h"
#include <string.h>
static json_object *runtime_command(const char *operation, const char *run) {
    if (!strcmp(operation, "replay")) return ws_replay(run);
    if (!strcmp(operation, "next")) {
        if (!ws_next(run)) return NULL;
        return f_error("workflow next", "invalid_journal", "cannot record a decision under the bound coordinator; reconcile this run");
    }
    if (!strcmp(operation, "drive")) {
        wt_drive(run);
        return f_error("workflow task", "coordinator_busy", "cannot acquire exclusive coordinator ownership");
    }
    if (!strcmp(operation, "verify")) {
        json_object *record = wt_bindings(run);
        if (record) { json_object_put(record); return f_success("workflow task", json_object_new_object()); }
    }
    return f_error("workflow task", "invalid_binding", "recorded coordinator bindings are invalid");
}
json_object *wt_cli(int argc, char **argv) {
    if (argc == 2) return runtime_command(argv[0], argv[1]);
    if (argc == 3 && !strcmp(argv[0], "init")) {
        if (!wt_initialize(argv[1], argv[2])) return f_success("workflow task", json_object_new_object());
    } else if (argc == 4 && !strcmp(argv[0], "run") && wd_name(argv[2])) return wt_execute(argv[1], argv[2], argv[3]);
    return f_error("workflow task", "invalid_binding", "task recipes, input mappings, or recorded coordinator bindings are invalid");
}
