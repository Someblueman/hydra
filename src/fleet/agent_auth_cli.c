#include "agent_auth.h"
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
json_object *auth_cli(int argc, char **argv) {
    const char *op, *host = NULL, *agent = NULL, *provider = NULL, *source = NULL, *approve = NULL, *executable = NULL;
    struct f_remote remote; char path[F_PATH], source_hash[65], payload_hash[65], plan_hash[65];
    json_object *request = NULL, *response = NULL, *local = NULL, *payload = NULL, *plan = NULL, *result = NULL;
    int i; bool map, supported = false;
    if (!argc || !strcmp(argv[0], "help") || !strcmp(argv[0], "--help")) {
        result = json_object_new_object();
        f_string_add(result, "usage", "fleet auth status|preview|copy HOST --agent codex|pi|opencode|claude [--provider NAME] [--source FILE] [--approve PLAN_SHA256]; fleet auth login HOST --agent codex|pi|opencode|claude|agy|cursor [--executable /path] (Pi: enter /login; agy: follow startup sign-in)");
        return f_success("fleet-auth-help", result);
    }
    op = argv[0];
    if (strcmp(op, "status") && strcmp(op, "preview") && strcmp(op, "copy") && strcmp(op, "login")) goto invalid;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        if (argv[i][0] != '-' && !host) { host = argv[i]; continue; }
        if (i + 1 >= argc) goto invalid;
        if (!strcmp(argv[i], "--agent")) agent = argv[++i];
        else if (!strcmp(argv[i], "--provider")) provider = argv[++i];
        else if (!strcmp(argv[i], "--source")) source = argv[++i];
        else if (!strcmp(argv[i], "--approve")) approve = argv[++i];
        else if (!strcmp(argv[i], "--executable")) executable = argv[++i];
        else goto invalid;
    }
    if (!host || !agent || (!auth_agent(agent) && (strcmp(op, "login") || (strcmp(agent, "agy") && strcmp(agent, "cursor")))) || f_remote_load(host, &remote)) goto invalid;
    map = !strcmp(agent, "pi") || !strcmp(agent, "opencode");
    if ((provider && (!map || !f_name(provider) || strlen(provider) > 128)) ||
        (executable && (strcmp(op, "login") || executable[0] != '/')) ||
        (approve && strcmp(op, "copy")) || (source && (!strcmp(op, "status") || !strcmp(op, "login"))) ||
        (!strcmp(op, "login") && provider)) goto invalid;
    if (!strcmp(op, "login")) return login(&remote, agent, executable);
    if ((!strcmp(op, "preview") || !strcmp(op, "copy")) && map && !provider) goto invalid;
    if (!strcmp(op, "copy") && (!approve || strlen(approve) != 64 || strspn(approve, "0123456789abcdef") != 64)) goto invalid;
    response = f_observe(&remote, "handshake", 5);
    if (json_object_get_boolean(f_field(response, "ok"))) {
        json_object *caps = f_field(f_field(response, "data"), "capabilities"); size_t k;
        for (k = 0; k < json_object_array_length(caps); k++) {
            const char *cap = json_object_get_string(json_object_array_get_idx(caps, k));
            if (cap && !strcmp(cap, "agent-auth")) supported = true;
        }
    }
    json_object_put(response); response = NULL;
    if (!supported) { result = f_error("fleet-auth", "capability_unavailable", "host does not advertise agent authentication support"); goto done; }
    request = json_object_new_object(); json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL));
    json_object_object_add(request, "auth_protocol", json_object_new_int(1));
    f_string_add(request, "action", "auth"); f_string_add(request, "operation", "status"); f_string_add(request, "agent", agent);
    if (provider) f_string_add(request, "provider", provider);
    response = request_safe(&remote, request);
    if (!json_object_get_boolean(f_field(response, "ok")) || !strcmp(op, "status")) { result = response; response = NULL; goto done; }
    if ((source ? f_copy(path, sizeof(path), source) : auth_path(agent, path)) || auth_read(path, &local, source_hash) || !local) {
        result = f_error("fleet-auth", "source_unavailable", "no safe portable credential file; use native host login or an explicit private source file"); goto done;
    }
    payload = map ? f_field(local, provider) : local;
    if (!auth_valid(agent, payload) || auth_hash(json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN), payload_hash)) {
        result = f_error("fleet-auth", "invalid_credential", "selected credential is missing or unsupported; dynamic key recipes are not copied"); goto done;
    }
    plan = json_object_new_object();
    f_string_add(plan, "host", remote.name); f_string_add(plan, "target", remote.target); f_string_add(plan, "hydra", remote.hydra); f_string_add(plan, "hydra_home", remote.home);
    f_string_add(plan, "agent", agent); if (provider) f_string_add(plan, "provider", provider);
    f_string_add(plan, "source", path); f_string_add(plan, "source_sha256", source_hash); f_string_add(plan, "credential_sha256", payload_hash);
    f_string_add(plan, "destination", f_string(f_field(response, "data"), "destination"));
    f_string_add(plan, "before", f_string(f_field(response, "data"), "before"));
    f_string_add(plan, "change", !strcmp(f_string(plan, "before"), "absent") ? "create" : map && !strcmp(f_string(f_field(response, "data"), "provider_state"), "absent") ? "add-provider" : "replace");
    if (auth_hash(json_object_to_json_string_ext(plan, JSON_C_TO_STRING_PLAIN), plan_hash)) { result = f_error("fleet-auth", "hash_failed", "cannot bind authentication preview"); goto done; }
    f_string_add(plan, "approval_sha256", plan_hash);
    if (!strcmp(op, "preview")) { result = f_success("fleet-auth-preview", plan); plan = NULL; goto done; }
    if (!approve || strcmp(approve, plan_hash)) { result = f_error("fleet-auth", "stale_preview", "source or destination changed, or approval did not match; preview again"); goto done; }
    f_string_add(request, "operation", "copy"); f_string_add(request, "before", f_string(plan, "before")); f_string_add(request, "destination", f_string(plan, "destination"));
    json_object_object_add(request, "credential", json_object_get(payload));
    json_object_put(response); response = f_request(&remote, request, 15);
    /* After sending a secret, only the boolean success and a fixed error enum
     * are used. Even a purported successful response may echo the request. */
    if (json_object_get_boolean(f_field(response, "ok"))) {
        f_string_add(plan, "result", "copied"); result = f_success("fleet-auth-copy", plan); plan = NULL;
    } else {
        const char *code = f_string(f_field(response, "error"), "code");
        result = f_error("fleet-auth", code && !strcmp(code, "stale_preview") ? "stale_preview" : "copy_unconfirmed", "copy was not confirmed; inspect host status before trying again");
    }
    goto done;
invalid:
    result = f_error("fleet-auth", "invalid_input", "use fleet auth help; copy requires the exact preview approval hash and Pi/OpenCode require --provider");
done:
    json_object_put(request); json_object_put(response); json_object_put(local); json_object_put(plan); return result;
}
