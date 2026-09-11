#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/transport/server.h"
#include "fleet/transport/bundle.h"
#include "fleet/cli.h"
#include "fleet/fleet.h"
#include "fleet/task/task.h"
#include "fleet/auth/agent_auth.h"
#include "fleet/discovery/discovery.h"
#include "fleet/enrollment/enrollment.h"
#include "fleet/retention/retention.h"
#include "tui/fleet_budget.h"
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
static json_object *attach_remote(const struct f_remote *remote, json_object *response, const char *project, const char *branch, const char *instance, unsigned seconds) {
    const char *session_id = f_string(f_field(response, "data"), "session_id");
    char *quoted_project = NULL, *quoted_hydra = NULL, *quoted_home = NULL, *quoted_branch = NULL, *quoted_instance = NULL, command[F_PATH * 5 + 128];
    struct f_capture cap = {0};
    if (!session_id || !project || project[0] != '/' || !branch || !instance ||
        !(quoted_project = f_quote(project)) ||
        !(quoted_hydra = f_quote(remote->hydra)) ||
        (remote->home[0] && !(quoted_home = f_quote(remote->home))) ||
        !(quoted_branch = f_quote(branch)) ||
        !(quoted_instance = f_quote(instance))) {
        free(quoted_project); free(quoted_hydra); free(quoted_home); free(quoted_branch); free(quoted_instance);
        return f_error("fleet-attach", "invalid_response", "remote session identity is missing");
    }
    if (snprintf(command, sizeof(command), "cd %s && env LC_ALL=C %s%s %s fleet-local attach %s %s", quoted_project,
                 remote->home[0] ? "HYDRA_HOME=" : "", remote->home[0] ? quoted_home : "",
                 quoted_hydra, quoted_branch, quoted_instance) >= (int)sizeof(command)) {
        free(quoted_project); free(quoted_hydra); free(quoted_home); free(quoted_branch); free(quoted_instance);
        return f_error("fleet-attach", "invalid_response", "remote attachment command is too long");
    }
    free(quoted_project); free(quoted_hydra); free(quoted_home); free(quoted_branch); free(quoted_instance);
    f_ssh(remote, command, NULL, 0, seconds, true, &cap);
    return f_error("fleet-attach", "transport_failed", "cannot execute interactive SSH");
}
static json_object *launch_tui(void) {
    char path[F_PATH]; const char *native = getenv("HYDRA_TUI_BIN"), *bin = getenv("HYDRA_BIN_DIR");
    if (native) execl(native, native, "--fleet", "--hydra", f_hydra, (char *)NULL);
    else if (bin) {
        if (!f_path(path, sizeof(path), bin, "../build/hydra-tui")) execl(path, path, "--fleet", "--hydra", f_hydra, (char *)NULL);
        if (!f_path(path, sizeof(path), bin, "../libexec/hydra/hydra-tui")) execl(path, path, "--fleet", "--hydra", f_hydra, (char *)NULL);
    }
    return f_error("fleet-tui", "missing_dependency", "build or install the optional native TUI");
}
struct fleet_options {
    const char *name, *project, *instance, *output, *input, *digest, *source, *binary, *run;
    unsigned seconds, jobs, interval;
    int rest;
    bool explicit_timeout;
};
static json_object *bootstrap_alias(struct f_remote *remote, const struct fleet_options *options) {
    json_object *result;
    if (!options->input || !options->digest) return f_error("fleet-bootstrap", "invalid_input", "input package and sha256 are required");
    result = f_bootstrap(remote, options->input, options->digest, NULL, options->explicit_timeout ? options->seconds : 60);
    if (json_object_get_boolean(f_field(result, "ok")) && f_remote_save(remote)) {
        json_object_put(result); return f_error("fleet-bootstrap", "alias_update_failed", "installed package exists but its alias could not be saved");
    }
    return result;
}
static json_object *parse_options(int argc, char **argv, struct fleet_options *options) {
    const struct { const char *name; const char **value; } fields[] = {
        {"--project", &options->project},
        {"--instance", &options->instance},
        {"--output", &options->output},
        {"--input", &options->input},
        {"--sha256", &options->digest},
        {"--source", &options->source},
        {"--binary", &options->binary},
        {"--run", &options->run}
    };
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { options->rest = i + 1; break; }
        if (!strcmp(argv[i], "--json")) continue;
        if (argv[i][0] != '-' && !options->name) { options->name = argv[i]; continue; }
        if (i + 1 >= argc) return f_error("fleet", "invalid_input", "missing option value");
        const char **destination = NULL;
        size_t field;
        for (field = 0; field < sizeof(fields) / sizeof(fields[0]); field++) {
            if (!strcmp(argv[i], fields[field].name)) { destination = fields[field].value; break; }
        }
        if (destination) *destination = argv[++i];
        else if (!strcmp(argv[i], "--timeout") || !strcmp(argv[i], "--jobs") || !strcmp(argv[i], "--interval")) {
            char *end; const char *option = argv[i]; unsigned long value = strtoul(argv[++i], &end, 10);
            if (!*argv[i] || *end || !value || value > (!strcmp(option, "--jobs") ? 16UL : 300UL)) return f_error("fleet", "invalid_input", "jobs must be 1-16; timeout and interval must be 1-300 seconds");
            if (!strcmp(option, "--jobs")) options->jobs = (unsigned)value;
            else if (!strcmp(option, "--interval")) options->interval = (unsigned)value;
            else { options->seconds = (unsigned)value; options->explicit_timeout = true; }
        } else return f_error("fleet", "invalid_input", "unknown option");
    }
    return NULL;
}
static bool is_tui_data(const char *action) {
    return !strcmp(action, "tui-data") || !strcmp(action, "tui-visual-data");
}

