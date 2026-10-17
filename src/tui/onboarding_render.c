#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "onboarding_flow.h"
/* Onboarding screens. Values come from the CLI envelopes; the fixed
 * sentences say what each screen is and which command a key would run. */

/* A scrolled body between a fixed title and footer. */
struct pen { struct app *app; size_t row, skip; int bottom; bool more; };

static void pen_begin(struct pen *p, struct app *app, int footer) {
    p->app = app; p->row = 0; p->skip = app->onboarding->scroll; p->more = false; p->bottom = app->limit - footer;
}
static void pen_line(struct pen *p, int indent, enum tv_style tone, const char *text) {
    struct app *app = p->app;
    if (p->row++ < p->skip) return;
    if (app->line >= p->bottom) { p->more = true; return; }
    column(app, indent, app->content_width - indent, tone, text);
    app->line++;
}
static size_t wrap_length(const char *text, size_t length, int width) {
    size_t i = 0, cut = 0;
    int columns = 0;
    while (i < length && columns < width) {
        if (text[i] == ' ') cut = i;
        i++;
        while (i < length && ((unsigned char)text[i] & 0xc0) == 0x80) i++;
        columns++;
    }
    if (i >= length) return length;
    return cut > 0 ? cut : i;
}
/* label in a fixed column, value wrapped with a hanging indent. */
static void pen_pair(struct pen *p, const char *label, const char *value, enum tv_style tone) {
    struct app *app = p->app;
    int label_width = app->content_width < 70 ? 14 : 20, width = app->content_width - 2 - label_width;
    const char *rest = value[0] ? value : "-";
    char line[1024];
    if (width < 8) width = 8;
    do {
        size_t take = wrap_length(rest, strlen(rest), width);
        if (p->row >= p->skip && app->line < p->bottom && rest == value) column(app, 2, label_width, TV_MUTED, label);
        snprintf(line, sizeof(line), "%.*s", (int)(take < sizeof(line) ? take : sizeof(line) - 1), rest);
        pen_line(p, 2 + label_width, tone, line);
        rest += take;
        while (*rest == ' ') rest++;
    } while (*rest);
}
static void pen_text(struct pen *p, const char *text, enum tv_style tone) {
    const char *rest = text;
    char line[1024];
    int width = p->app->content_width;
    do {
        size_t take = wrap_length(rest, strlen(rest), width);
        snprintf(line, sizeof(line), "%.*s", (int)(take < sizeof(line) ? take : sizeof(line) - 1), rest);
        pen_line(p, 0, tone, line);
        rest += take;
        while (*rest == ' ') rest++;
    } while (*rest);
}
static void pen_blank(struct pen *p) { pen_line(p, 0, TV_BASE, ""); }
static void title(struct app *app, enum tv_style tone, const char *text) {
    style(app, tone == TV_WARNING ? TONE_WARNING : tone == TV_SUCCESS ? TONE_SUCCESS : TONE_STRONG);
    linef(app, "%s", text);
    style(app, TONE_BASE);
}
static void footer(struct app *app, struct pen *p, const char *prompt, enum tv_style tone) {
    struct native_onboarding *s = app->onboarding;
    s->more = p->more;
    app->line = app->limit - 2;
    if (p->more || p->skip)
        column(app, 0, app->content_width, TV_MUTED, p->more ? (app->ascii ? "v more below: Down or PgDn scrolls" : "\xe2\x86\x93 more below: Down or PgDn scrolls") : "Up or PgUp scrolls back");
    app->line++;
    column(app, 0, app->content_width, s->notice[0] ? TV_WARNING : tone, s->notice[0] ? s->notice : prompt);
    app->line++;
}
static void revocation_pairs(struct pen *p, const struct onb_revocation *r) {
    char when[48], by[320];
    if (!r->present) return;
    onb_time_text(r->revoked_at, when, sizeof(when));
    snprintf(by, sizeof(by), "%.15s %.200s%s%.63s", r->scope, r->id, r->user[0] ? " by " : "", r->user);
    pen_pair(p, "Revoked", by, TV_WARNING);
    pen_pair(p, "Reason", r->reason, TV_WARNING);
    pen_pair(p, "Revoked at", when, TV_BASE);
    pen_text(p, "A revoked identity can be inspected here and nowhere else: no review, enrollment or apply is offered for it.", TV_MUTED);
}

