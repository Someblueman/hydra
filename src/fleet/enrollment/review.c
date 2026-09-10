#include "fleet/enrollment/enrollment.h"
#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <string.h>
#include <time.h>

static bool config_source(json_object *row, const char **config) {
    const char *explicit_config = f_string(row, "ssh_config");
    json_object *sources = f_field(row, "sources"); size_t i;
    *config = NULL;
    if (explicit_config) {
        if (!enrollment_path(explicit_config)) return false;
        *config = explicit_config; return true;
    }
    if (!json_object_is_type(sources, json_type_array)) return true;
    for (i = 0; i < json_object_array_length(sources); i++) {
        json_object *source = json_object_array_get_idx(sources, i);
        const char *kind = f_string(source, "kind"), *locator = f_string(source, "locator");
        if (kind && !strcmp(kind, "ssh-config")) {
            if (enrollment_path(locator)) { *config = locator; return true; }
        }
    }
    return true;
}
static json_object *selected_candidate(json_object *qualification, const char *id) {
    json_object *rows = f_field(f_field(qualification, "data"), "candidates"); size_t i;
    if (!json_object_is_type(rows, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i); const char *candidate_id = f_string(row, "candidate_id");
        if (candidate_id && !strcmp(candidate_id, id)) return row;
    }
    return NULL;
}
static bool compatible_candidate(json_object *row) {
    const char *status = f_string(row, "status"), *fingerprint;
    json_object *data = f_field(f_field(row, "qualification"), "data"), *resolution = f_field(f_field(row, "resolution"), "data");
    fingerprint = f_string(data, "peer_fingerprint");
    return status && !strcmp(status, "compatible") && f_target(f_string(row, "target")) &&
        f_handshake_compatible(data) && fingerprint && !strncmp(fingerprint, "SHA256:", 7) &&
        hd_text(fingerprint, 256) && strlen(fingerprint) > 7 &&
        json_object_is_type(f_field(row, "sources"), json_type_array) &&
        json_object_is_type(resolution, json_type_object) && enrollment_digest(f_string(resolution, "policy_sha256"));
}
static json_object *review_host(json_object *row, const struct enrollment_options *options, const char *required) {
    const char *config, *alias = options->alias ? options->alias : f_string(row, "candidate_id");
    json_object *host = json_object_new_object(), *resolved = f_field(f_field(row, "resolution"), "data"); char hash[65];
    if (!config_source(row, &config) || !compatible_candidate(row) || !f_name(alias) || strlen(alias) >= 128 ||
        !json_object_is_type(f_field(resolved, "user"), json_type_array) ||
        !hd_text(f_text(json_object_array_get_idx(f_field(resolved, "user"), 0)), 128)) goto bad;
    f_string_add(host, "candidate_id", f_string(row, "candidate_id"));
    json_object_object_add(host, "sources", json_object_get(f_field(row, "sources")));
    json_object_object_add(host, "resolution", json_object_get(resolved));
    f_string_add(host, "principal", f_text(json_object_array_get_idx(f_field(resolved, "user"), 0)));
    f_string_add(host, "target", f_string(row, "target")); f_string_add(host, "alias", alias);
    f_string_add(host, "fingerprint", f_string(f_field(f_field(row, "qualification"), "data"), "peer_fingerprint"));
    f_string_add(host, "project", options->project); f_string_add(host, "required_capability", required);
    json_object_object_add(host, "fleet_protocol", json_object_new_int(F_PROTOCOL));
    if (config) {
        if (f_hash(config, hash)) goto bad;
        f_string_add(host, "ssh_config", config); f_string_add(host, "ssh_config_sha256", hash);
    }
    if (options->package) {
        f_string_add(host, "package", options->package); f_string_add(host, "package_sha256", options->sha256);
        f_string_add(host, "prefix", options->prefix);
    }
    return host;
bad:
    json_object_put(host); return NULL;
}
static bool review_options(const struct enrollment_options *options) {
    size_t i, j;
    if (!options->input || !options->output || !options->count || !enrollment_path(options->project) ||
        (options->alias && options->count != 1) || options->confirm) return false;
    if (options->package) {
        if (!enrollment_path(options->prefix) || !enrollment_file_matches(options->package, options->sha256)) return false;
    } else if (options->sha256 || options->prefix) return false;
    for (i = 0; i < options->count; i++)
        for (j = 0; j < i; j++) if (!strcmp(options->candidates[i], options->candidates[j])) return false;
    return true;
}
json_object *enrollment_review(const struct enrollment_options *options) {
    json_object *qualification = NULL, *intent = NULL, *hosts = NULL, *result; const char *required; char hash[65]; size_t i;
    if (!review_options(options)) return f_error("fleet-enrollment-review", "invalid_input", "select candidates, qualification, output and absolute project; a pinned package also requires its digest and exact prefix");
    qualification = f_read_json(options->input, F_LIMIT);
    required = f_string(f_field(qualification, "data"), "required_capability");
    if (!f_number_is(qualification, "schema_version", 1) || !hd_text(required, 128)) goto invalid;
    intent = json_object_new_object(); hosts = json_object_new_array();
    json_object_object_add(intent, "schema_version", json_object_new_int(2)); f_string_add(intent, "kind", "fleet-enrollment-intent");
    json_object_object_add(intent, "hosts", hosts);
    json_object_object_add(intent, "reviewed_at", json_object_new_int64((int64_t)time(NULL)));
    for (i = 0; i < options->count; i++) {
        json_object *host = review_host(selected_candidate(qualification, options->candidates[i]), options, required);
        if (!host) goto invalid;
        json_object_array_add(hosts, host);
    }
    if (enrollment_hash(intent, hash)) goto invalid;
    f_string_add(intent, "intent_sha256", hash);
    if (enrollment_write(options->output, intent)) goto invalid;
    result = f_success("fleet-enrollment-review", intent); json_object_put(qualification); return result;
invalid:
    json_object_put(qualification); json_object_put(intent);
    return f_error("fleet-enrollment-review", "invalid_review", "selected candidates must have fresh compatible qualification, peer identity and SSH policy; review output must be writable");
}
