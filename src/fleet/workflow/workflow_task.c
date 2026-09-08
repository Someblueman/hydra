#include "fleet/workflow/workflow_task.h"
#include "fleet/workflow/workflow_data.h"
#include <string.h>
json_object *wt_cli(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[0], "drive")) {
        wt_drive(argv[1]);
        return f_error("workflow task", "coordinator_busy", "cannot acquire exclusive coordinator ownership");
    }
    if (argc == 3 && !strcmp(argv[0], "init")) {
        if (!wt_initialize(argv[1], argv[2])) return f_success("workflow task", json_object_new_object());
    } else if (argc == 2 && !strcmp(argv[0], "verify")) {
        json_object *record = wt_bindings(argv[1]);
        if (record) { json_object_put(record); return f_success("workflow task", json_object_new_object()); }
    } else if (argc == 4 && !strcmp(argv[0], "run") && wd_name(argv[2])) return wt_execute(argv[1], argv[2], argv[3]);
    return f_error("workflow task", "invalid_binding", "task recipes, input mappings, or recorded coordinator bindings are invalid");
}