/* ---- List ---- */

static enum tv_style typed_tone(enum onb_typed typed, bool selected) {
    if (selected) return TV_SELECTED;
    if (typed == ONB_ENROLLED) return TV_SUCCESS;
    if (typed == ONB_FAILED || typed == ONB_REVOKED || typed == ONB_CONFLICTING || typed == ONB_UNKNOWN) return TV_WARNING;
    return TV_BASE;
}
static void list_row(struct app *app, const struct onb_row *row, bool selected, long long now) {
    struct native_onboarding *s = app->onboarding;
    const struct onb_model *m = s->model;
    int width = app->content_width, name_w = width >= 110 ? 30 : width >= 80 ? 22 : 16, state_w = width >= 110 ? 30 : 22;
    char name[300], state[64], age[16], line[600];
    enum tv_style tone;
    if (row->kind == ONB_ROW_CANDIDATE) {
        const struct onb_candidate *c = &m->candidates[row->index];
        enum onb_typed typed = onb_candidate_typed(m, c);
        onb_candidate_state(m, c, state, sizeof(state));
        onb_candidate_age(c, now, age, sizeof(age));
        snprintf(name, sizeof(name), "%s", c->source_count && c->sources[0].name[0] && strcmp(c->sources[0].name, c->target) ? c->sources[0].name : c->target);
        tone = typed_tone(typed, selected);
        if (width >= 80) snprintf(line, sizeof(line), "%c %-*.*s %-*.*s %-10.10s %-14.14s %5s", selected ? '>' : ' ', name_w, name_w, name, state_w, state_w, state,
                                  onb_candidate_source(c), onb_candidate_platform(c), age);
        else snprintf(line, sizeof(line), "%c %-*.*s %-*.*s", selected ? '>' : ' ', name_w, name_w, name, state_w, state_w, state);
    } else {
        const struct onb_record *r = &m->records[row->index];
        tone = typed_tone(r->revocation.present ? ONB_REVOKED : ONB_CONFLICTING, selected);
        snprintf(state, sizeof(state), "%s: %s", r->revocation.present ? "revoked" : "conflicting", r->revocation.present ? "not imported" : r->reason);
        if (width >= 80) snprintf(line, sizeof(line), "%c %-*.*s %-*.*s %-10.10s %-14.14s %5s", selected ? '>' : ' ', name_w, name_w, r->name, state_w, state_w, state, r->source, "-", "-");
        else snprintf(line, sizeof(line), "%c %-*.*s %-*.*s", selected ? '>' : ' ', name_w, name_w, r->name, state_w, state_w, state);
    }
    if (selected) column(app, 0, width, TV_SELECTED, "");
    column(app, 0, width, tone, line);
    app->line++;
}
static void list_summary(struct app *app, char *out, size_t size) {
    const struct onb_model *m = app->onboarding->model;
    char filters[256];
    onb_filter_text(&app->onboarding->filter, filters, sizeof(filters));
    snprintf(out, size, "%zu candidate%s%s%s%s", m->candidate_count, m->candidate_count == 1 ? "" : "s",
             m->record_count ? ", " : "", m->record_count ? "reported records" : "", m->partial ? ", partial" : "");
    if (filters[0]) text_append(out, size, " / filters:%s", filters);
    if (m->export_path[0]) text_append(out, size, " / export %s", strrchr(m->export_path, '/') ? strrchr(m->export_path, '/') + 1 : m->export_path);
    else if (m->candidate_count) text_append(out, size, " / discovered, not recorded");
    if (!m->complete && m->remaining) text_append(out, size, " / %lld not yet compatible", m->remaining);
    if (m->intent.loaded) text_append(out, size, " / intent %.12s (%zu host%s)", m->intent.digest, m->intent.host_count, m->intent.host_count == 1 ? "" : "s");
}
static void render_list(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    struct onb_row rows[ONB_CANDIDATES + ONB_RECORDS];
    size_t count = onb_rows(s->model, &s->filter, rows, ONB_CANDIDATES + ONB_RECORDS), i, start;
    int available, width = app->content_width;
    long long now = (long long)time(NULL);
    char summary[512], header[400];
    if (s->selected >= count) s->selected = count ? count - 1 : 0;
    title(app, TV_STRONG, "ONBOARDING / candidates, qualification and enrollment");
    list_summary(app, summary, sizeof(summary));
    column(app, 0, width, TV_MUTED, summary); app->line++;
    if (!count) {
        linef(app, "");
        paragraph(app, s->model->candidate_count || s->model->record_count ? "No row matches the filters; x clears them."
                  : "No candidates yet. f discovers explicitly named SSH aliases, an inventory or a live source (no connection is made); "
                    "Q then qualifies them with one strict read-only handshake each and records the export; o opens an export made on the command line.", TV_BASE);
        app->line = app->limit - 2;
        column(app, 0, width, s->notice[0] ? TV_WARNING : TV_MUTED, s->notice[0] ? s->notice : "Nothing here changes a host: enrollment is applied only from a reviewed intent after a typed confirmation.");
        return;
    }
    if (width >= 80) snprintf(header, sizeof(header), "  %-*s %-*s %-10s %-14s %5s", width >= 110 ? 30 : 22, "NAME / TARGET", width >= 110 ? 30 : 22, "STATE", "SOURCE", "PLATFORM", "AGE");
    else snprintf(header, sizeof(header), "  %-16s %-22s", "NAME / TARGET", "STATE");
    column(app, 0, width, TV_MUTED, header); app->line++;
    available = app->limit - 2 - app->line;
    if (available < 1) available = 1;
    start = s->selected >= (size_t)available ? s->selected - (size_t)available + 1 : 0;
    for (i = start; i < count && app->line < app->limit - 2; i++) list_row(app, &rows[i], i == s->selected, now);
    app->line = app->limit - 2;
    snprintf(header, sizeof(header), "rows %zu-%zu of %zu", start + 1, i, count);
    column(app, 0, width, TV_MUTED, header); app->line++;
    column(app, 0, width, s->notice[0] ? TV_WARNING : TV_MUTED, s->notice[0] ? s->notice
           : "Enter inspects; s/S/P/l filter state, source, platform, label; / searches; x clears; r reviews, i intent");
    app->line++;
}

