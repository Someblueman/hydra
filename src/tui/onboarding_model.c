#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "onboarding.h"
#include "setup_json.h"
/* Envelope readers for the onboarding view. Every value comes from the
 * CLI's own JSON (candidate schema 1, source reports with diff schema 1,
 * enrollment intent schema 2, progress schema 1 and the enrollment status
 * projection); unknown members are ignored and absent ones stay empty. */
#define ONB_TEXT_LIMIT (8U * 1024U * 1024U)
#define ONB_TOKEN_LIMIT 1500000

struct doc { struct sj_doc d; struct sj_token *tokens; };

static bool doc_open(struct doc *doc, const char *text, size_t length) {
    size_t capacity = length / 2 + 64;
    if (!text || length > ONB_TEXT_LIMIT) return false;
    if (capacity > ONB_TOKEN_LIMIT) capacity = ONB_TOKEN_LIMIT;
    doc->tokens = malloc(capacity * sizeof(*doc->tokens));
    if (!doc->tokens) return false;
    if (sj_parse(&doc->d, text, length, doc->tokens, (int)capacity)) { free(doc->tokens); doc->tokens = NULL; return false; }
    return true;
}
static void doc_close(struct doc *doc) { free(doc->tokens); doc->tokens = NULL; }

static void text_at(const struct sj_doc *d, int token, const char *path, char *out, size_t size) {
    if (!sj_path_text(d, token, path, out, size)) out[0] = '\0';
}
static long long number_at(const struct sj_doc *d, int token, const char *path) {
    char text[48];
    if (!sj_path_text(d, token, path, text, sizeof(text)) || !text[0]) return 0;
    return strtoll(text, NULL, 10);
}
static bool true_at(const struct sj_doc *d, int token, const char *path) {
    int value = sj_path(d, token, path);
    return value >= 0 && sj_true(d, value);
}
/* The first string of a value that is either a string or an array of strings. */
static void first_text(const struct sj_doc *d, int token, const char *path, char *out, size_t size) {
    int value = sj_path(d, token, path);
    out[0] = '\0';
    if (value < 0) return;
    if (sj_type_of(d, value) == SJ_ARRAY) value = sj_item(d, value, 0);
    if (value < 0 || !sj_text(d, value, out, size)) out[0] = '\0';
}
/* Items of an array of strings joined by spaces, bounded. */
static void join_strings(const struct sj_doc *d, int array, char *out, size_t size) {
    int i, item;
    char text[128];
    out[0] = '\0';
    if (array < 0 || sj_type_of(d, array) != SJ_ARRAY) return;
    for (i = 0; (item = sj_item(d, array, i)) >= 0; i++) {
        if (!sj_text(d, item, text, sizeof(text))) continue;
        if (out[0]) text_append(out, size, " ");
        text_append(out, size, "%s", text);
    }
}
static void names_at(const struct sj_doc *d, int array, char (*out)[128], size_t capacity, size_t *count) {
    int i, item;
    *count = 0;
    if (array < 0 || sj_type_of(d, array) != SJ_ARRAY) return;
    for (i = 0; (item = sj_item(d, array, i)) >= 0 && *count < capacity; i++)
        if (sj_text(d, item, out[*count], 128)) (*count)++;
}
static void revocation_at(const struct sj_doc *d, int token, struct onb_revocation *r) {
    memset(r, 0, sizeof(*r));
    if (token < 0 || sj_type_of(d, token) != SJ_OBJECT) return;
    r->present = true;
    text_at(d, token, "scope", r->scope, sizeof(r->scope));
    text_at(d, token, "id", r->id, sizeof(r->id));
    text_at(d, token, "reason", r->reason, sizeof(r->reason));
    text_at(d, token, "revoked_by.user", r->user, sizeof(r->user));
    text_at(d, token, "revoked_by.host", r->host, sizeof(r->host));
    r->revoked_at = number_at(d, token, "revoked_at");
}

/* ---- Envelope header ---- */

