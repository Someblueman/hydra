#include "fleet/discovery/discovery.h"
#include "fleet/revocation/revocation.h"
#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A changed host key is explained, never accepted: the presented fingerprint
 * comes from OpenSSH's own strict refusal, the recorded ones from Hydra's
 * alias, revocation and prior qualification records. Each value names where
 * it was observed. Removing the old entry (ssh-keygen -R) is the operator's
 * own OpenSSH step, after which enrollment must be reviewed afresh. */
static void fingerprint_after(const char *text, const char *marker, char out[256]) {
    const char *start = text ? strstr(text, marker) : NULL, *end;
    out[0] = '\0';
    if (!start || !(start = strstr(start, "SHA256:"))) return;
    end = start + 7;
    while (*end && strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=", *end)) end++;
    if (end == start + 7 || end - start >= 256) return;
    memcpy(out, start, (size_t)(end - start)); out[end - start] = '\0';
}
static void word_between(const char *text, const char *before, const char *after, char out[64]) {
    const char *start = text ? strstr(text, before) : NULL, *end;
    out[0] = '\0';
    if (!start) return;
    start += strlen(before); end = strstr(start, after);
    if (!end || end == start || end - start >= 64 || (size_t)(end - start) != strspn(start, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-")) return;
    memcpy(out, start, (size_t)(end - start)); out[end - start] = '\0';
}
static void offending(const char *text, json_object *detail) {
    const char *start = text ? strstr(text, "Offending ") : NULL, *path, *colon; char file[F_PATH];
    if (!start || !(path = strstr(start, " in /"))) return;
    path += 4;
    colon = path + strcspn(path, ":\r\n");
    if (*colon != ':' || (size_t)(colon - path) >= sizeof(file)) return;
    memcpy(file, path, (size_t)(colon - path)); file[colon - path] = '\0';
    if (!hd_text(file, F_PATH)) return;
    f_string_add(detail, "known_hosts", file);
    if (strspn(colon + 1, "0123456789")) json_object_object_add(detail, "known_hosts_line", json_object_new_int64(strtoll(colon + 1, NULL, 10)));
}
static void recorded_add(json_object *recorded, const char *fingerprint, const char *source) {
    json_object *entry;
    if (!fingerprint || strncmp(fingerprint, "SHA256:", 7) || !hd_text(fingerprint, 256) || json_object_array_length(recorded) >= 16) return;
    entry = json_object_new_object();
    f_string_add(entry, "fingerprint", fingerprint); f_string_add(entry, "source", source);
    json_object_array_add(recorded, entry);
}
static void recorded_aliases(json_object *recorded, const char *target) {
    json_object *names = f_remotes(), *revoked = revocation_list("transport"); size_t i; char source[200];
    for (i = 0; i < json_object_array_length(names); i++) {
        struct f_remote remote; const char *name = json_object_get_string(json_object_array_get_idx(names, i));
        if (f_remote_load(name, &remote) || strcmp(remote.target, target) || !remote.accepted_host_key[0]) continue;
        snprintf(source, sizeof(source), "alias:%s accepted_host_key", name);
        recorded_add(recorded, remote.accepted_host_key, source);
    }
    for (i = 0; i < json_object_array_length(revoked); i++) {
        json_object *record = json_object_array_get_idx(revoked, i), *superseded = f_field(record, "superseded");
        if (!f_string(superseded, "target") || strcmp(f_string(superseded, "target"), target)) continue;
        snprintf(source, sizeof(source), "revoked transport:%s accepted_host_key", f_string(record, "id"));
        recorded_add(recorded, f_string(superseded, "accepted_host_key"), source);
    }
    json_object_put(names); json_object_put(revoked);
}
void hd_host_key_detail(json_object *error, json_object *candidate, const char *stderr_text) {
    json_object *detail = json_object_new_object(), *recorded = json_object_new_array(), *history = f_field(candidate, "qualification_history");
    char presented[256], type[64]; size_t i;
    fingerprint_after(stderr_text, "sent by the remote host", presented);
    word_between(stderr_text, "The fingerprint for the ", " key sent", type);
    if (presented[0]) f_string_add(detail, "presented_fingerprint", presented);
    if (type[0]) f_string_add(detail, "key_type", type);
    offending(stderr_text, detail);
    f_string_add(detail, "observation", "strict OpenSSH refusal (REMOTE HOST IDENTIFICATION HAS CHANGED) during this qualification; the key was not accepted");
    json_object_object_add(detail, "observed_at", json_object_new_int64((int64_t)time(NULL)));
    recorded_aliases(recorded, f_string(candidate, "target"));
    for (i = 0; history && i < json_object_array_length(history); i++) {
        json_object *entry = json_object_array_get_idx(history, i);
        recorded_add(recorded, f_string(f_field(f_field(entry, "result"), "data"), "peer_fingerprint"), "prior qualification peer_fingerprint");
    }
    json_object_object_add(detail, "recorded", recorded);
    f_string_add(detail, "acceptance", "Hydra never edits known_hosts. Verify the new key out of band; only if the change is expected remove the old entry yourself (ssh-keygen -R HOST [-f FILE]), re-qualify, then review enrollment again");
    json_object_object_add(error, "host_key", detail);
}
