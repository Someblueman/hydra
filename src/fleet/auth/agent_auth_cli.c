#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/auth/agent_auth.h"
#include <stdlib.h>
#include <string.h>

/* Never forward remote errors or arbitrary response fields: a failed transport
 * may echo its stdin, which contains credentials during the copy request. */
static json_object *request_safe(const struct f_remote *remote, json_object *request) {
    json_object *raw = f_request(remote, request, 15), *data = f_field(raw, "data"), *result = NULL, *clean;
    const char *destination = f_string(data, "destination"), *before = f_string(data, "before"), *state = f_string(data, "credential_state"), *provider = f_string(data, "provider_state");
    if (!json_object_get_boolean(f_field(raw, "ok"))) {
        const char *code = f_string(f_field(raw, "error"), "code");
        if (code && !strcmp(code, "unsafe_store"))
            result = f_error("fleet-auth", "unsafe_store", "host credential store is unavailable, invalid, or has unsafe ownership or permissions");
        else result = f_error("fleet-auth", code && !strcmp(code, "stale_preview") ? "stale_preview" : "auth_request_failed", "authentication request failed; inspect host status before retrying");
    } else if (destination && destination[0] == '/' && strlen(destination) < F_PATH && before &&
               (!strcmp(before, "absent") || (strlen(before) == 64 && strspn(before, "0123456789abcdef") == 64)) &&
               state && (!strcmp(state, "present") || !strcmp(state, "absent")) &&
               (!f_string(request, "provider") || provider) &&
               (!provider || !strcmp(provider, "present") || !strcmp(provider, "absent"))) {
        /* This metadata is accepted only on status, before any secret is sent. */
        clean = json_object_new_object(); f_string_add(clean, "destination", destination); f_string_add(clean, "before", before); f_string_add(clean, "credential_state", state);
        if (provider) f_string_add(clean, "provider_state", provider);
        result = f_success("fleet-auth", clean);
    } else result = f_error("fleet-auth", "invalid_response", "invalid authentication response");
    json_object_put(raw); return result;
}
static json_object *login(const struct f_remote *remote, const char *agent, const char *executable) {
    const char *args = !strcmp(agent, "codex") ? "login --device-auth" : !strcmp(agent, "claude") ? "auth login" : !strcmp(agent, "opencode") ? "auth login" : !strcmp(agent, "cursor") ? "login" : "";
    const char *program = executable ? executable : !strcmp(agent, "cursor") ? "cursor-agent" : agent;
    char command[F_PATH * 4]; char *quoted = f_quote(program); struct f_capture cap = {0};
    if (!quoted || snprintf(command, sizeof(command), "exec %s %s", quoted, args) >= (int)sizeof(command)) { free(quoted); return f_error("fleet-auth", "invalid_input", "invalid login executable"); }
    free(quoted); (void)f_ssh(remote, command, NULL, 0, 300, true, &cap); f_capture_free(&cap);
    return f_error("fleet-auth", "login_failed", "cannot start native sign-in over interactive SSH");
}
/* Parsed strings borrow argv; remote is loaded once during validation. */
enum auth_operation { AUTH_STATUS, AUTH_PREVIEW, AUTH_COPY, AUTH_LOGIN };
struct auth_options {
    enum auth_operation operation;
    const char *host, *agent, *provider, *source, *approve, *executable;
    bool map;
};
static int validate_options(struct auth_options *options, struct f_remote *remote) {
    if (!options->host || !options->agent || (!auth_agent(options->agent) && (options->operation != AUTH_LOGIN || (strcmp(options->agent, "agy") && strcmp(options->agent, "cursor")))) || f_remote_load(options->host, remote)) return -1;
    options->map = !strcmp(options->agent, "pi") || !strcmp(options->agent, "opencode");
    if ((options->provider && (!options->map || !f_name(options->provider) || strlen(options->provider) > 128)) ||
        (options->executable && (options->operation != AUTH_LOGIN || options->executable[0] != '/')) ||
        (options->approve && options->operation != AUTH_COPY) || (options->source && (options->operation == AUTH_STATUS || options->operation == AUTH_LOGIN)) ||
        (options->operation == AUTH_LOGIN && options->provider)) return -1;
    if (options->operation == AUTH_LOGIN) return 0;
    if ((options->operation == AUTH_PREVIEW || options->operation == AUTH_COPY) && options->map && !options->provider) return -1;
    if (options->operation == AUTH_COPY && (!options->approve || strlen(options->approve) != 64 || strspn(options->approve, "0123456789abcdef") != 64)) return -1;
    return 0;
}