static void header(const struct sj_doc *d, struct onb_envelope *e, int exit_status) {
    memset(e, 0, sizeof(*e));
    e->exit_status = exit_status;
    if (sj_type_of(d, 0) != SJ_OBJECT || number_at(d, 0, "schema_version") != 1) return;
    e->parsed = true;
    e->ok = true_at(d, 0, "ok");
    text_at(d, 0, "command", e->command, sizeof(e->command));
    text_at(d, 0, "error.code", e->code, sizeof(e->code));
    text_at(d, 0, "error.message", e->message, sizeof(e->message));
    text_at(d, 0, "error.recovery", e->recovery, sizeof(e->recovery));
}

void onb_envelope_only(struct onb_envelope *e, const char *text, size_t length, int exit_status) {
    struct doc doc;
    if (!doc_open(&doc, text, length)) { memset(e, 0, sizeof(*e)); e->exit_status = exit_status; return; }
    header(&doc.d, e, exit_status);
    doc_close(&doc);
}

/* ---- Candidates ---- */

static void source_ref(const struct sj_doc *d, int token, struct onb_source_ref *s) {
    memset(s, 0, sizeof(*s));
    text_at(d, token, "kind", s->kind, sizeof(s->kind));
    text_at(d, token, "locator", s->locator, sizeof(s->locator));
    text_at(d, token, "name", s->name, sizeof(s->name));
    text_at(d, token, "freshness", s->freshness, sizeof(s->freshness));
    text_at(d, token, "scope", s->scope, sizeof(s->scope));
    text_at(d, token, "client", s->client, sizeof(s->client));
    s->observed_at = number_at(d, token, "observed_at");
    s->age_seconds = number_at(d, token, "age_seconds");
    join_strings(d, sj_find(d, token, "labels"), s->labels, sizeof(s->labels));
}

static void host_key(const struct sj_doc *d, int error, struct onb_candidate *c) {
    int detail = sj_find(d, error, "host_key"), recorded, i, item;
    if (detail < 0) return;
    c->key_changed = true;
    text_at(d, detail, "presented_fingerprint", c->presented, sizeof(c->presented));
    text_at(d, detail, "key_type", c->key_type, sizeof(c->key_type));
    text_at(d, detail, "known_hosts", c->known_hosts, sizeof(c->known_hosts));
    c->known_hosts_line = number_at(d, detail, "known_hosts_line");
    recorded = sj_find(d, detail, "recorded");
    for (i = 0; recorded >= 0 && (item = sj_item(d, recorded, i)) >= 0 && c->recorded_count < ONB_RECORDED; i++) {
        struct onb_recorded *r = &c->recorded[c->recorded_count++];
        text_at(d, item, "fingerprint", r->fingerprint, sizeof(r->fingerprint));
        text_at(d, item, "source", r->source, sizeof(r->source));
    }
}

static void qualification(const struct sj_doc *d, int token, struct onb_candidate *c) {
    int q = sj_find(d, token, "qualification"), data, error;
    char os[24], arch[24];
    if (q < 0 || sj_type_of(d, q) != SJ_OBJECT) return;
    c->qualified = true;
    c->qualification_ok = true_at(d, q, "ok");
    data = sj_find(d, q, "data"); error = sj_find(d, q, "error");
    text_at(d, q, "error.code", c->code, sizeof(c->code));
    text_at(d, q, "error.message", c->message, sizeof(c->message));
    text_at(d, q, "error.recovery", c->recovery, sizeof(c->recovery));
    if (error >= 0) host_key(d, error, c);
    if (data < 0) return;
    text_at(d, data, "peer_fingerprint", c->fingerprint, sizeof(c->fingerprint));
    text_at(d, data, "hydra_version", c->hydra_version, sizeof(c->hydra_version));
    c->fleet_protocol = (int)number_at(d, data, "fleet_protocol");
    text_at(d, data, "platform.os", os, sizeof(os));
    text_at(d, data, "platform.arch", arch, sizeof(arch));
    if (os[0] && arch[0]) snprintf(c->platform, sizeof(c->platform), "%s-%s", os, arch);
}

