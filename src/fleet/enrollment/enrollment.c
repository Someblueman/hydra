#include "fleet/enrollment/enrollment.h"
#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/bundle.h"
#include "fleet/transport/remote.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool abs_path(const char *path) { return path && path[0] == '/' && hd_text(path, F_PATH); }
static bool digest(const char *value) { size_t i; if (!value || strlen(value) != 64) return false; for (i = 0; i < 64; i++) if (!strchr("0123456789abcdef", value[i])) return false; return true; }
static bool unknown_code(const char *code) {
    return code && (!strcmp(code, "outcome_unknown") || !strcmp(code, "timeout") || !strcmp(code, "offline") ||
        !strcmp(code, "cancelled") || !strcmp(code, "transport_failed") || !strcmp(code, "invalid_response"));
}
static bool package_matches(const char *path, const char *expected) {
    char actual[65]; return path && expected && digest(expected) && !f_hash(path, actual) && !strcmp(actual, expected);
}
static json_object *candidate(json_object *qualification, const char *id) {
    json_object *rows = f_field(f_field(qualification, "data"), "candidates"); size_t i;
    if (!json_object_is_type(rows, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!id || !strcmp(f_string(row, "candidate_id"), id)) return row;
    }
    return NULL;
}
static json_object *review(const char *input, const char *output, const char *id, const char *project,
                           const char *package, const char *package_digest, const char *prefix, const char *alias) {
    json_object *qualification = f_read_json(input, F_LIMIT), *row, *data, *intent = NULL, *host, *source;
    const char *target, *fingerprint; char hash[65];
    if (!qualification || !f_number_is(qualification, "schema_version", 1) || !json_object_get_boolean(f_field(qualification, "ok")) ||
        !(row = candidate(qualification, id)) || strcmp(f_string(row, "status"), "compatible") ||
        !(data = f_field(row, "qualification")) || !(fingerprint = f_string(f_field(data, "data"), "peer_fingerprint")) || strncmp(fingerprint, "SHA256:", 7) ||
        !(target = f_string(row, "target")) || !abs_path(project) || (package && (!abs_path(package) || !package_matches(package, package_digest))) ||
        (prefix && !abs_path(prefix))) goto done;
    intent = json_object_new_object(); json_object_object_add(intent, "schema_version", json_object_new_int(1)); f_string_add(intent, "kind", "fleet-enrollment-intent");
    f_string_add(intent, "candidate_id", f_string(row, "candidate_id")); f_string_add(intent, "target", target); f_string_add(intent, "accepted_host_key", fingerprint);
    f_string_add(intent, "principal", f_string(f_field(f_field(row, "resolution"), "data"), "user")); f_string_add(intent, "required_capability", f_string(f_field(qualification, "data"), "required_capability"));
    f_string_add(intent, "project", project); f_string_add(intent, "alias", alias ? alias : f_string(row, "candidate_id"));
    source = f_field(row, "sources"); json_object_object_add(intent, "sources", source ? json_object_get(source) : json_object_new_array());
    host = json_object_new_object(); f_string_add(host, "target", target); f_string_add(host, "fingerprint", fingerprint); f_string_add(host, "project", project); f_string_add(host, "alias", alias ? alias : f_string(row, "candidate_id"));
    if (package) { f_string_add(host, "package", package); f_string_add(host, "package_sha256", package_digest); f_string_add(host, "prefix", prefix ? prefix : ""); }
    json_object_object_add(intent, "host", host); json_object_object_add(intent, "reviewed_at", json_object_new_int64((int64_t)time(NULL)));
    if (f_write(output, json_object_to_json_string_ext(intent, JSON_C_TO_STRING_PLAIN), strlen(json_object_to_json_string_ext(intent, JSON_C_TO_STRING_PLAIN)), true) || f_hash(output, hash)) { json_object_put(qualification); json_object_put(intent); return f_error("fleet-enrollment-review", "io_failed", "cannot write the reviewed intent"); }
    json_object_object_add(intent, "intent_sha256", json_object_new_string(hash));
    /* Rewrite with the digest included; apply hashes a copy with this field removed. */
    if (f_write(output, json_object_to_json_string_ext(intent, JSON_C_TO_STRING_PLAIN), strlen(json_object_to_json_string_ext(intent, JSON_C_TO_STRING_PLAIN)), true)) goto done;
    f_string_add(intent, "output", output); f_string_add(intent, "intent_sha256", hash); json_object_put(qualification); return f_success("fleet-enrollment-review", intent);
done: json_object_put(qualification); json_object_put(intent); return f_error("fleet-enrollment-review", "invalid_review", "qualification must contain an actual peer fingerprint and a compatible candidate");
}
static json_object *apply(const char *path, const char *confirm, unsigned seconds) {
    json_object *intent = f_read_json(path, F_LIMIT), *host, *result, *rows, *row; struct f_remote remote = {0}; char hash[65], verify[F_PATH];
    const char *saved; const char *target; const char *fingerprint; const char *alias; const char *project; const char *package; const char *package_digest; const char *prefix;
    if (!intent || strcmp(f_string(intent, "kind"), "fleet-enrollment-intent") || !digest(confirm) || !digest(f_string(intent, "intent_sha256"))) goto invalid;
    saved = f_string(intent, "intent_sha256"); json_object_object_del(intent, "intent_sha256");
    if (snprintf(verify, sizeof(verify), "%s.verify", path) >= (int)sizeof(verify) || f_write(verify, json_object_to_json_string_ext(intent, JSON_C_TO_STRING_PLAIN), strlen(json_object_to_json_string_ext(intent, JSON_C_TO_STRING_PLAIN)), true) || f_hash(verify, hash)) { unlink(verify); goto invalid; }
    unlink(verify); if (strcmp(hash, saved) || strcmp(hash, confirm)) goto invalid;
    json_object_object_add(intent, "intent_sha256", json_object_new_string(saved));
    host = f_field(intent, "host"); target = f_string(host, "target"); fingerprint = f_string(host, "fingerprint"); alias = f_string(host, "alias"); project = f_string(host, "project");
    package = f_string(host, "package"); package_digest = f_string(host, "package_sha256"); prefix = f_string(host, "prefix");
    if (!target || !fingerprint || !alias || !project || !f_target(target) || !f_name(alias) || !abs_path(project) || f_copy(remote.name, sizeof(remote.name), alias) || f_copy(remote.target, sizeof(remote.target), target) || f_copy(remote.hydra, sizeof(remote.hydra), "hydra")) goto invalid;
    result = json_object_new_object(); f_string_add(result, "intent_sha256", saved); rows = json_object_new_array(); json_object_object_add(result, "hosts", rows); row = json_object_new_object(); f_string_add(row, "alias", alias); f_string_add(row, "target", target);
    { char *actual = f_peer_fingerprint(&remote, seconds); if (!actual) { f_string_add(row, "status", "outcome_unknown"); f_string_add(row, "error", "peer fingerprint unavailable"); } else if (strcmp(actual, fingerprint)) { f_string_add(row, "status", "host_key_changed"); f_string_add(row, "error", "authenticated peer fingerprint differs from reviewed identity"); free(actual); } else {
        if (package) { json_object *boot = f_bootstrap(&remote, package, package_digest, seconds); bool boot_ok = json_object_get_boolean(f_field(boot, "ok")); const char *code = f_string(f_field(boot, "error"), "code"); if (!boot_ok) { f_string_add(row, "status", unknown_code(code) ? "outcome_unknown" : "failed"); f_string_add(row, "error", code ? code : "bootstrap_failed"); json_object_put(boot); json_object_array_add(rows, row); return f_success("fleet-enrollment-apply", result); } json_object_put(boot); }
        { json_object *request = json_object_new_object(), *args = json_object_new_array(); json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(request, "action", "init"); f_string_add(request, "project", project); json_object_array_add(args, json_object_new_string("--no-agent")); json_object_object_add(request, "args", args); result = f_request(&remote, request, seconds); json_object_put(request); if (json_object_get_boolean(f_field(result, "ok"))) { f_string_add(row, "status", "enrolled"); } else { const char *code = f_string(f_field(result, "error"), "code"); f_string_add(row, "status", unknown_code(code) ? "outcome_unknown" : "failed"); f_string_add(row, "error", code ? code : "remote_failed"); } json_object_put(result); if (prefix && *prefix) f_string_add(row, "reviewed_prefix", prefix); }
    }} json_object_array_add(rows, row); return f_success("fleet-enrollment-apply", result);
invalid: json_object_put(intent); return f_error("fleet-enrollment-apply", "review_required", "intent digest, confirmation, or reviewed fields do not match");
}
json_object *enrollment_cli(int argc, char **argv) {
    const char *action = argc ? argv[0] : NULL, *input = NULL, *output = NULL, *id = NULL, *project = NULL, *package = NULL, *sha = NULL, *prefix = NULL, *alias = NULL, *confirm = NULL; unsigned seconds = 300; int i;
    for (i = 1; i < argc; i++) { const char *arg = argv[i]; const char **dst = NULL; if (!strcmp(arg, "--input")) dst = &input; else if (!strcmp(arg, "--output")) dst = &output; else if (!strcmp(arg, "--candidate")) dst = &id; else if (!strcmp(arg, "--project")) dst = &project; else if (!strcmp(arg, "--package")) dst = &package; else if (!strcmp(arg, "--sha256")) dst = &sha; else if (!strcmp(arg, "--prefix")) dst = &prefix; else if (!strcmp(arg, "--alias")) dst = &alias; else if (!strcmp(arg, "--confirm")) dst = &confirm; else if (!strcmp(arg, "--timeout") && i + 1 < argc) { seconds = (unsigned)strtoul(argv[++i], NULL, 10); continue; } else return f_error("fleet-enrollment", "invalid_input", "use enroll review|apply with explicit input, output, and confirmation"); if (!dst || i + 1 >= argc) return f_error("fleet-enrollment", "invalid_input", "option value is required"); *dst = argv[++i]; }
    if (!strcmp(action, "review")) { if (!input || !output || !project) return f_error("fleet-enrollment-review", "invalid_input", "qualification, output, and project are required"); return review(input, output, id, project, package, sha, prefix, alias); }
    if (!strcmp(action, "apply")) { if (!input || !confirm) return f_error("fleet-enrollment-apply", "invalid_input", "intent and exact --confirm digest are required"); return apply(input, confirm, seconds); }
    return f_error("fleet-enrollment", "invalid_input", "use enroll review|apply");
}
