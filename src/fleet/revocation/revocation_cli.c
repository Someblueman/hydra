#include "fleet/revocation/revocation.h"
#include "fleet/discovery/discovery.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <string.h>

/* What a revocation supersedes is copied into the record so the decision
 * stays inspectable after the live record changes or disappears. Reading it
 * never mutates anything. */
static json_object *alias_record(const char *name) {
    char directory[F_PATH], path[F_PATH], file[160];
    if (!f_name(name) || strlen(name) >= 128 || f_path(directory, sizeof(directory), f_home, "fleet/remotes")) return NULL;
    snprintf(file, sizeof(file), "%s.json", name);
    if (f_path(path, sizeof(path), directory, file)) return NULL;
    return f_read_json(path, 16384);
}
static json_object *superseded_transport(const char *name) {
    json_object *record = alias_record(name), *out = json_object_new_object();
    f_string_add(out, "alias", name);
    if (record) {
        if (f_string(record, "target")) f_string_add(out, "target", f_string(record, "target"));
        if (f_string(record, "accepted_host_key")) f_string_add(out, "accepted_host_key", f_string(record, "accepted_host_key"));
        if (f_string(record, "project")) f_string_add(out, "project", f_string(record, "project"));
        if (f_string(record, "principal")) f_string_add(out, "principal", f_string(record, "principal"));
        json_object_object_add(out, "alias_record", record);
    } else json_object_object_add(out, "alias_record", NULL);
    f_string_add(out, "retained", "the alias record stays on disk for inspection; it is blocked, not deleted");
    return out;
}
static json_object *superseded_candidate(const char *requested, const char *canonical) {
    json_object *out = json_object_new_object();
    f_string_add(out, "candidate_id", canonical);
    if (strcmp(requested, canonical)) f_string_add(out, "target", requested);
    return out;
}
static json_object *superseded_source(const char *id) {
    json_object *out = json_object_new_object(), *cache; const char *colon = strchr(id, ':'); char kind[64];
    size_t n = (size_t)(colon - id);
    memcpy(kind, id, n); kind[n] = '\0';
    f_string_add(out, "kind", kind); f_string_add(out, "locator", colon + 1);
    cache = hd_cache_summary(kind, colon + 1);
    json_object_object_add(out, "cache", cache);
    return out;
}
static json_object *superseded_project(const char *path) {
    json_object *out = json_object_new_object(), *names = f_remotes(), *aliases = json_object_new_array(); size_t i;
    f_string_add(out, "path", path);
    for (i = 0; i < json_object_array_length(names); i++) {
        const char *name = json_object_get_string(json_object_array_get_idx(names, i));
        json_object *record = alias_record(name); const char *project = f_string(record, "project");
        if (project && !strcmp(project, path)) json_object_array_add(aliases, json_object_new_string(name));
        json_object_put(record);
    }
    json_object_put(names);
    json_object_object_add(out, "aliases", aliases);
    return out;
}
static json_object *superseded(const char *scope, const char *id, const char *canonical) {
    if (!strcmp(scope, "transport")) return superseded_transport(canonical);
    if (!strcmp(scope, "candidate")) return superseded_candidate(id, canonical);
    if (!strcmp(scope, "source")) return superseded_source(canonical);
    if (!strcmp(scope, "project")) return superseded_project(canonical);
    json_object *out = json_object_new_object(); f_string_add(out, "sha256", canonical); return out;
}
static json_object *revoke(const char *scope, const char *id, const char *reason) {
    char canonical[REVOCATION_ID]; json_object *binding, *result;
    if (!revocation_normalize(scope, id, canonical) || !hd_text(reason, REVOCATION_REASON))
        return f_error("fleet-revoke", "invalid_input", "revoke candidate|source|transport|package|project ID --reason TEXT; the identity must be valid for its scope");
    if (revocation_blocked(scope, canonical)) {
        result = revocation_error("fleet-revoke", scope, canonical);
        f_string_add(f_field(result, "error"), "code", "already_revoked");
        f_string_add(f_field(result, "error"), "message", "this identity is already revoked; the original record is retained");
        return result;
    }
    binding = superseded(scope, id, canonical);
    result = revocation_record(scope, id, reason, binding);
    json_object_put(binding); return result;
}
static json_object *list(int argc, char **argv) {
    const char *scope = NULL; json_object *data, *records; int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        if (!strcmp(argv[i], "--scope") && i + 1 < argc && !scope && revocation_scope(argv[i + 1])) { scope = argv[++i]; continue; }
        return f_error("fleet-revoke-list", "invalid_input", "revoke list [--scope candidate|source|transport|package|project] [--json]");
    }
    records = revocation_list(scope); data = json_object_new_object();
    json_object_object_add(data, "revocation_schema_version", json_object_new_int(1));
    json_object_object_add(data, "count", json_object_new_int64((int64_t)json_object_array_length(records)));
    json_object_object_add(data, "revocations", records);
    return f_success("fleet-revoke-list", data);
}
json_object *revocation_cli(int argc, char **argv) {
    const char *reason = NULL; int i;
    if (argc >= 1 && !strcmp(argv[0], "list")) return list(argc, argv);
    if (argc < 2 || !revocation_scope(argv[0]))
        return f_error("fleet-revoke", "invalid_input", "revoke candidate|source|transport|package|project ID --reason TEXT [--json]; revoke list [--scope SCOPE]");
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        if (!strcmp(argv[i], "--reason") && i + 1 < argc && !reason) { reason = argv[++i]; continue; }
        return f_error("fleet-revoke", "invalid_input", "revoke SCOPE ID --reason TEXT takes one reason and no other options");
    }
    if (!reason) return f_error("fleet-revoke", "invalid_input", "--reason TEXT is required; the reason is retained with the decision");
    return revoke(argv[0], argv[1], reason);
}