/* ---- Detail ---- */

static void source_pairs(struct pen *p, const struct onb_candidate *c, long long now) {
    size_t i;
    char text[600], age[16];
    for (i = 0; i < c->source_count; i++) {
        const struct onb_source_ref *r = &c->sources[i];
        onb_candidate_age(c, now, age, sizeof(age));
        snprintf(text, sizeof(text), "%s %s as %s (%s, observed %llds ago%s%s%s%s)", r->kind, r->locator, r->name, r->freshness, r->age_seconds,
                 r->scope[0] ? ", scope " : "", r->scope, r->client[0] ? ", client " : "", r->client);
        pen_pair(p, i ? "" : "Provenance", text, TV_BASE);
        if (r->labels[0]) pen_pair(p, "Labels", r->labels, TV_BASE);
    }
}
static void key_change_pairs(struct pen *p, const struct onb_candidate *c) {
    size_t i;
    char text[600];
    if (!c->key_changed) return;
    snprintf(text, sizeof(text), "%s%s%s", c->presented[0] ? c->presented : "(not reported by OpenSSH)", c->key_type[0] ? " " : "", c->key_type);
    pen_pair(p, "Presented key", text, TV_WARNING);
    for (i = 0; i < c->recorded_count; i++) {
        snprintf(text, sizeof(text), "%s (%s)", c->recorded[i].fingerprint, c->recorded[i].source);
        pen_pair(p, i ? "" : "Recorded key", text, TV_BASE);
    }
    if (!c->recorded_count) pen_pair(p, "Recorded key", "none on record for this target", TV_BASE);
    if (c->known_hosts[0]) { snprintf(text, sizeof(text), "%s line %lld", c->known_hosts, c->known_hosts_line); pen_pair(p, "known_hosts", text, TV_BASE); }
    pen_text(p, "Hydra did not accept the new key and never edits known_hosts. Verify it out of band; if the change is expected, remove the old entry yourself (ssh-keygen -R), re-qualify and review enrollment afresh.", TV_MUTED);
}
static void enrollment_pairs(struct pen *p, const struct onb_host *h) {
    char text[600];
    if (!h) return;
    pen_blank(p);
    snprintf(text, sizeof(text), "%s / phase %s%s", h->status, h->phase, h->attempted ? "" : " (not attempted yet)");
    pen_pair(p, "Enrollment", text, !strcmp(h->status, "enrolled") ? TV_SUCCESS : strcmp(h->status, "not_started") ? TV_WARNING : TV_BASE);
    pen_pair(p, "Alias", h->alias, TV_BASE);
    pen_pair(p, "Operation", h->operation_id, TV_BASE);
    if (h->key_changed) { snprintf(text, sizeof(text), "reviewed %s, presented %s", h->reviewed, h->presented); pen_pair(p, "Host key", text, TV_WARNING); }
    revocation_pairs(p, &h->revocation);
}
static void render_candidate(struct app *app, struct pen *p, const struct onb_candidate *c) {
    const struct onb_model *m = app->onboarding->model;
    long long now = (long long)time(NULL);
    char text[600], when[48], state[64];
    onb_candidate_state(m, c, state, sizeof(state));
    pen_pair(p, "Candidate", c->candidate_id, TV_STRONG);
    pen_pair(p, "Target", c->target, TV_BASE);
    pen_pair(p, "State", state, typed_tone(onb_candidate_typed(m, c), false));
    source_pairs(p, c, now);
    if (c->resolution_ok) {
        snprintf(text, sizeof(text), "%.100s@%.200s port %.8s, policy %.12s%s%.200s", c->user, c->hostname, c->port[0] ? c->port : "22", c->policy_sha256, c->ssh_config[0] ? ", config " : "", c->ssh_config);
        pen_pair(p, "Resolved", text, TV_BASE);
    } else pen_pair(p, "Resolved", "no: ssh -G could not resolve this target", TV_WARNING);
    if (c->qualified) {
        onb_time_text(c->qualified_at, when, sizeof(when));
        snprintf(text, sizeof(text), "%s at %s (%lld attempt%s, %s)", c->qualification_ok ? "compatible" : "failed", when, c->attempts, c->attempts == 1 ? "" : "s", c->freshness[0] ? c->freshness : "freshness unknown");
        pen_pair(p, "Qualification", text, c->qualification_ok ? TV_SUCCESS : TV_WARNING);
        if (c->fingerprint[0]) pen_pair(p, "Peer key", c->fingerprint, TV_BASE);
        if (c->platform[0] || c->hydra_version[0]) {
            snprintf(text, sizeof(text), "%s, Hydra %s, fleet protocol %d", onb_candidate_platform(c), c->hydra_version[0] ? c->hydra_version : "?", c->fleet_protocol);
            pen_pair(p, "Remote", text, TV_BASE);
        }
        if (c->code[0]) { snprintf(text, sizeof(text), "%s: %s", c->code, c->message); pen_pair(p, "Error", text, TV_WARNING); }
        if (c->recovery[0] && !c->key_changed) pen_pair(p, "Next", c->recovery, TV_MUTED);
        key_change_pairs(p, c);
    } else pen_pair(p, "Qualification", "not yet: Q runs one strict read-only handshake", TV_BASE);
    revocation_pairs(p, &c->revocation);
    enrollment_pairs(p, onb_host_for(m, c->candidate_id));
}
static void render_detail(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    struct pen p;
    title(app, TV_STRONG, s->detail_record ? "REPORTED RECORD (inspection only)" : "CANDIDATE DETAIL");
    pen_begin(&p, app, 2);
    if (s->detail_record && s->detail_index < s->model->record_count) {
        const struct onb_record *r = &s->model->records[s->detail_index];
        pen_pair(&p, "Record", r->name, TV_STRONG);
        pen_pair(&p, "Target", r->target, TV_BASE);
        pen_pair(&p, "Source", r->source, TV_BASE);
        pen_pair(&p, "Why not imported", r->revocation.present ? "identity revoked" : r->reason, TV_WARNING);
        revocation_pairs(&p, &r->revocation);
        if (!r->revocation.present) pen_text(&p, "A conflicting record (two names for one target, one name for two targets, or a target another name held before) is reported with its provenance and never imported; selecting it is an error.", TV_MUTED);
    } else if (!s->detail_record && s->detail_index < s->model->candidate_count) render_candidate(app, &p, &s->model->candidates[s->detail_index]);
    footer(app, &p, "Esc back to the list", TV_MUTED);
}

