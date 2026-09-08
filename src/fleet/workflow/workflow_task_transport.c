#include "fleet/workflow/workflow_task.h"
#include "fleet/transport/remote.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include <string.h>

json_object *wt_request(json_object *destination, json_object *request, unsigned seconds) {
    const char *kind = f_string(destination, "kind"), *alias = f_string(destination, "alias");
    json_object *current = wt_destination(alias ? alias : "local"), *response = NULL;
    bool matches = current && json_object_equal(current, destination);
    json_object_put(current);
    if (!matches || !kind) return f_error("workflow task", "placement_changed", "the recorded destination no longer matches its registered transport");
    if (!strcmp(kind, "local")) {
        struct f_capture cap = {0}; char *argv[] = {(char *)f_hydra, "fleet", "serve", NULL};
        const char *text = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
        if (!f_run(argv, text, strlen(text), seconds, &cap)) response = f_parse(cap.out);
        f_capture_free(&cap);
    } else {
        struct f_remote remote = {0};
        if (!f_copy(remote.name, sizeof(remote.name), alias) &&
            !f_copy(remote.target, sizeof(remote.target), f_string(destination, "target")) &&
            !f_copy(remote.home, sizeof(remote.home), f_string(destination, "home")) &&
            !f_copy(remote.hydra, sizeof(remote.hydra), f_string(destination, "hydra"))) {
            remote.multiplex = json_object_get_boolean(f_field(destination, "multiplex"));
            response = f_request(&remote, request, seconds);
        }
    }
    return response ? response : f_error("workflow task", "outcome_unknown", "transport did not return a valid response; retain the original dispatch identity");
}
