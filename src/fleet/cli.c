#include "fleet.h"
#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool supported(json_object *handshake, const char *action) {
    json_object *caps = f_field(f_field(handshake, "data"), "capabilities"); size_t i;
    if (!json_object_is_type(caps, json_type_array)) return false;
    for (i = 0; i < json_object_array_length(caps); i++) {
        json_object *cap = json_object_array_get_idx(caps, i);
        if (json_object_is_type(cap, json_type_string) && !strcmp(json_object_get_string(cap), action)) return true;
    }
    return false;
}
static json_object *attach_remote(const struct f_remote *remote, json_object *response, unsigned seconds) {
    const char *session = f_string(f_field(response, "data"), "session"); char *quoted, command[F_PATH * 4];
    struct f_capture cap = {0};
    if (!session || !(quoted = f_quote(session))) return f_error("fleet-attach", "invalid_response", "remote session is missing");
    if (snprintf(command, sizeof(command), "tmux attach-session -t %s", quoted) >= (int)sizeof(command)) { free(quoted); return f_error("fleet-attach", "invalid_response", "session name is too long"); }
    free(quoted); f_ssh(remote, command, NULL, 0, seconds, true, &cap);
    return f_error("fleet-attach", "transport_failed", "cannot execute interactive SSH");
}
json_object *f_cli(int argc, char **argv) {
    const char *action, *name = NULL, *project = NULL, *instance = NULL, *output = NULL, *input = NULL, *digest = NULL, *source = NULL, *binary = NULL, *run = NULL;
    unsigned seconds = 5, jobs = 4, interval = 5; int i, rest = argc; bool explicit_timeout = false;
    json_object *result, *request, *args; struct f_remote remote;
    if (argc < 1) return f_error("fleet", "invalid_input", "use hydra fleet help");
    action = argv[0];
    if (!strcmp(action, "task")) return task_cli(argc - 1, argv + 1);
    if (!strcmp(action, "help") || !strcmp(action, "--help")) {
        json_object *data = json_object_new_object();
        f_string_add(data, "usage", "fleet list|doctor|reconcile|watch [--timeout N --jobs N]; fleet task help; fleet bootstrap HOST --input PACKAGE --sha256 HASH; fleet package --source DIR --binary FILE --output FILE; fleet init|spawn|signal|cancel|workflow|attach|export|import HOST --project /path [--instance ID] [--input FILE --output FILE --run ID] -- ARGS");
        return f_success("fleet-help", data);
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { rest = i + 1; break; }
        if (!strcmp(argv[i], "--json")) continue;
        if (argv[i][0] != '-' && !name) { name = argv[i]; continue; }
        if (i + 1 >= argc) return f_error("fleet", "invalid_input", "missing option value");
        if (!strcmp(argv[i], "--project")) project = argv[++i];
        else if (!strcmp(argv[i], "--instance")) instance = argv[++i];
        else if (!strcmp(argv[i], "--output")) output = argv[++i];
        else if (!strcmp(argv[i], "--input")) input = argv[++i];
        else if (!strcmp(argv[i], "--sha256")) digest = argv[++i];
        else if (!strcmp(argv[i], "--source")) source = argv[++i];
        else if (!strcmp(argv[i], "--binary")) binary = argv[++i];
        else if (!strcmp(argv[i], "--run")) run = argv[++i];
        else if (!strcmp(argv[i], "--timeout") || !strcmp(argv[i], "--jobs") || !strcmp(argv[i], "--interval")) {
            char *end; const char *option = argv[i]; unsigned long value = strtoul(argv[++i], &end, 10);
            if (!*argv[i] || *end || !value || value > (!strcmp(option, "--jobs") ? 16UL : 300UL)) return f_error("fleet", "invalid_input", "jobs must be 1-16; timeout and interval must be 1-300 seconds");
            if (!strcmp(option, "--jobs")) jobs = (unsigned)value;
            else if (!strcmp(option, "--interval")) interval = (unsigned)value;
            else { seconds = (unsigned)value; explicit_timeout = true; }
        } else return f_error("fleet", "invalid_input", "unknown option");
    }
    if (!strcmp(action, "handshake") && !name) return f_handshake();
    if (!strcmp(action, "tui")) {
        char path[F_PATH]; const char *native = getenv("HYDRA_TUI_BIN"), *bin = getenv("HYDRA_BIN_DIR");
        if (native) execl(native, native, "--fleet", "--hydra", f_hydra, (char *)NULL);
        else if (bin) {
            if (!f_path(path, sizeof(path), bin, "../build/hydra-tui")) execl(path, path, "--fleet", "--hydra", f_hydra, (char *)NULL);
            if (!f_path(path, sizeof(path), bin, "../libexec/hydra/hydra-tui")) execl(path, path, "--fleet", "--hydra", f_hydra, (char *)NULL);
        }
        return f_error("fleet-tui", "missing_dependency", "build or install the optional native TUI");
    }
    if (!strcmp(action, "tui-data")) { (void)f_tui_data(1, 16); return NULL; }
    if (!strcmp(action, "package")) {
        if (!source || !binary || !output) return f_error("fleet-package", "invalid_input", "source, target binary, and output are required");
        result = f_package(source, binary);
        if (json_object_get_boolean(f_field(result, "ok"))) {
            const char *text = json_object_to_json_string_ext(f_field(result, "data"), JSON_C_TO_STRING_PLAIN); char hash[65];
            if (f_write(output, text, strlen(text), false) || f_hash(output, hash)) { json_object_put(result); return f_error("fleet-package", "io_failed", "cannot write new package output"); }
            json_object_put(result); request = json_object_new_object(); f_string_add(request, "file", output); f_string_add(request, "sha256", hash); result = f_success("fleet-package", request);
        }
        return result;
    }
    if (!strcmp(action, "watch")) {
        while (!f_stopped) {
            struct timespec pause = {0, 100000000}; unsigned tick;
            result = f_aggregate("list", seconds, jobs); (void)f_emit(result); json_object_put(result); fflush(stdout);
            for (tick = 0; tick < interval * 10 && !f_stopped; tick++) nanosleep(&pause, NULL);
        }
        return NULL;
    }
    if (!strcmp(action, "reconcile")) action = "list";
    if ((!strcmp(action, "list") || !strcmp(action, "doctor")) && !name) return f_aggregate(action, seconds, jobs);
    if (!name || f_remote_load(name, &remote)) return f_error("fleet", "invalid_alias", "register a remote with hydra remote add");
    if (!strcmp(action, "bootstrap")) {
        if (!input || !digest) return f_error("fleet-bootstrap", "invalid_input", "input package and sha256 are required");
        return f_bootstrap(&remote, input, digest, explicit_timeout ? seconds : 60);
    }
    result = f_observe(&remote, "handshake", seconds);
    if (!json_object_get_boolean(f_field(result, "ok"))) return result;
    if (!supported(result, action)) { json_object_put(result); return f_error("fleet", "capability_unavailable", "remote does not advertise this operation"); }
    json_object_put(result);
    request = json_object_new_object(); args = json_object_new_array();
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(request, "action", action);
    if (project) f_string_add(request, "project", project);
    if (instance) f_string_add(request, "instance", instance);
    if (run) f_string_add(request, "run", run);
    for (i = rest; i < argc; i++) json_object_array_add(args, json_object_new_string(argv[i]));
    json_object_object_add(request, "args", args);
    if (!strcmp(action, "import")) {
        char *text = input ? f_read(input, F_LIMIT) : NULL; json_object *bundle = text ? f_parse(text) : NULL; free(text);
        if (!bundle) { json_object_put(request); return f_error("fleet-import", "invalid_input", "a valid input bundle is required"); }
        json_object_object_add(request, "bundle", bundle);
    }
    result = f_request(&remote, request, explicit_timeout ? seconds : ((!strcmp(action, "list") || !strcmp(action, "doctor")) ? 5 : 300));
    json_object_put(request);
    if (!json_object_get_boolean(f_field(result, "ok"))) {
        const char *code = f_string(f_field(result, "error"), "code");
        if (code && (!strcmp(code, "timeout") || !strcmp(code, "offline") || !strcmp(code, "cancelled")) && strcmp(action, "list") && strcmp(action, "doctor"))
            f_string_add(f_field(result, "error"), "code", "outcome_unknown");
        return result;
    }
    if (!strcmp(action, "export")) {
        const char *text = json_object_to_json_string_ext(f_field(result, "data"), JSON_C_TO_STRING_PLAIN);
        if (!output || f_write(output, text, strlen(text), false)) { json_object_put(result); return f_error("fleet-export", "io_failed", "a new output path is required"); }
    }
    if (!strcmp(action, "attach")) { json_object *failure = attach_remote(&remote, result, seconds); json_object_put(result); return failure; }
    return result;
}