/* ---- Changes ---- */

static void source_block(struct pen *p, const struct onb_source_report *r) {
    char text[600], when[48];
    size_t i;
    onb_time_text(r->observed_at, when, sizeof(when));
    snprintf(text, sizeof(text), "%s:%s", r->kind, r->locator);
    pen_pair(p, "Source", text, TV_STRONG);
    snprintf(text, sizeof(text), "%s%s%s at %s, %lld record%s, %lld bytes%s%s", r->status, r->reason[0] ? ": " : "", r->reason, when, r->record_count,
             r->record_count == 1 ? "" : "s", r->bytes, r->scope[0] ? ", scope " : "", r->scope);
    pen_pair(p, "Acquisition", text, strcmp(r->status, "acquired") ? TV_WARNING : TV_BASE);
    if (r->client[0]) pen_pair(p, "Client", r->client, TV_BASE);
    if (r->credentials_omitted || r->skipped) {
        snprintf(text, sizeof(text), "%lld credential%s omitted, %lld skipped", r->credentials_omitted, r->credentials_omitted == 1 ? "" : "s", r->skipped);
        pen_pair(p, "Dropped", text, TV_BASE);
    }
    if (r->cached) { snprintf(text, sizeof(text), "%s, %llds old, %lld record%s", r->cache_status, r->cache_age, r->cache_count, r->cache_count == 1 ? "" : "s"); pen_pair(p, "Previous", text, TV_BASE); }
    else pen_pair(p, "Previous", r->first_observation ? "none: first observation of this source" : "none", TV_BASE);
    snprintf(text, sizeof(text), "%lld added, %lld removed, %lld changed, %lld conflicting, %lld revoked", r->added_count, r->removed_count, r->changed_count, r->conflicting_count, r->revoked_count);
    pen_pair(p, "Since then", text, r->conflicting_count || r->revoked_count ? TV_WARNING : TV_BASE);
    for (i = 0; i < r->added_n; i++) pen_pair(p, i ? "" : "Added", r->added[i], TV_SUCCESS);
    for (i = 0; i < r->removed_n; i++) pen_pair(p, i ? "" : "Removed", r->removed[i], TV_WARNING);
    for (i = 0; i < r->changed_n; i++) {
        snprintf(text, sizeof(text), "%s: %s -> %s", r->changed[i].name, r->changed[i].previous_target[0] ? r->changed[i].previous_target : "(same target)", r->changed[i].target);
        pen_pair(p, i ? "" : "Changed", text, TV_WARNING);
    }
    if ((r->added_count > (long long)r->added_n || r->removed_count > (long long)r->removed_n || r->changed_count > (long long)r->changed_n) && !r->added_n && !r->removed_n && !r->changed_n)
        pen_pair(p, "Names", "run discover with --diff to list them", TV_MUTED);
    pen_blank(p);
}
static void render_changes(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    const struct onb_model *m = s->model;
    struct pen p;
    size_t i;
    char text[600];
    title(app, TV_STRONG, "SNAPSHOT CHANGES / live sources since their previous acquisition");
    pen_begin(&p, app, 2);
    if (!m->source_count) pen_text(&p, "No live source was acquired for these candidates. Discover with a source (f: ssh-config, ansible-inventory:FILE or dns-sd) to compare it with its previous private snapshot.", TV_BASE);
    for (i = 0; i < m->source_count; i++) source_block(&p, &m->sources[i]);
    for (i = 0; i < m->record_count; i++) {
        const struct onb_record *r = &m->records[i];
        snprintf(text, sizeof(text), "%s (%s) from %s: %s", r->name, r->target, r->source, r->revocation.present ? "identity revoked" : r->reason);
        pen_pair(&p, i ? "" : "Not imported", text, TV_WARNING);
    }
    footer(app, &p, "Esc back to the list", TV_MUTED);
}