static int parse_options(int argc, char **argv, struct auth_options *options, struct f_remote *remote) {
    const struct { const char *name; const char **value; } flags[] = {
        {"--agent", &options->agent}, {"--provider", &options->provider},
        {"--source", &options->source}, {"--approve", &options->approve},
        {"--executable", &options->executable}
    };
    int i;
    memset(options, 0, sizeof(*options));
    if (!strcmp(argv[0], "status")) options->operation = AUTH_STATUS;
    else if (!strcmp(argv[0], "preview")) options->operation = AUTH_PREVIEW;
    else if (!strcmp(argv[0], "copy")) options->operation = AUTH_COPY;
    else if (!strcmp(argv[0], "login")) options->operation = AUTH_LOGIN;
    else return -1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        if (argv[i][0] != '-' && !options->host) { options->host = argv[i]; continue; }
        if (i + 1 >= argc) return -1;
        bool found = false;
        for (size_t j = 0; j < sizeof(flags) / sizeof(flags[0]); j++) {
            if (strcmp(argv[i], flags[j].name)) continue;
            *flags[j].value = argv[++i];
            found = true;
            break;
        }
        if (!found) return -1;
    }
    return validate_options(options, remote);
}

static bool supported(const struct f_remote *remote) {
    json_object *response;
    bool supported = false;
    response = f_observe(remote, "handshake", 5);
    if (json_object_get_boolean(f_field(response, "ok"))) {
        json_object *caps = f_field(f_field(response, "data"), "capabilities"); size_t k;
        for (k = 0; k < json_object_array_length(caps); k++) {
            const char *cap = json_object_get_string(json_object_array_get_idx(caps, k));
            if (cap && !strcmp(cap, "agent-auth")) supported = true;
        }
    }
    json_object_put(response);
    return supported;
}

/* Caller owns returned preview; all source objects and strings are borrowed. */
static json_object *preview_plan(const struct auth_options *options, const struct f_remote *remote,
                                 const char *path, const char *source_hash, const char *payload_hash,
                                 json_object *response) {
    json_object *plan = json_object_new_object();
    f_string_add(plan, "host", remote->name); f_string_add(plan, "target", remote->target); f_string_add(plan, "hydra", remote->hydra); f_string_add(plan, "hydra_home", remote->home);
    f_string_add(plan, "agent", options->agent); if (options->provider) f_string_add(plan, "provider", options->provider);
    f_string_add(plan, "source", path); f_string_add(plan, "source_sha256", source_hash); f_string_add(plan, "credential_sha256", payload_hash);
    f_string_add(plan, "destination", f_string(f_field(response, "data"), "destination"));
    f_string_add(plan, "before", f_string(f_field(response, "data"), "before"));
    const char *change = "replace";
    if (!strcmp(f_string(plan, "before"), "absent")) change = "create";
    else if (options->map && !strcmp(f_string(f_field(response, "data"), "provider_state"), "absent")) change = "add-provider";
    f_string_add(plan, "change", change);
    return plan;
}

