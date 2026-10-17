#include "fleet/revocation/revocation.h"
#include "fleet/discovery/discovery.h"
#include "fleet/enrollment/enrollment.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <dirent.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static const char *const scopes[] = {"candidate", "source", "transport", "package", "project", NULL};
bool revocation_scope(const char *scope) {
    size_t i;
    for (i = 0; scope && scopes[i]; i++) if (!strcmp(scope, scopes[i])) return true;
    return false;
}
static bool candidate_id(const char *id) {
    size_t n = strlen(id);
    if (strncmp(id, "cand_", 5) || n <= 5 || (n - 5) % 2 || n > 517) return false;
    return strspn(id + 5, "0123456789abcdef") == n - 5;
}
static bool source_id(const char *id) {
    const char *colon = strchr(id, ':');
    if (!colon || colon == id || !colon[1] || (size_t)(colon - id) >= 64) return false;
    return strspn(id, "abcdefghijklmnopqrstuvwxyz-") == (size_t)(colon - id);
}
bool revocation_normalize(const char *scope, const char *id, char out[REVOCATION_ID]) {
    if (!revocation_scope(scope) || !hd_text(id, REVOCATION_ID)) return false;
    if (!strcmp(scope, "candidate")) {
        if (candidate_id(id)) return !f_copy(out, REVOCATION_ID, id);
        if (!hd_text(id, 256) || !f_target(id)) return false;
        hd_id(out, id); return true;
    }
    if (!strcmp(scope, "source") && !source_id(id)) return false;
    if (!strcmp(scope, "transport") && (!f_name(id) || strlen(id) >= 128)) return false;
    if (!strcmp(scope, "package") && !enrollment_digest(id)) return false;
    if (!strcmp(scope, "project") && !enrollment_path(id)) return false;
    return !f_copy(out, REVOCATION_ID, id);
}
static int record_path(const char *scope, const char *id, char path[F_PATH], char directory[F_PATH]) {
    json_object *label = json_object_new_string(id); char digest[65], name[96]; int status = enrollment_hash(label, digest);
    json_object_put(label);
    if (status || f_path(directory, F_PATH, f_home, "fleet/revocations") || snprintf(name, sizeof(name), "%s/%s.json", scope, digest) >= (int)sizeof(name)) return -1;
    return f_path(path, F_PATH, directory, name);
}
static bool record_valid(json_object *record, const char *scope, const char *id) {
    const char *kind = f_string(record, "kind");
    return f_number_is(record, "schema_version", 1) && kind && !strcmp(kind, "fleet-revocation") &&
        f_string(record, "scope") && !strcmp(f_string(record, "scope"), scope) &&
        f_string(record, "id") && !strcmp(f_string(record, "id"), id) &&
        hd_text(f_string(record, "reason"), REVOCATION_REASON) && json_object_is_type(f_field(record, "revoked_at"), json_type_int);
}
json_object *revocation_find(const char *scope, const char *id) {
    char canonical[REVOCATION_ID], path[F_PATH], directory[F_PATH]; struct stat st; json_object *record;
    if (!revocation_normalize(scope, id, canonical) || record_path(scope, canonical, path, directory)) return NULL;
    if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) return NULL;
    record = f_read_json(path, 65536);
    if (record_valid(record, scope, canonical)) return record;
    json_object_put(record); return NULL;
}
bool revocation_blocked(const char *scope, const char *id) {
    json_object *record = revocation_find(scope, id);
    json_object_put(record); return record != NULL;
}
json_object *revocation_detail(json_object *record) {
    static const char *const keys[] = {"scope", "id", "reason", "revoked_at", "revoked_by", NULL};
    json_object *detail = json_object_new_object(); size_t i;
    for (i = 0; keys[i]; i++) if (f_field(record, keys[i])) json_object_object_add(detail, keys[i], json_object_get(f_field(record, keys[i])));
    return detail;
}
json_object *revocation_error(const char *command, const char *scope, const char *id) {
    json_object *record = revocation_find(scope, id), *error;
    error = f_error(command, "revoked", "this identity was explicitly revoked; revocation is final for it and blocks new operations");
    f_string_add(f_field(error, "error"), "recovery", "inspect hydra fleet revoke list; re-qualify and review enrollment under a new identity or alias");
    if (record) { json_object_object_add(error, "data", revocation_detail(record)); json_object_put(record); }
    return error;
}
static json_object *actor(void) {
    json_object *by = json_object_new_object(); struct passwd *pw = getpwuid(geteuid()); struct utsname host;
    if (pw && pw->pw_name && hd_text(pw->pw_name, 256)) f_string_add(by, "user", pw->pw_name);
    json_object_object_add(by, "uid", json_object_new_int64((int64_t)geteuid()));
    if (!uname(&host) && hd_text(host.nodename, 256)) f_string_add(by, "host", host.nodename);
    return by;
}
json_object *revocation_record(const char *scope, const char *id, const char *reason, json_object *superseded) {
    char canonical[REVOCATION_ID], path[F_PATH], directory[F_PATH], scoped[F_PATH]; json_object *record, *copy = NULL, *result;
    const char *text;
    if (!revocation_normalize(scope, id, canonical) || !hd_text(reason, REVOCATION_REASON))
        return f_error("fleet-revoke", "invalid_input", "revoke candidate|source|transport|package|project ID --reason TEXT; the identity must be valid for its scope");
    if (record_path(scope, canonical, path, directory) || f_path(scoped, sizeof(scoped), directory, scope) || f_mkdirs(scoped))
        return f_error("fleet-revoke", "state_unavailable", "cannot establish the private revocation store");
    if (superseded && json_object_deep_copy(superseded, &copy, NULL)) return f_error("fleet-revoke", "state_unavailable", "cannot retain the superseded binding");
    record = json_object_new_object();
    json_object_object_add(record, "schema_version", json_object_new_int(1));
    f_string_add(record, "kind", "fleet-revocation"); f_string_add(record, "scope", scope); f_string_add(record, "id", canonical);
    if (strcmp(canonical, id)) f_string_add(record, "requested_id", id);
    f_string_add(record, "reason", reason);
    json_object_object_add(record, "revoked_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(record, "revoked_by", actor());
    json_object_object_add(record, "superseded", copy);
    f_string_add(record, "effect", "blocks new operations and pending intents; cancels no task, uninstalls nothing, deletes no remote state and edits no known_hosts");
    text = json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN);
    if (f_write(path, text, strlen(text), false)) {
        json_object_put(record);
        if (revocation_blocked(scope, canonical)) return revocation_error("fleet-revoke", scope, canonical);
        return f_error("fleet-revoke", "state_unavailable", "cannot write the private revocation record");
    }
    result = f_success("fleet-revoke", record); return result;
}
json_object *revocation_find_key(const char *fingerprint) {
    json_object *list, *found = NULL; size_t i;
    if (!fingerprint || strncmp(fingerprint, "SHA256:", 7)) return NULL;
    list = revocation_list("transport");
    for (i = 0; i < json_object_array_length(list) && !found; i++) {
        json_object *record = json_object_array_get_idx(list, i);
        const char *key = f_string(f_field(record, "superseded"), "accepted_host_key");
        if (key && !strcmp(key, fingerprint)) found = json_object_get(record);
    }
    json_object_put(list); return found;
}
static int record_order(const void *left, const void *right) {
    json_object *a = *(json_object *const *)left, *b = *(json_object *const *)right;
    int64_t at = json_object_get_int64(f_field(a, "revoked_at")), bt = json_object_get_int64(f_field(b, "revoked_at"));
    int order = at < bt ? -1 : at > bt;
    if (!order) order = strcmp(f_string(a, "scope"), f_string(b, "scope"));
    return order ? order : strcmp(f_string(a, "id"), f_string(b, "id"));
}
static void list_scope(json_object *out, const char *scope) {
    char directory[F_PATH], scoped[F_PATH], path[F_PATH]; DIR *dir; struct dirent *entry;
    if (f_path(directory, sizeof(directory), f_home, "fleet/revocations") || f_path(scoped, sizeof(scoped), directory, scope) || !(dir = opendir(scoped))) return;
    while ((entry = readdir(dir))) {
        size_t n = strlen(entry->d_name); json_object *record; struct stat st;
        if (n != 69 || strcmp(entry->d_name + 64, ".json") || f_path(path, sizeof(path), scoped, entry->d_name)) continue;
        if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) continue;
        record = f_read_json(path, 65536);
        if (record && record_valid(record, scope, f_string(record, "id") ? f_string(record, "id") : "")) json_object_array_add(out, record);
        else json_object_put(record);
    }
    closedir(dir);
}
json_object *revocation_list(const char *scope) {
    json_object *out = json_object_new_array(); size_t i;
    if (scope) list_scope(out, scope);
    else for (i = 0; scopes[i]; i++) list_scope(out, scopes[i]);
    json_object_array_sort(out, record_order); return out;
}