/* ---- Forms ---- */

static void field_row(struct app *app, struct native_onboarding *s, int index, const char *label, const char *help) {
    const struct onb_field *f = &onb_form_fields(s)[index];
    int label_width = app->content_width < 70 ? 14 : 20, width = app->content_width - label_width - 2;
    size_t start = 0, length = strlen(f->text);
    bool focused = s->focus == index;
    char shown[1100];
    if (width > 90) width = 90;
    while (f->cursor > start && (int)(f->cursor - start) >= width - 1) start++;
    snprintf(shown, sizeof(shown), "%.*s", width - 1, f->text + start);
    column(app, 0, label_width + 2, focused ? TV_STRONG : TV_MUTED, label);
    column(app, label_width + 2, width, focused ? TV_SELECTED : TV_BORDER, "");
    column(app, label_width + 2, width, focused ? TV_SELECTED : TV_BASE, shown);
    if (focused && f->cursor <= length) column(app, label_width + 2 + (int)(f->cursor - start), 1, TV_FOCUS, f->cursor < length ? "" : "_");
    app->line++;
    if (app->line < app->limit) { column(app, label_width + 2, width, TV_MUTED, help); app->line++; }
}
static void render_select(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    title(app, TV_STRONG, s->form_qualify ? "QUALIFY CANDIDATES" : "DISCOVER CANDIDATES");
    paragraph(app, s->form_qualify ? "Name what to qualify. Each selection is resolved with ssh -G and checked with one strict read-only handshake that records the authenticated peer fingerprint; the export is kept for review. Nothing is trusted, installed or enrolled."
                                   : "Name what to look at. Discovery resolves each selection with ssh -G and makes no connection; live sources read your own files or one terminating client run and are compared with their previous snapshot.", TV_BASE);
    linef(app, "");
    field_row(app, s, ONB_SELECT_ALIASES, "SSH aliases", "space separated Host names, at most 16");
    field_row(app, s, ONB_SELECT_SOURCE, "Live source", "ssh-config[:FILE], ansible-inventory:FILE or dns-sd[:DOMAIN]; empty for none");
    field_row(app, s, ONB_SELECT_NAMES, "Selected names", "records of the inventory or source to import, space separated");
    field_row(app, s, ONB_SELECT_INVENTORY, "Inventory file", "absolute path of a schema-1 or schema-2 inventory; needs selected names");
    field_row(app, s, ONB_SELECT_CONFIG, "SSH config file", "absolute path; empty: your usual SSH config");
    linef(app, "");
    if (s->problem[0]) paragraph(app, s->problem, TV_WARNING);
    else paragraph(app, s->form_qualify ? "Enter on the last field runs hydra fleet qualify --require list --progress (a new export) --json with exactly these values. Esc goes back."
                                        : "Enter on the last field runs hydra fleet discover --diff --json with exactly these values. Esc goes back.", TV_MUTED);
}
static void render_review(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    char text[320];
    snprintf(text, sizeof(text), "REVIEW ENROLLMENT OF %.80s", s->candidate);
    title(app, TV_STRONG, text);
    paragraph(app, "The review binds this candidate, its source, principal, authenticated fingerprint and SSH policy to a project (and optionally a pinned package) in a digest-confirmed intent. Nothing is applied here.", TV_BASE);
    linef(app, "");
    field_row(app, s, ONB_REVIEW_PROJECT, "Remote project", "absolute path on the host (required)");
    field_row(app, s, ONB_REVIEW_ALIAS, "Alias in Hydra", "empty: the candidate ID is used");
    field_row(app, s, ONB_REVIEW_PACKAGE, "Package file", "absolute path of an install package; empty: none pinned");
    field_row(app, s, ONB_REVIEW_SHA256, "Package SHA-256", "64 hex characters, required with a package");
    field_row(app, s, ONB_REVIEW_PREFIX, "Install prefix", "exact absolute prefix on the host, required with a package");
    linef(app, "");
    if (s->problem[0]) paragraph(app, s->problem, TV_WARNING);
    else paragraph(app, "Enter on the last field runs hydra fleet enroll review --json and shows the intent, its digest and the exact apply command. Esc goes back.", TV_MUTED);
}