static json_object *credential_command(const struct auth_options *options, const struct f_remote *remote) {
    char path[F_PATH], source_hash[65], payload_hash[65], plan_hash[65];
    json_object *request = NULL, *response = NULL, *local = NULL, *payload = NULL, *plan = NULL, *result = NULL;
    if (!supported(remote))
        return f_error("fleet-auth", "capability_unavailable", "host does not advertise agent authentication support");
    request = json_object_new_object(); json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL));
    json_object_object_add(request, "auth_protocol", json_object_new_int(1));
    f_string_add(request, "action", "auth"); f_string_add(request, "operation", "status"); f_string_add(request, "agent", options->agent);
    if (options->provider) f_string_add(request, "provider", options->provider);
    response = request_safe(remote, request);
    if (!json_object_get_boolean(f_field(response, "ok")) || options->operation == AUTH_STATUS) { result = response; response = NULL; goto done; }
    if ((options->source ? f_copy(path, sizeof(path), options->source) : auth_path(options->agent, path)) || auth_read(path, &local, source_hash) || !local) {
        result = f_error("fleet-auth", "source_unavailable", "no safe portable credential file; use native host login or an explicit private source file"); goto done;
    }
    payload = options->map ? f_field(local, options->provider) : local;
    if (!auth_valid(options->agent, payload) || auth_hash(json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN), payload_hash)) {
        result = f_error("fleet-auth", "invalid_credential", "selected credential is missing or unsupported; dynamic key recipes are not copied"); goto done;
    }
    plan = preview_plan(options, remote, path, source_hash, payload_hash, response);
    if (auth_hash(json_object_to_json_string_ext(plan, JSON_C_TO_STRING_PLAIN), plan_hash)) { result = f_error("fleet-auth", "hash_failed", "cannot bind authentication preview"); goto done; }
    f_string_add(plan, "approval_sha256", plan_hash);
    if (options->operation == AUTH_PREVIEW) { result = f_success("fleet-auth-preview", plan); plan = NULL; goto done; }
    if (!options->approve || strcmp(options->approve, plan_hash)) { result = f_error("fleet-auth", "stale_preview", "source or destination changed, or approval did not match; preview again"); goto done; }
    f_string_add(request, "operation", "copy"); f_string_add(request, "before", f_string(plan, "before")); f_string_add(request, "destination", f_string(plan, "destination"));
    json_object_object_add(request, "credential", json_object_get(payload));
    json_object_put(response); response = f_request(remote, request, 15);
    /* After sending a secret, only the boolean success and a fixed error enum
     * are used. Even a purported successful response may echo the request. */
    if (json_object_get_boolean(f_field(response, "ok"))) {
        f_string_add(plan, "result", "copied"); result = f_success("fleet-auth-copy", plan); plan = NULL;
    } else {
        const char *code = f_string(f_field(response, "error"), "code");
        result = f_error("fleet-auth", code && !strcmp(code, "stale_preview") ? "stale_preview" : "copy_unconfirmed", "copy was not confirmed; inspect host status before trying again");
    }
done:
    json_object_put(request); json_object_put(response); json_object_put(local); json_object_put(plan); return result;
}

json_object *auth_cli(int argc, char **argv) {
    struct auth_options options;
    struct f_remote remote;
    if (!argc || !strcmp(argv[0], "help") || !strcmp(argv[0], "--help")) {
        json_object *result = json_object_new_object();
        f_string_add(result, "usage", "fleet auth status|preview|copy HOST --agent codex|pi|opencode|claude [--provider NAME] [--source FILE] [--approve PLAN_SHA256]; fleet auth login HOST --agent codex|pi|opencode|claude|agy|cursor [--executable /path] (Pi: enter /login; agy: follow startup sign-in)");
        return f_success("fleet-auth-help", result);
    }
    if (parse_options(argc, argv, &options, &remote))
        return f_error("fleet-auth", "invalid_input", "use fleet auth help; copy requires the exact preview approval hash and Pi/OpenCode require --provider");
    if (options.operation == AUTH_LOGIN) return login(&remote, options.agent, options.executable);
    return credential_command(&options, &remote);
}