static void candidate(const struct sj_doc *d, int token, struct onb_candidate *c) {
    int sources = sj_find(d, token, "sources"), resolution = sj_find(d, token, "resolution"), i, item;
    memset(c, 0, sizeof(*c));
    text_at(d, token, "candidate_id", c->candidate_id, sizeof(c->candidate_id));
    text_at(d, token, "target", c->target, sizeof(c->target));
    text_at(d, token, "status", c->status, sizeof(c->status));
    text_at(d, token, "ssh_config", c->ssh_config, sizeof(c->ssh_config));
    text_at(d, token, "qualification_freshness", c->freshness, sizeof(c->freshness));
    c->qualified_at = number_at(d, token, "qualified_at");
    c->attempts = number_at(d, token, "qualification_attempts");
    for (i = 0; sources >= 0 && (item = sj_item(d, sources, i)) >= 0 && c->source_count < ONB_SOURCES; i++)
        source_ref(d, item, &c->sources[c->source_count++]);
    if (resolution >= 0) {
        c->resolution_ok = true_at(d, resolution, "ok");
        text_at(d, resolution, "data.policy_sha256", c->policy_sha256, sizeof(c->policy_sha256));
        first_text(d, resolution, "data.user", c->user, sizeof(c->user));
        first_text(d, resolution, "data.hostname", c->hostname, sizeof(c->hostname));
        first_text(d, resolution, "data.port", c->port, sizeof(c->port));
    }
    qualification(d, token, c);
    revocation_at(d, sj_find(d, token, "revocation"), &c->revocation);
}

/* ---- Source reports ---- */

static void record_add(struct onb_model *m, const char *source, const struct sj_doc *d, int item, const char *reason) {
    struct onb_record *r;
    if (m->record_count >= ONB_RECORDS) return;
    r = &m->records[m->record_count++];
    memset(r, 0, sizeof(*r));
    copy_text(r->source, sizeof(r->source), source);
    text_at(d, item, "name", r->name, sizeof(r->name));
    text_at(d, item, "target", r->target, sizeof(r->target));
    if (reason) copy_text(r->reason, sizeof(r->reason), reason);
    else text_at(d, item, "reason", r->reason, sizeof(r->reason));
    revocation_at(d, sj_find(d, item, "revocation"), &r->revocation);
}

static void source_diff(const struct sj_doc *d, int diff, struct onb_source_report *s) {
    int changed = sj_find(d, diff, "changed"), i, item;
    if (diff < 0 || sj_type_of(d, diff) != SJ_OBJECT) return;
    s->first_observation = true_at(d, diff, "first_observation");
    s->added_count = number_at(d, diff, "added_count");
    s->removed_count = number_at(d, diff, "removed_count");
    s->changed_count = number_at(d, diff, "changed_count");
    s->conflicting_count = number_at(d, diff, "conflicting_count");
    names_at(d, sj_find(d, diff, "added"), s->added, ONB_DIFF_NAMES, &s->added_n);
    names_at(d, sj_find(d, diff, "removed"), s->removed, ONB_DIFF_NAMES, &s->removed_n);
    for (i = 0; changed >= 0 && (item = sj_item(d, changed, i)) >= 0 && s->changed_n < ONB_DIFF_NAMES; i++) {
        struct onb_change *c = &s->changed[s->changed_n++];
        text_at(d, item, "name", c->name, sizeof(c->name));
        text_at(d, item, "previous_target", c->previous_target, sizeof(c->previous_target));
        text_at(d, item, "target", c->target, sizeof(c->target));
    }
}