/* ---- Intent ---- */

static void host_block(struct pen *p, const struct onb_host *h, size_t number) {
    char text[600];
    enum tv_style tone = !strcmp(h->status, "enrolled") ? TV_SUCCESS : !strcmp(h->status, "not_started") ? TV_BASE : TV_WARNING;
    snprintf(text, sizeof(text), "%zu. %s -> alias %s", number, h->target, h->alias);
    pen_pair(p, "Host", text, TV_STRONG);
    snprintf(text, sizeof(text), "%s / phase %s%s%s", h->status, h->phase, h->attempted ? "" : " (not attempted)", !strcmp(h->status, "outcome_unknown") ? " / last attempt's outcome is unknown: nothing is assumed" : "");
    pen_pair(p, "Progress", text, tone);
    pen_pair(p, "Candidate", h->candidate_id, TV_BASE);
    pen_pair(p, "Principal", h->principal, TV_BASE);
    pen_pair(p, "Fingerprint", h->fingerprint, TV_BASE);
    pen_pair(p, "Project", h->project, TV_BASE);
    if (h->package_sha256[0]) { snprintf(text, sizeof(text), "%s at prefix %s", h->package_sha256, h->prefix); pen_pair(p, "Package", text, TV_BASE); }
    else pen_pair(p, "Package", "none pinned: the host's installed Hydra must match", TV_BASE);
    if (h->ssh_config[0]) pen_pair(p, "SSH config", h->ssh_config, TV_BASE);
    pen_pair(p, "Operation", h->operation_id, TV_MUTED);
    if (h->key_changed) { snprintf(text, sizeof(text), "reviewed %s, presented %s; not accepted", h->reviewed, h->presented); pen_pair(p, "Host key", text, TV_WARNING); }
    revocation_pairs(p, &h->revocation);
    pen_blank(p);
}
static void render_intent(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    const struct onb_intent *in = &s->model->intent;
    struct pen p;
    size_t i, open = 0;
    char text[700], when[48];
    title(app, TV_STRONG, "ENROLLMENT INTENT / bindings and per-host progress");
    pen_begin(&p, app, 2);
    if (!in->loaded) { pen_text(&p, "No reviewed intent is loaded.", TV_BASE); footer(app, &p, "Esc back", TV_MUTED); return; }
    onb_time_text(in->reviewed_at, when, sizeof(when));
    pen_pair(&p, "Intent digest", in->digest, TV_STRONG);
    pen_pair(&p, "Intent file", in->path, TV_BASE);
    snprintf(text, sizeof(text), "%s, %zu host%s%s%s", when, in->host_count, in->host_count == 1 ? "" : "s", in->progress_recorded ? ", progress recorded" : ", not applied yet", in->partial ? ", partial" : "");
    pen_pair(&p, "Reviewed", text, TV_BASE);
    pen_blank(&p);
    for (i = 0; i < in->host_count; i++) {
        host_block(&p, &in->hosts[i], i + 1);
        if (strcmp(in->hosts[i].status, "enrolled") && strcmp(in->hosts[i].status, "revoked")) open++;
    }
    snprintf(text, sizeof(text), "hydra fleet enroll apply --input %s --confirm %s --json", in->path, in->digest);
    pen_pair(&p, "Apply command", text, TV_STRONG);
    pen_text(&p, open ? "a asks you to type the digest and then runs exactly that command: the CLI authenticates each host against its reviewed fingerprint, applies the recorded steps and records every row; rows already enrolled are skipped and a revoked binding is refused before any connection."
                      : "Every host is enrolled or revoked; the command has nothing left to apply.", TV_MUTED);
    footer(app, &p, open ? "a apply (typed digest)  s refresh status  Esc back" : "s refresh status  Esc back", TV_MUTED);
}

