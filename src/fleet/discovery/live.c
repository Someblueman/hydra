#include "fleet/discovery/discovery.h"
#include "fleet/revocation/revocation.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

/* Opt-in live sources feed the existing candidate importer. Each selector is
 * acquired once per invocation through its normal client or the operator's
 * own files, diffed against the previous private snapshot, and only records
 * from a complete current acquisition (or explicitly selected names from a
 * partial one) become candidates. Stale caches, missing clients, partial
 * output, vanished sources, identity conflicts and revoked identities are
 * reported with provenance and never widen the selection. */
struct selector { char kind[32], locator[F_PATH]; };
static bool parse_selector(const char *spec, struct selector *s) {
    const char *colon = strchr(spec, ':'); const char *home = getenv("HOME");
    size_t n = colon ? (size_t)(colon - spec) : strlen(spec);
    if (!hd_text(spec, F_PATH) || n >= sizeof(s->kind)) return false;
    memcpy(s->kind, spec, n); s->kind[n] = '\0';
    if (!strcmp(s->kind, "ssh-config")) {
        if (colon) return colon[1] == '/' && !strpbrk(colon + 1, "\"\\*?[%$~") && !f_copy(s->locator, F_PATH, colon + 1);
        return home && home[0] == '/' && !f_path(s->locator, F_PATH, home, ".ssh/config");
    }
    if (!strcmp(s->kind, "ansible-inventory")) return colon && colon[1] == '/' && !strpbrk(colon + 1, "\"\\*?[%$~") && !f_copy(s->locator, F_PATH, colon + 1);
    if (!strcmp(s->kind, "dns-sd")) {
        if (!colon) return !f_copy(s->locator, F_PATH, "local");
        return hd_text(colon + 1, 64) && strspn(colon + 1, "abcdefghijklmnopqrstuvwxyz0123456789.-") == strlen(colon + 1) && !f_copy(s->locator, F_PATH, colon + 1);
    }
    return false;
}
bool hd_source_valid(const char *selector) { struct selector s; return parse_selector(selector, &s); }
static void scope_of(const struct selector *s, char scope[512]) {
    struct passwd *pw = getpwuid(geteuid()); struct utsname host; const char *user = pw && pw->pw_name && hd_text(pw->pw_name, 128) ? pw->pw_name : "uid";
    const char *node = !uname(&host) && hd_text(host.nodename, 256) ? host.nodename : "localhost";
    if (!strcmp(s->kind, "dns-sd")) snprintf(scope, 512, "network:%s via %s", s->locator, node);
    else snprintf(scope, 512, "account:%s@%s", user, node);
}
static void mark_revoked(json_object *report) {
    json_object *records = f_field(report, "records"), *revoked = json_object_new_array(); size_t i;
    for (i = 0; i < json_object_array_length(records); i++) {
        json_object *record = json_object_array_get_idx(records, i), *found = revocation_find("candidate", f_string(record, "target")), *entry;
        if (!found) continue;
        json_object_object_add(record, "revoked", revocation_detail(found));
        entry = json_object_new_object();
        f_string_add(entry, "name", f_string(record, "name")); f_string_add(entry, "target", f_string(record, "target"));
        json_object_object_add(entry, "revocation", revocation_detail(found));
        json_object_array_add(revoked, entry); json_object_put(found);
    }
    json_object_object_add(report, "revoked", revoked);
}
static json_object *acquire(const struct selector *s, const struct hd_options *options) {
    char id[F_PATH + 64], scope[512]; json_object *report, *previous, *found; const char *client;
    if (snprintf(id, sizeof(id), "%s:%s", s->kind, s->locator) >= (int)sizeof(id)) return hd_report("unavailable", "invalid_selector");
    found = revocation_find("source", id);
    if (found) {
        report = hd_report("revoked", "source_revoked"); json_object_object_add(report, "revocation", revocation_detail(found)); json_object_put(found);
        client = "none";
    } else if (!strcmp(s->kind, "ssh-config")) { report = hd_scan_ssh_config(s->locator); client = "file scan, then ssh -G per candidate"; }
    else if (!strcmp(s->kind, "ansible-inventory")) { report = hd_scan_ansible(s->locator); client = "file"; }
    else { report = hd_scan_dnssd(s->locator, options->seconds); client = "avahi-browse"; }
    f_string_add(report, "kind", s->kind); f_string_add(report, "locator", s->locator);
    scope_of(s, scope); f_string_add(report, "scope", scope); f_string_add(report, "client", client);
    f_string_add(report, "source_id", id);
    previous = hd_cache_read(s->kind, s->locator);
    hd_live_diff(report, previous);
    mark_revoked(report);
    if (previous && (!strcmp(f_string(report, "status"), "unavailable") || !strcmp(f_string(report, "status"), "revoked")))
        f_string_add(f_field(report, "cache"), "status", "stale");
    if (strcmp(f_string(report, "status"), "unavailable") && strcmp(f_string(report, "status"), "revoked") && hd_cache_write(report))
        json_object_object_add(report, "cache_write_failed", json_object_new_boolean(true));
    json_object_put(previous);
    return report;
}
json_object *hd_live_acquire(const struct hd_options *options) {
    json_object *live = json_object_new_array(); size_t i, j; struct selector s[HD_SOURCES];
    for (i = 0; i < options->source_count; i++) {
        if (!parse_selector(options->sources[i], &s[i])) goto bad;
        for (j = 0; j < i; j++) if (!strcmp(s[i].kind, s[j].kind) && !strcmp(s[i].locator, s[j].locator)) goto bad;
    }
    for (i = 0; i < options->source_count; i++) json_object_array_add(live, acquire(&s[i], options));
    return live;
bad:
    json_object_put(live); return NULL;
}
static bool selected(const struct hd_options *options, const char *name, bool matched[HD_HOSTS]) {
    size_t i; bool found = false;
    for (i = 0; i < options->select_count; i++) if (!strcmp(options->select[i], name)) { matched[i] = true; found = true; }
    return found;
}
int hd_live_import(json_object *rows, const struct hd_options *options, bool matched[HD_HOSTS]) {
    size_t i, j;
    for (i = 0; options->live && i < json_object_array_length(options->live); i++) {
        json_object *report = json_object_array_get_idx(options->live, i), *records = f_field(report, "records");
        const char *status = f_string(report, "status");
        bool complete = !strcmp(status, "acquired"), partial = !strcmp(status, "partial");
        if (!complete && !partial) continue;
        for (j = 0; j < json_object_array_length(records); j++) {
            json_object *record = json_object_array_get_idx(records, j), *row, *source;
            bool chosen = selected(options, f_string(record, "name"), matched);
            if (f_string(record, "conflict") || f_field(record, "revoked")) { if (chosen) return -1; continue; }
            if (options->select_count ? !chosen : partial) continue;
            row = hd_candidate_add(rows, f_string(record, "target"));
            if (!row) return -1;
            hd_source_add(row, f_string(report, "kind"), f_string(report, "locator"), f_string(record, "name"),
                          json_object_get_int64(f_field(report, "observed_at")), f_field(record, "labels"));
            source = json_object_array_get_idx(f_field(row, "sources"), json_object_array_length(f_field(row, "sources")) - 1);
            f_string_add(source, "scope", f_string(report, "scope"));
            f_string_add(source, "freshness", complete ? "acquired_now" : "partial_acquisition");
            f_string_add(source, "client", f_string(report, "client"));
        }
    }
    return 0;
}
static void counts(json_object *diff) {
    static const char *const keys[] = {"added", "removed", "changed", NULL}; size_t i;
    for (i = 0; keys[i]; i++) {
        char key[32]; snprintf(key, sizeof(key), "%s_count", keys[i]);
        json_object_object_add(diff, key, json_object_new_int64((int64_t)json_object_array_length(f_field(diff, keys[i]))));
    }
    json_object_object_add(diff, "conflicting_count", json_object_new_int64((int64_t)json_object_array_length(f_field(diff, "conflicting"))));
}
json_object *hd_live_public(json_object *live, bool diff) {
    json_object *out = NULL; size_t i;
    if (!live || json_object_deep_copy(live, &out, NULL)) return json_object_new_array();
    for (i = 0; i < json_object_array_length(out); i++) {
        json_object *report = json_object_array_get_idx(out, i), *records = f_field(report, "records"), *d = f_field(report, "diff");
        json_object_object_add(report, "record_count", json_object_new_int64((int64_t)json_object_array_length(records)));
        json_object_object_del(report, "records");
        json_object_object_add(report, "diff_schema_version", json_object_new_int(1));
        counts(d);
        if (!diff) { json_object_object_del(d, "added"); json_object_object_del(d, "removed"); json_object_object_del(d, "changed"); }
    }
    return out;
}
bool hd_live_failed(json_object *live) {
    size_t i;
    for (i = 0; live && i < json_object_array_length(live); i++) {
        const char *status = f_string(json_object_array_get_idx(live, i), "status");
        if (!strcmp(status, "unavailable") || !strcmp(status, "revoked")) return true;
    }
    return false;
}
