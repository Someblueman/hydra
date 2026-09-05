#include "task.h"
#include <stdlib.h>
#include <string.h>

json_object *task_cli(int argc, char **argv) {
    const char *source = NULL, *spec_path = NULL, *input = NULL, *output = NULL;
    json_object *parsed = NULL, *result; int i;
    if (argc && (!strcmp(argv[0], "submit") || !strcmp(argv[0], "status") || !strcmp(argv[0], "start") || !strcmp(argv[0], "cancel") || !strcmp(argv[0], "logs") || !strcmp(argv[0], "result"))) return task_remote_cli(argc, argv);
    if (!argc || !strcmp(argv[0], "help") || !strcmp(argv[0], "--help")) {
        json_object *data = json_object_new_object();
        f_string_add(data, "usage", "fleet task prepare --source DIR --spec FILE --output PACKAGE; fleet task inspect --input PACKAGE; fleet task submit HOST --input PACKAGE --key KEY [--trust-spec HASH]; fleet task start HOST --id TASK_ID --trust-spec HASH; fleet task result HOST --id TASK_ID [--output FILE] [--timeout SECONDS]; fleet task inspect-result --input FILE; fleet task status HOST --id TASK_ID; fleet task cancel HOST --id TASK_ID; fleet task logs HOST --id TASK_ID [--source owner|work] [--stream stdout|stderr] [--offset N] [--limit N] [--step NAME --attempt N]");
        return f_success("fleet-task-help", data);
    }
    for (i = 1; i < argc; i++) {
        const char **destination;
        if (!strcmp(argv[i], "--source")) destination = &source;
        else if (!strcmp(argv[i], "--spec")) destination = &spec_path;
        else if (!strcmp(argv[i], "--input")) destination = &input;
        else if (!strcmp(argv[i], "--output")) destination = &output;
        else return f_error("fleet-task", "invalid_input", "unknown task option");
        if (*destination || ++i == argc || !*argv[i]) return f_error("fleet-task", "invalid_input", "each task option requires one value");
        *destination = argv[i];
    }
    if (!strcmp(argv[0], "prepare") && source && spec_path && output && !input) {
        parsed = f_read_json(spec_path, 65536);
        result = task_prepare(source, parsed); json_object_put(parsed);
        if (json_object_get_boolean(f_field(result, "ok"))) {
            json_object *package = f_field(result, "data"), *preview = json_object_new_object();
            const char *encoded = json_object_to_json_string_ext(package, JSON_C_TO_STRING_PLAIN);
            if (f_write(output, encoded, strlen(encoded), false)) {
                json_object_put(result); json_object_put(preview);
                return f_error("fleet-task-prepare", "io_failed", "cannot write a new package; existing output is never replaced");
            }
            f_string_add(preview, "file", output); f_string_add(preview, "spec_sha256", f_string(package, "spec_sha256"));
            json_object_object_add(preview, "transfer_bytes", json_object_new_int64((int64_t)strlen(encoded)));
            json_object_object_add(preview, "spec", json_object_get(f_field(package, "spec")));
            json_object_put(result); return f_success("fleet-task-prepare", preview);
        }
        return result;
    }
    if (!strcmp(argv[0], "inspect-result") && input && !source && !spec_path && !output) {
        parsed = f_read_json(input, TASK_PACKAGE_LIMIT + 1024);
        result = task_result_verify(parsed); json_object_put(parsed);
        if (json_object_get_boolean(f_field(result, "ok"))) {
            json_object *data = f_field(result, "data"); const char *arrays[] = {"artifacts", "evidence"}; size_t j, k;
            json_object_object_del(data, "bundle_hex");
            for (j = 0; j < 2; j++) for (k = 0; k < json_object_array_length(f_field(data, arrays[j])); k++)
                json_object_object_del(json_object_array_get_idx(f_field(data, arrays[j]), k), "hex");
        }
        return result;
    }
    if (!strcmp(argv[0], "inspect") && input && !source && !spec_path && !output) {
        parsed = f_read_json(input, TASK_PACKAGE_LIMIT);
        result = task_inspect(parsed); json_object_put(parsed); return result;
    }
    return f_error("fleet-task", "invalid_input", "use hydra fleet task help");
}
