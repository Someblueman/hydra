#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <string.h>

static bool has_capability(json_object *result, const char *required) {
    json_object *caps = f_field(f_field(result, "data"), "capabilities"); size_t i;
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *cap = f_text(json_object_array_get_idx(caps, i));
        if (cap && !strcmp(cap, required)) return true;
    }
    return false;
}
static void classify_key(json_object *result) {
    json_object *error = f_field(result, "error");
    const char *code = f_string(error, "code"), *message = f_string(error, "message");
    if (!code || strcmp(code, "host_key_failed")) return;
    if (message && strstr(message, "REMOTE HOST IDENTIFICATION HAS CHANGED"))
        f_string_add(error, "code", "host_key_changed");
    else if (message && strstr(message, "No ") && strstr(message, "host key is known"))
        f_string_add(error, "code", "host_key_unknown");
    /* Generic verification errors stay ambiguous; never invent a diagnosis. */
    f_string_add(error, "recovery", "review the host key through your existing OpenSSH process; Hydra did not accept or change it");
}
static json_object *check_handshake(json_object *result, const char *capability) {
    json_object *failure;
    if (!json_object_get_boolean(f_field(result, "ok"))) return result;
    if (strcmp(f_string(result, "command"), "fleet-handshake"))
        failure = f_error("fleet-qualify", "invalid_response", "expected a Hydra handshake response");
    else if (!f_handshake_compatible(f_field(result, "data")))
        failure = f_error("fleet-qualify", "version_mismatch", "remote Hydra or fleet protocol is incompatible");
    else if (!has_capability(result, capability))
        failure = f_error("fleet-qualify", "capability_unavailable", "handshake does not advertise the required capability");
    else return result;
    json_object_object_add(failure, "data", json_object_get(f_field(result, "data")));
    json_object_put(result); return failure;
}
json_object *hd_probe(json_object *candidate, const char *config, const struct hd_options *options) {
    struct f_remote remote = {0}; json_object *result, *request;
    if (f_copy(remote.target, sizeof(remote.target), f_string(candidate, "target")) ||
        f_copy(remote.hydra, sizeof(remote.hydra), "hydra") ||
        f_copy(remote.ssh_config, sizeof(remote.ssh_config), config))
        return f_error("fleet-qualify", "invalid_input", "candidate transport exceeds limits");
    request = json_object_new_object();
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL));
    f_string_add(request, "action", "handshake");
    result = f_request(&remote, request, options->seconds); json_object_put(request);
    classify_key(result);
    if (!json_object_get_boolean(f_field(result, "ok"))) {
        /* SSH stderr can contain an operator's ProxyCommand arguments. Keep
         * the typed diagnosis, not potentially credential-bearing text. */
        f_string_add(f_field(result, "error"), "message", "strict SSH handshake failed; see the typed code and review the selected SSH configuration");
    }
    result = check_handshake(result, options->capability);
    /* f_request attaches the peer-reported fingerprint from the same strict
     * authenticated connection; known_hosts text is never used as identity. */
    return result;
}