/* ---- Running and errors ---- */

static void render_running(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    char text[200];
    long elapsed = (long)(time(NULL) - s->job_started);
    size_t i;
    title(app, TV_STRONG, "RUNNING");
    snprintf(text, sizeof(text), "%s%s %lds", s->job_label, app->ascii ? "..." : "\xe2\x80\xa6", elapsed);
    paragraph(app, text, TV_BASE);
    linef(app, "");
    text[0] = '\0';
    for (i = 0; i < s->argv.count; i++) text_append(text, sizeof(text), "%s%s", i ? " " : "", s->argv.words[i]);
    paragraph(app, text, TV_MUTED);
    linef(app, "");
    paragraph(app, s->job_kind == ONB_JOB_APPLY ? "Esc or c cancel: the CLI records progress before every step, so the recorded status is read afterwards and nothing is replayed."
                                                : "Esc or c cancel; b returns to Hosts while it finishes.", TV_MUTED);
    if (s->notice[0]) { app->line = app->limit - 1; column(app, 0, app->content_width, TV_WARNING, s->notice); app->line++; }
}
static void render_error(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    const struct onb_envelope *e = &s->model->last;
    struct pen p;
    char text[400];
    title(app, TV_WARNING, "NOT DONE");
    pen_begin(&p, app, 2);
    snprintf(text, sizeof(text), "%s (exit status %d)", e->code[0] ? e->code : "unknown", e->exit_status);
    pen_pair(&p, "Result", text, TV_WARNING);
    pen_pair(&p, "Detail", e->message, TV_BASE);
    if (e->recovery[0]) pen_pair(&p, "Next", e->recovery, TV_BASE);
    if (!strcmp(e->code, "revoked")) pen_text(&p, "A revoked identity never enters a new intent or apply; it stays inspectable in the list.", TV_MUTED);
    if (!strcmp(e->code, "no_result")) pen_text(&p, "No result does not mean nothing happened: Enter reads the recorded enrollment status instead of retrying.", TV_MUTED);
    footer(app, &p, !strcmp(e->code, "no_result") ? "Enter reads the recorded status  Esc back" : "Enter or Esc back to the list", TV_MUTED);
}