static void source_report(struct onb_model *m, const struct sj_doc *d, int token) {
    struct onb_source_report *s = &m->sources[m->source_count++];
    int cache = sj_find(d, token, "cache"), skipped = sj_find(d, token, "skipped"), list, i, item;
    memset(s, 0, sizeof(*s));
    text_at(d, token, "kind", s->kind, sizeof(s->kind));
    text_at(d, token, "locator", s->locator, sizeof(s->locator));
    text_at(d, token, "scope", s->scope, sizeof(s->scope));
    text_at(d, token, "client", s->client, sizeof(s->client));
    text_at(d, token, "status", s->status, sizeof(s->status));
    text_at(d, token, "reason", s->reason, sizeof(s->reason));
    s->observed_at = number_at(d, token, "observed_at");
    s->bytes = number_at(d, token, "bytes");
    s->record_count = number_at(d, token, "record_count");
    s->credentials_omitted = number_at(d, token, "credentials_omitted");
    if (skipped >= 0 && sj_type_of(d, skipped) == SJ_ARRAY) s->skipped = d->tokens[skipped].size;
    if (cache >= 0 && sj_type_of(d, cache) == SJ_OBJECT) {
        s->cached = true;
        s->cache_age = number_at(d, cache, "age_seconds");
        s->cache_count = number_at(d, cache, "record_count");
        text_at(d, cache, "status", s->cache_status, sizeof(s->cache_status));
    }
    source_diff(d, sj_find(d, token, "diff"), s);
    list = sj_path(d, token, "diff.conflicting");
    for (i = 0; list >= 0 && (item = sj_item(d, list, i)) >= 0; i++) record_add(m, s->kind, d, item, NULL);
    list = sj_find(d, token, "revoked");
    for (i = 0; list >= 0 && (item = sj_item(d, list, i)) >= 0; i++) { record_add(m, s->kind, d, item, "revoked"); s->revoked_count++; }
}

static void discovery(struct onb_model *m, const struct sj_doc *d, int data) {
    int rows = sj_find(d, data, "candidates"), sources = sj_find(d, data, "sources"), i, item;
    m->candidate_count = m->source_count = m->record_count = 0;
    m->observed_at = number_at(d, data, "observed_at");
    m->pending = number_at(d, data, "pending_count");
    m->remaining = number_at(d, data, "remaining_count");
    m->processed = number_at(d, data, "processed_this_batch");
    m->revoked_count = number_at(d, data, "revoked_count");
    m->complete = true_at(d, data, "complete");
    m->partial = true_at(d, data, "partial_failure");
    m->diff_requested = true_at(d, data, "diff_requested");
    text_at(d, data, "required_capability", m->capability, sizeof(m->capability));
    for (i = 0; rows >= 0 && (item = sj_item(d, rows, i)) >= 0 && m->candidate_count < ONB_CANDIDATES; i++)
        candidate(d, item, &m->candidates[m->candidate_count++]);
    for (i = 0; sources >= 0 && (item = sj_item(d, sources, i)) >= 0 && m->source_count < ONB_SOURCES; i++)
        source_report(m, d, item);
}

/* ---- Intent, status and apply progress ---- */

static void host_row(const struct sj_doc *d, int token, struct onb_host *h) {
    text_at(d, token, "alias", h->alias, sizeof(h->alias));
    text_at(d, token, "candidate_id", h->candidate_id, sizeof(h->candidate_id));
    text_at(d, token, "target", h->target, sizeof(h->target));
    text_at(d, token, "principal", h->principal, sizeof(h->principal));
    text_at(d, token, "project", h->project, sizeof(h->project));
    text_at(d, token, "fingerprint", h->fingerprint, sizeof(h->fingerprint));
    text_at(d, token, "package_sha256", h->package_sha256, sizeof(h->package_sha256));
    text_at(d, token, "prefix", h->prefix, sizeof(h->prefix));
    text_at(d, token, "ssh_config", h->ssh_config, sizeof(h->ssh_config));
}
static void progress_fields(const struct sj_doc *d, int token, struct onb_host *h) {
    int key = sj_find(d, token, "host_key");
    char status[32];
    text_at(d, token, "operation_id", h->operation_id, sizeof(h->operation_id));
    text_at(d, token, "phase", h->phase, sizeof(h->phase));
    text_at(d, token, "status", status, sizeof(status));
    if (status[0]) copy_text(h->status, sizeof(h->status), status);
    if (key >= 0) {
        h->key_changed = true;
        text_at(d, key, "presented_fingerprint", h->presented, sizeof(h->presented));
        text_at(d, key, "reviewed_fingerprint", h->reviewed, sizeof(h->reviewed));
    }
    if (sj_find(d, token, "revocation") >= 0) revocation_at(d, sj_find(d, token, "revocation"), &h->revocation);
}
static struct onb_host *host_by_alias(struct onb_intent *in, const char *alias) {
    size_t i;
    for (i = 0; i < in->host_count; i++) if (!strcmp(in->hosts[i].alias, alias)) return &in->hosts[i];
    return NULL;
}
/* A review result or status projection replaces the intent; apply progress
 * (the same digest) merges its rows by alias, keeping the review bindings. */
