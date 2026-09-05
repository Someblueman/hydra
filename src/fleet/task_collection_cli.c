#include "task.h"
#include <stdlib.h>
#include <string.h>

json_object *task_collection_cli(int argc, char **argv) {
    const char *project = NULL, *input = NULL, *host = NULL, *id = NULL, *timeout = NULL, *format = NULL;
    bool read = !strcmp(argv[0], "collected"); int i; json_object *response = NULL, *envelope = NULL, *out;
    for (i = 1; i < argc; i++) {
        const char **destination;
        if (argv[i][0] != '-' && !host && !read) { host = argv[i]; continue; }
        if (!strcmp(argv[i], "--into")) destination = &project;
        else if (!strcmp(argv[i], "--input") && !read) destination = &input;
        else if (!strcmp(argv[i], "--id")) destination = &id;
        else if (!strcmp(argv[i], "--timeout") && !read) destination = &timeout;
        else if (!strcmp(argv[i], "--format") && read) destination = &format;
        else return f_error("fleet-task-collect", "invalid_input", "use fleet task collect HOST --id TASK --into DIR, or collect --input FILE --into DIR");
        if (*destination || ++i == argc || !*argv[i]) return f_error("fleet-task-collect", "invalid_input", "each option requires one value");
        *destination = argv[i];
    }
    if (!project || (read ? !id : (input ? host || id || timeout : !host || !id))) return f_error("fleet-task-collect", "invalid_input", "choose one result source and an explicit destination repository");
    if (read) {
        if (format && strcmp(format, "candidates")) return f_error("fleet-task-collected", "invalid_input", "format must be candidates");
        out = task_collected(project, id);
        if (format && json_object_get_boolean(f_field(out, "ok"))) {
            json_object *data = f_field(out, "data"), *heads = f_field(data, "heads"), *state = f_field(f_field(data, "receipt"), "runtime"); size_t n;
            if (strcmp(f_string(state, "state"), "succeeded") || !json_object_array_length(heads)) goto ineligible;
            for (n = 0; n < json_object_array_length(heads); n++)
                if (json_object_get_boolean(f_field(json_object_array_get_idx(heads, n), "dirty"))) goto ineligible;
            for (n = 0; n < json_object_array_length(heads); n++) {
                json_object *head = json_object_array_get_idx(heads, n);
                printf("remote_%s\t%s\t%s\t%s\n", f_string(head, "head_id"), f_string(head, "ref"), f_string(head, "commit"), f_string(data, "source_commit"));
            }
            json_object_put(out); return NULL;
        }
        return out;
    }
    if (input) envelope = f_read_json(input, TASK_PACKAGE_LIMIT + 1024);
    else {
        char *remote[] = {"result", (char *)host, "--id", (char *)id, "--timeout", (char *)timeout, NULL};
        response = task_remote_cli(timeout ? 6 : 4, remote);
        if (!json_object_get_boolean(f_field(response, "ok"))) return response;
        envelope = json_object_get(f_field(f_field(response, "data"), "collection"));
    }
    out = task_collect(project, envelope); json_object_put(envelope); json_object_put(response); return out;
ineligible:
    json_object_put(out); return f_error("fleet-task-collected", "integration_ineligible", "integration requires a successful task snapshot with clean committed result heads");
}