static json_object *aggregate_action(const char *action, const struct fleet_options *options) {
    if (!strcmp(action, "overview")) return f_observation_aggregate(options->seconds, options->jobs);
    if (!strcmp(action, "attention")) {
        json_object *overview = f_observation_aggregate(options->seconds, options->jobs);
        json_object *result = json_object_get_boolean(f_field(overview, "ok")) ? f_attention_aggregate(overview) : overview;
        if (result != overview) json_object_put(overview);
        return result;
    }
    if (!strcmp(action, "list") || !strcmp(action, "doctor")) return f_aggregate(action, options->seconds, options->jobs);
    return NULL;
}

/* A handled command may return NULL after writing its raw output. */
static bool domain_cli(int argc, char **argv, json_object **result) {
    if (!strcmp(argv[0], "discover") || !strcmp(argv[0], "qualify")) *result = hd_cli(argc, argv);
    else if (!strcmp(argv[0], "enroll")) *result = enrollment_cli(argc - 1, argv + 1);
    else if (!strcmp(argv[0], "auth")) *result = auth_cli(argc - 1, argv + 1);
    else if (!strcmp(argv[0], "task")) *result = task_cli(argc - 1, argv + 1);
    else if (!strcmp(argv[0], "retention")) *result = retention_cli(argc - 1, argv + 1);
    else return false;
    return true;
}
json_object *f_cli(int argc, char **argv) {
    const char *action;
    struct fleet_options options = {.seconds = 5, .jobs = 4, .interval = 5, .rest = argc};
    int i;
    json_object *result, *request, *args; struct f_remote remote;
    if (argc < 1) return f_error("fleet", "invalid_input", "use hydra fleet help");
    action = argv[0];
    if (domain_cli(argc, argv, &result)) return result;
    if (!strcmp(action, "help") || !strcmp(action, "--help")) {
        json_object *data = json_object_new_object();
        f_string_add(data, "usage", "fleet discover|qualify ...; fleet enroll review --input QUALIFICATION --candidate ID [--candidate ID...] --output INTENT --project /absolute [--package FILE --sha256 HASH --prefix /path]; fleet enroll apply --input INTENT --confirm DIGEST; fleet list|overview|attention|doctor|reconcile|watch [--timeout N --jobs N]; fleet bootstrap HOST --input PACKAGE --sha256 HASH; fleet init|spawn|signal|cancel|workflow|attach|export|import HOST --project /path -- ARGS");
        return f_success("fleet-help", data);
    }
    result = parse_options(argc, argv, &options);
    if (result) return result;
    if (!strcmp(action, "handshake") && !options.name) return f_handshake();
    if (!strcmp(action, "tui")) return launch_tui();
    if (is_tui_data(action)) {
        (void)f_tui_data(HYDRA_FLEET_TUI_REQUEST_SECONDS, 16, !strcmp(action, "tui-visual-data")); return NULL;
    }
    if (!strcmp(action, "package")) {
        if (!options.source || !options.binary || !options.output) return f_error("fleet-package", "invalid_input", "source, target binary, and output are required");
        result = f_package(options.source, options.binary);
        if (json_object_get_boolean(f_field(result, "ok"))) {
            const char *text = json_object_to_json_string_ext(f_field(result, "data"), JSON_C_TO_STRING_PLAIN); char hash[65];
            if (f_write(options.output, text, strlen(text), false) || f_hash(options.output, hash)) { json_object_put(result); return f_error("fleet-package", "io_failed", "cannot write new package output"); }
            json_object_put(result); request = json_object_new_object(); f_string_add(request, "file", options.output); f_string_add(request, "sha256", hash); result = f_success("fleet-package", request);
        }
        return result;
    }
    if (!strcmp(action, "watch")) {
        while (!f_stopped) {
            struct timespec pause = {0, 100000000}; unsigned tick;
            result = f_aggregate("list", options.seconds, options.jobs); (void)f_emit(result); json_object_put(result); fflush(stdout);
            for (tick = 0; tick < options.interval * 10 && !f_stopped; tick++) nanosleep(&pause, NULL);
        }
        return NULL;
    }
    if (!strcmp(action, "reconcile")) action = "list";
    if (!options.name) {
        result = aggregate_action(action, &options);
        if (result) return result;
    }
    if (!options.name || f_remote_load(options.name, &remote)) return f_error("fleet", "invalid_alias", "register a remote with hydra remote add");
    if (!strcmp(action, "bootstrap")) return bootstrap_alias(&remote, &options);
    result = f_observe(&remote, "handshake", options.seconds);
    if (!json_object_get_boolean(f_field(result, "ok"))) return result;
    if (!supported(result, action)) { json_object_put(result); return f_error("fleet", "capability_unavailable", "remote does not advertise this operation"); }
    json_object_put(result);
    request = json_object_new_object(); args = json_object_new_array();
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(request, "action", action);
    if (options.project) f_string_add(request, "project", options.project);
    if (options.instance) f_string_add(request, "instance", options.instance);
    if (options.run) f_string_add(request, "run", options.run);
    for (i = options.rest; i < argc; i++) json_object_array_add(args, json_object_new_string(argv[i]));
    json_object_object_add(request, "args", args);
    if (!strcmp(action, "import")) {
        char *text = options.input ? f_read(options.input, F_LIMIT) : NULL; json_object *bundle = text ? f_parse(text) : NULL; free(text);
        if (!bundle) { json_object_put(request); return f_error("fleet-import", "invalid_input", "a valid input bundle is required"); }
        json_object_object_add(request, "bundle", bundle);
    }
    result = f_request(&remote, request, options.explicit_timeout ? options.seconds : ((!strcmp(action, "list") || !strcmp(action, "doctor")) ? 5 : 300));
    json_object_put(request);
    if (!json_object_get_boolean(f_field(result, "ok"))) {
        const char *code = f_string(f_field(result, "error"), "code");
        if (code && (!strcmp(code, "timeout") || !strcmp(code, "offline") || !strcmp(code, "cancelled")) && strcmp(action, "list") && strcmp(action, "doctor") && strcmp(action, "admission"))
            f_string_add(f_field(result, "error"), "code", "outcome_unknown");
        return result;
    }
    if (!strcmp(action, "export")) {
        const char *text = json_object_to_json_string_ext(f_field(result, "data"), JSON_C_TO_STRING_PLAIN);
        if (!options.output || f_write(options.output, text, strlen(text), false)) { json_object_put(result); return f_error("fleet-export", "io_failed", "a new output path is required"); }
    }
    if (!strcmp(action, "attach")) {
        const char *branch = options.rest < argc ? argv[options.rest] : NULL;
        json_object *failure = attach_remote(&remote, result, options.project, branch, options.instance, options.seconds);
        json_object_put(result); return failure;
    }
    return result;
}