static void intent(struct onb_model *m, const struct sj_doc *d, int data, bool progress_only) {
    struct onb_intent *in = &m->intent;
    int rows = sj_find(d, data, "hosts"), i, item;
    char digest[72], path[ONB_PATH];
    text_at(d, data, "intent_sha256", digest, sizeof(digest));
    if (progress_only && (!in->loaded || strcmp(digest, in->digest))) return;
    if (!progress_only) {
        char keep[ONB_PATH];
        copy_text(keep, sizeof(keep), in->path);
        memset(in, 0, sizeof(*in));
        copy_text(in->path, sizeof(in->path), keep);
        copy_text(in->digest, sizeof(in->digest), digest);
        in->reviewed_at = number_at(d, data, "reviewed_at");
        text_at(d, data, "intent", path, sizeof(path));
        if (path[0]) copy_text(in->path, sizeof(in->path), path);
        text_at(d, data, "progress", in->progress_path, sizeof(in->progress_path));
        in->progress_recorded = true_at(d, data, "progress_recorded");
        in->loaded = true;
    }
    if (sj_find(d, data, "partial_failure") >= 0) { in->partial = true_at(d, data, "partial_failure"); if (progress_only) in->progress_recorded = true; }
    for (i = 0; rows >= 0 && (item = sj_item(d, rows, i)) >= 0; i++) {
        struct onb_host *h;
        char alias[128];
        text_at(d, item, "alias", alias, sizeof(alias));
        h = host_by_alias(in, alias);
        if (!h) {
            if (in->host_count >= ONB_HOSTS) continue;
            h = &in->hosts[in->host_count++];
            memset(h, 0, sizeof(*h));
            copy_text(h->status, sizeof(h->status), "not_started");
            copy_text(h->phase, sizeof(h->phase), "preflight");
        }
        if (!progress_only) host_row(d, item, h);
        if (sj_find(d, item, "status") >= 0) { progress_fields(d, item, h); h->attempted = progress_only || true_at(d, item, "attempted"); }
    }
}

int onb_parse(struct onb_model *m, const char *text, size_t length, int exit_status) {
    struct doc doc;
    int data;
    if (!doc_open(&doc, text, length)) { memset(&m->last, 0, sizeof(m->last)); m->last.exit_status = exit_status; return -1; }
    header(&doc.d, &m->last, exit_status);
    data = sj_find(&doc.d, 0, "data");
    if (m->last.parsed && data >= 0 && sj_type_of(&doc.d, data) == SJ_OBJECT) {
        const char *command = m->last.command;
        if (!strcmp(command, "fleet-discovery") && sj_find(&doc.d, data, "candidates") >= 0) discovery(m, &doc.d, data);
        else if (!strcmp(command, "fleet-enrollment-review") || !strcmp(command, "fleet-enrollment-status")) { if (m->last.ok) intent(m, &doc.d, data, false); }
        else if (!strcmp(command, "fleet-enrollment")) intent(m, &doc.d, data, true);
    }
    doc_close(&doc);
    return m->last.parsed ? 0 : -1;
}

int onb_parse_file(struct onb_model *m, const char *path) {
    FILE *f = fopen(path, "rb");
    char *text;
    size_t length;
    int status;
    if (!f) return -1;
    text = malloc(ONB_TEXT_LIMIT + 1);
    if (!text) { fclose(f); return -1; }
    length = fread(text, 1, ONB_TEXT_LIMIT + 1, f);
    fclose(f);
    status = length > ONB_TEXT_LIMIT ? -1 : onb_parse(m, text, length, 0);
    free(text);
    return status;
}
