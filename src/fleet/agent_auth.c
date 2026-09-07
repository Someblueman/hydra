#include "agent_auth.h"
#include <stdlib.h>
#include <string.h>

bool auth_agent(const char *agent) {
    return agent && (!strcmp(agent, "codex") || !strcmp(agent, "pi") || !strcmp(agent, "opencode") || !strcmp(agent, "claude"));
}
static bool token(json_object *obj, const char *key) {
    const char *s = f_string(obj, key);
    return s && *s && *s != '!' && *s != '$';
}
bool auth_valid(const char *agent, json_object *value) {
    const char *type = f_string(value, "type");
    if (!json_object_is_type(value, json_type_object)) return false;
    if (!strcmp(agent, "codex")) return token(value, "OPENAI_API_KEY") ||
        (token(f_field(value, "tokens"), "access_token") && token(f_field(value, "tokens"), "refresh_token"));
    if (!strcmp(agent, "claude")) return token(f_field(value, "claudeAiOauth"), "accessToken") && token(f_field(value, "claudeAiOauth"), "refreshToken");
    if (!type) return false;
    if (!strcmp(type, "oauth")) return token(value, "access") && token(value, "refresh");
    return ((!strcmp(agent, "pi") && !strcmp(type, "api_key")) || (!strcmp(agent, "opencode") && !strcmp(type, "api"))) && token(value, "key");
}
json_object *auth_serve(json_object *request) {
    const char *agent = f_string(request, "agent"), *op = f_string(request, "operation"), *provider = f_string(request, "provider");
    char path[F_PATH], digest[65]; json_object *stored = NULL, *data = NULL, *result = NULL, *payload = f_field(request, "credential");
    bool map; int rc;
    if (!auth_agent(agent) || !op || (strcmp(op, "status") && strcmp(op, "copy")) || !f_number_is(request, "auth_protocol", 1))
        return f_error("fleet-auth", "invalid_input", "unsupported authentication request");
    json_object_object_foreach(request, key, value) {
        (void)value;
        if (strcmp(key, "protocol") && strcmp(key, "action") && strcmp(key, "auth_protocol") && strcmp(key, "agent") && strcmp(key, "operation") && strcmp(key, "provider") && strcmp(key, "credential") && strcmp(key, "before") && strcmp(key, "destination"))
            return f_error("fleet-auth", "invalid_input", "unknown authentication request field");
    }
    map = !strcmp(agent, "pi") || !strcmp(agent, "opencode");
    if ((provider && (!map || !f_name(provider) || strlen(provider) > 128)) || auth_path(agent, path) || auth_read(path, &stored, digest))
        return f_error("fleet-auth", "unsafe_store", "credential store is unavailable, invalid, or has unsafe ownership or permissions");
    if (!strcmp(op, "copy")) {
        const char *before = f_string(request, "before"), *destination = f_string(request, "destination");
        if (!before || !destination || strcmp(path, destination) || strcmp(before, digest)) { result = f_error("fleet-auth", "stale_preview", "destination changed; preview again"); goto done; }
        if ((map && !provider) || !auth_valid(agent, payload) || strlen(json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN)) > AUTH_LIMIT) {
            result = f_error("fleet-auth", "invalid_credential", "a supported static credential is required"); goto done;
        }
        if (map) {
            if (!stored) stored = json_object_new_object();
            json_object_object_add(stored, provider, json_object_get(payload));
        } else { json_object_put(stored); stored = json_object_get(payload); }
        rc = auth_store(path, digest, stored);
        if (rc) { result = f_error("fleet-auth", rc == -2 ? "stale_preview" : "store_failed", "copy was not confirmed; inspect host status before trying again"); goto done; }
    }
    data = json_object_new_object();
    f_string_add(data, "destination", path); f_string_add(data, "before", digest);
    f_string_add(data, "credential_state", stored ? "present" : "absent");
    if (provider) f_string_add(data, "provider_state", f_field(stored, provider) ? "present" : "absent");
    result = f_success("fleet-auth", data);
done:
    json_object_put(stored); return result;
}
