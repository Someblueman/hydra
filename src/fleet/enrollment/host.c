#include "fleet/enrollment/enrollment.h"
#include "fleet/discovery/discovery.h"
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/transport/remote.h"
#include "fleet/transport/bundle.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool has_capability(json_object *handshake, const char *required) {
    json_object *caps = f_field(handshake, "capabilities"); size_t i;
    if (!required || !json_object_is_type(caps, json_type_array)) return false;
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *cap = f_text(json_object_array_get_idx(caps, i));
        if (cap && !strcmp(cap, required)) return true;
    }
    return false;
}
static json_object *request(const struct f_remote *remote, const char *action, json_object *host, unsigned seconds) {
    json_object *req = json_object_new_object(), *result;
    json_object_object_add(req, "protocol", json_object_new_int(F_PROTOCOL));
    f_string_add(req, "action", action); f_string_add(req, "project", f_string(host, "project"));
    result = f_request(remote, req, seconds); json_object_put(req); return result;
}
static bool host_shape(json_object *host) {
    const char *fingerprint = f_string(host, "fingerprint");
    return f_target(f_string(host, "target")) && f_name(f_string(host, "alias")) &&
        hd_text(f_string(host, "principal"), 128) && enrollment_path(f_string(host, "project")) &&
        fingerprint && !strncmp(fingerprint, "SHA256:", 7) && hd_text(fingerprint, 256) &&
        f_number_is(host, "fleet_protocol", F_PROTOCOL) && hd_text(f_string(host, "required_capability"), 128);
}
static bool local_policy(json_object *host, unsigned seconds) {
    const char *config = f_string(host, "ssh_config"), *package = f_string(host, "package");
    json_object *resolution; bool valid; char policy[F_PATH];
    if (config && !enrollment_file_matches(config, f_string(host, "ssh_config_sha256"))) return false;
    if (package && (!enrollment_file_matches(package, f_string(host, "package_sha256")) || !enrollment_path(f_string(host, "prefix")))) return false;
    if (hd_config(policy, config)) return false;
    resolution = hd_resolve(f_string(host, "target"), policy, seconds); unlink(policy);
    valid = json_object_get_boolean(f_field(resolution, "ok")) && json_object_equal(f_field(resolution, "data"), f_field(host, "resolution"));
    json_object_put(resolution); return valid;
}
static const char *authenticate(json_object *host, struct f_remote *remote, unsigned seconds) {
    char *peer; bool matches;
    if (!host_shape(host) || !local_policy(host, seconds)) return "review_required";
    if (f_copy(remote->target, sizeof(remote->target), f_string(host, "target")) ||
        f_copy(remote->name, sizeof(remote->name), f_string(host, "alias")) ||
        f_copy(remote->principal, sizeof(remote->principal), f_string(host, "principal")) ||
        f_copy(remote->hydra, sizeof(remote->hydra), "hydra") || hd_config(remote->ssh_config, f_string(host, "ssh_config"))) return "invalid_intent";
    remote->multiplex = true; peer = f_peer_fingerprint(remote, seconds);
    if (!peer) return "outcome_unknown";
    matches = !strcmp(peer, f_string(host, "fingerprint")); free(peer);
    return matches ? NULL : "host_key_changed";
}
/* Older compatible receivers can be upgraded by an explicitly pinned package.
 * Inspect their project through the existing authenticated SSH channel first;
 * durable enrollment support is mandatory before initialization. */