void native_onboarding_render(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    switch (s->screen) {
        case ONB_SCREEN_LIST: render_list(app); break;
        case ONB_SCREEN_DETAIL: render_detail(app); break;
        case ONB_SCREEN_CHANGES: render_changes(app); break;
        case ONB_SCREEN_SELECT: render_select(app); break;
        case ONB_SCREEN_REVIEW: render_review(app); break;
        case ONB_SCREEN_INTENT: render_intent(app); break;
        case ONB_SCREEN_RUNNING: render_running(app); break;
        default: render_error(app); break;
    }
}

const char *native_onboarding_title(const struct app *app) {
    static const char *const titles[] = {"Hosts: onboarding", "Hosts: onboarding / candidate", "Hosts: onboarding / snapshot changes",
                                         "Hosts: onboarding / discover", "Hosts: onboarding / review", "Hosts: onboarding / intent",
                                         "Hosts: onboarding / running", "Hosts: onboarding / result"};
    return titles[app->onboarding->screen];
}

const char *native_onboarding_hints(const struct app *app, bool narrow) {
    switch (app->onboarding->screen) {
        case ONB_SCREEN_LIST: return narrow ? "Enter detail  f find  Q qualify  r review  Esc back" : "j/k select  Enter detail  f find  Q qualify  r review  i intent  c changes  s/S/P/l filters  Esc back  q quit";
        case ONB_SCREEN_SELECT: case ONB_SCREEN_REVIEW: return "Tab next field  Enter submit on the last field  Esc back";
        case ONB_SCREEN_INTENT: return narrow ? "a apply  s status  Esc back" : "j/k scroll  a apply after typing the digest  s refresh status  Esc back  q quit";
        case ONB_SCREEN_RUNNING: return "Esc or c cancel  b back to Hosts";
        default: return narrow ? "j/k scroll  Esc back" : "j/k scroll  Esc back to the list  q quit";
    }
}