static json_object *project_probe(json_object *host, struct f_remote *remote, unsigned seconds) {
    const char *project = f_string(host, "project"); char *quoted = f_quote(project), command[F_PATH * 4 + 128];
    struct f_capture cap = {0}; json_object *result = NULL;
    if (!quoted || snprintf(command, sizeof(command), "git -C %s rev-parse --show-toplevel", quoted) >= (int)sizeof(command)) goto done;
    if (f_ssh(remote, command, NULL, 0, seconds, false, &cap) || cap.status || !cap.out[0]) goto done;
    cap.out[strcspn(cap.out, "\r\n")] = '\0';
    json_object *data = json_object_new_object(); f_string_add(data, "requested_project", project);
    f_string_add(data, "canonical_project", cap.out); f_string_add(data, "probe", "authenticated-git");
    result = f_success("fleet-enrollment-preflight", data);
done:
    free(quoted); f_capture_free(&cap);
    return result ? result : f_error("fleet-enrollment", "invalid_project", "reviewed project is not an accessible Git working tree");
}
static bool compatible_host(json_object *data, json_object *host, bool require_enrollment) {
    if (!f_handshake_compatible(data) || !has_capability(data, f_string(host, "required_capability"))) return false;
    return !require_enrollment || has_capability(data, "enrollment-init");
}
static const char *preflight(json_object *host, struct f_remote *remote, json_object *row, unsigned seconds, bool require_enrollment) {
    json_object *reply = request(remote, "handshake", host, seconds), *data = f_field(reply, "data"); const char *status = NULL;
    bool native_probe = has_capability(data, "enrollment-preflight");
    if (!json_object_get_boolean(f_field(reply, "ok"))) status = "outcome_unknown";
    else if (!compatible_host(data, host, require_enrollment)) status = "capability_changed";
    json_object_put(reply);
    if (status) return status;
    reply = native_probe ? request(remote, "enrollment-preflight", host, seconds) : project_probe(host, remote, seconds);
    data = f_field(reply, "data");
    if (!json_object_get_boolean(f_field(reply, "ok"))) {
        const char *code = f_string(f_field(reply, "error"), "code");
        status = code && !strcmp(code, "invalid_project") ? "project_unavailable" : "outcome_unknown";
    }
    else if (!json_object_equal(f_field(data, "requested_project"), f_field(host, "project"))) status = "project_changed";
    json_object_object_add(row, "preflight", reply); return status;
}
static bool set_phase(json_object *row, const char *phase, json_object *progress, const char *path) {
    f_string_add(row, "phase", phase); f_string_add(row, "status", "outcome_unknown");
    return enrollment_write(path, progress) == 0;
}
static const char *install_package(json_object *host, struct f_remote *remote, json_object *row, json_object *progress, const char *path, unsigned seconds) {
    const char *package = f_string(host, "package"), *phase = f_string(row, "phase"); json_object *reply; bool ok;
    if (!package) return NULL;
    if (f_stopped) return "cancelled";
    if (!strcmp(phase, "preflight")) {
        if (!set_phase(row, "install", progress, path)) return "state_unavailable";
        if (f_stopped) return "cancelled";
        reply = f_bootstrap(remote, package, f_string(host, "package_sha256"), f_string(host, "prefix"), seconds);
    } else reply = f_bootstrap_reconcile(remote, package, f_string(host, "package_sha256"), f_string(host, "prefix"), seconds);
    ok = json_object_get_boolean(f_field(reply, "ok")); json_object_object_add(row, "installation", reply);
    return ok ? NULL : "outcome_unknown";
}
static const char *initialize(json_object *host, struct f_remote *remote, json_object *row, json_object *progress, const char *path, unsigned seconds) {
    json_object *req, *args, *reply; bool ok;
    if (!strcmp(f_string(row, "phase"), "alias")) return NULL;
    if (f_stopped) return "cancelled";
    if (!set_phase(row, "init", progress, path)) return "state_unavailable";
    if (f_stopped) return "cancelled";
    req = json_object_new_object(); args = json_object_new_array();
    json_object_object_add(req, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(req, "action", "init");
    f_string_add(req, "project", f_string(host, "project"));
    f_string_add(req, "enrollment_operation_id", f_string(row, "operation_id"));
    f_string_add(req, "expected_peer_fingerprint", f_string(host, "fingerprint"));
    json_object_array_add(args, json_object_new_string("--no-agent")); json_object_object_add(req, "args", args);
    reply = f_request(remote, req, seconds); json_object_put(req); ok = json_object_get_boolean(f_field(reply, "ok"));
    json_object_object_add(row, "initialization", reply);
    if (!ok) return "outcome_unknown";
    return set_phase(row, "alias", progress, path) ? NULL : "state_unavailable";
}
static bool alias_record(json_object *host, const struct f_remote *remote, struct f_remote *alias) {
    *alias = *remote; alias->multiplex = false;
    if (f_copy(alias->ssh_config, sizeof(alias->ssh_config), f_string(host, "ssh_config")) ||
        f_copy(alias->project, sizeof(alias->project), f_string(host, "project")) ||
        f_copy(alias->accepted_host_key, sizeof(alias->accepted_host_key), f_string(host, "fingerprint"))) return false;
    if (f_string(host, "package") && f_path(alias->hydra, sizeof(alias->hydra), f_string(host, "prefix"), "bin/hydra")) return false;
    return true;
}
static const char *alias_check(json_object *host, const struct f_remote *remote, bool publish) {
    struct f_remote alias;
    if (!alias_record(host, remote, &alias)) return "invalid_intent";
    return f_remote_enrolled(&alias, publish) ? "alias_conflict" : NULL;
}
json_object *enrollment_host_apply(json_object *host, json_object *row, json_object *progress, const char *path, unsigned seconds) {
    struct f_remote remote = {0}; char config[F_PATH] = ""; const char *status;
    status = authenticate(host, &remote, seconds); f_copy(config, sizeof(config), remote.ssh_config);
    if (!status) status = alias_check(host, &remote, false);
    if (!status) status = preflight(host, &remote, row, seconds, !f_string(host, "package"));
    if (!status) status = install_package(host, &remote, row, progress, path, seconds);
    if (!status && f_string(host, "package")) status = preflight(host, &remote, row, seconds, true);
    if (!status) status = initialize(host, &remote, row, progress, path, seconds);
    f_peer_close(&remote); if (config[0]) unlink(config);
    if (!status) status = alias_check(host, &remote, true);
    if (!status) { f_string_add(row, "phase", "complete"); status = "enrolled"; }
    f_string_add(row, "status", status);
    if (!strcmp(status, "state_unavailable") || enrollment_write(path, progress))
        return f_error("fleet-enrollment", "state_unavailable", "progress could not be saved; remote outcome must be reconciled before any retry");
    return NULL;
}
