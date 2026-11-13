#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "onboarding.h"
/* Typed state, list rows, filters and the exact argv of every delegated
 * command. The typed state is derived only from recorded CLI statuses (a
 * candidate's status and its enrollment row); nothing here infers health. */

static const char *const typed_names[ONB_TYPED_COUNT] = {"new", "qualified", "failed", "conflicting", "revoked", "reviewed", "unknown", "enrolled"};
const char *onb_typed_name(enum onb_typed typed) { return typed >= 0 && typed < ONB_TYPED_COUNT ? typed_names[typed] : "all"; }

const struct onb_host *onb_host_for(const struct onb_model *m, const char *candidate_id) {
    size_t i;
    if (!m->intent.loaded || !candidate_id[0]) return NULL;
    for (i = 0; i < m->intent.host_count; i++) if (!strcmp(m->intent.hosts[i].candidate_id, candidate_id)) return &m->intent.hosts[i];
    return NULL;
}

static enum onb_typed host_typed(const struct onb_host *h) {
    if (!strcmp(h->status, "enrolled")) return ONB_ENROLLED;
    if (!strcmp(h->status, "revoked")) return ONB_REVOKED;
    if (!strcmp(h->status, "not_started")) return ONB_REVIEWED;
    if (!strcmp(h->status, "outcome_unknown")) return ONB_UNKNOWN;
    return ONB_FAILED;
}

enum onb_typed onb_candidate_typed(const struct onb_model *m, const struct onb_candidate *c) {
    const struct onb_host *h = onb_host_for(m, c->candidate_id);
    if (!strcmp(c->status, "revoked")) return ONB_REVOKED;
    if (h) return host_typed(h);
    if (!strcmp(c->status, "compatible")) return ONB_QUALIFIED;
    if (!strcmp(c->status, "resolution_failed") || !strcmp(c->status, "qualification_failed")) return ONB_FAILED;
    return ONB_NEW;
}

static const char *apply_word(const char *status) {
    if (!strcmp(status, "outcome_unknown")) return "outcome unknown";
    if (!strcmp(status, "not_started")) return "reviewed, not applied";
    if (!strcmp(status, "capability_changed")) return "capability changed";
    if (!strcmp(status, "project_unavailable")) return "project unavailable";
    if (!strcmp(status, "project_changed")) return "project changed";
    if (!strcmp(status, "review_required")) return "review required";
    if (!strcmp(status, "state_unavailable")) return "state unavailable";
    if (!strcmp(status, "host_key")) return "host key changed";
    return status;
}

/* Words for a recorded status; an unlisted status is spelled as recorded. */
static const char *word_for(const char *const (*words)[2], const char *status, const char *otherwise) {
    for (; (*words)[0]; words++) if (!strcmp((*words)[0], status)) return (*words)[1];
    return otherwise;
}
static void host_state(const struct onb_host *h, char *out, size_t size) {
    if (!strcmp(h->status, "not_started")) snprintf(out, size, "reviewed, not applied");
    else if (!strcmp(h->status, "enrolled")) snprintf(out, size, "enrolled: %s", h->phase);
    else snprintf(out, size, "apply: %s", apply_word(h->status));
}
static void qualification_failure(const struct onb_model *m, const struct onb_candidate *c, char *out, size_t size) {
    static const char *const words[][2] = {{"host_key_changed", "failed: key changed"}, {"host_key_unknown", "failed: key unknown"}, {NULL, NULL}};
    const char *word = word_for(words, c->code, NULL);
    if (word) snprintf(out, size, "%s", word);
    else if (!strcmp(c->code, "capability_unavailable")) snprintf(out, size, "failed: no %s", m->capability[0] ? m->capability : "capability");
    else snprintf(out, size, "failed: %.40s", c->code[0] ? c->code : "qualification");
}
void onb_candidate_state(const struct onb_model *m, const struct onb_candidate *c, char *out, size_t size) {
    static const char *const words[][2] = {{"compatible", "qualified"}, {"resolution_failed", "failed: unresolved"},
                                           {"not_qualified", "new: not yet qualified"}, {NULL, NULL}};
    const struct onb_host *h = onb_host_for(m, c->candidate_id);
    if (!strcmp(c->status, "revoked")) snprintf(out, size, "revoked");
    else if (h) host_state(h, out, size);
    else if (!strcmp(c->status, "qualification_failed")) qualification_failure(m, c, out, size);
    else snprintf(out, size, "%s", word_for(words, c->status, "new: discovered"));
}

/* One list row's cells: a candidate, or a reported record that is never imported. */
static void candidate_cells(const struct onb_model *m, const struct onb_candidate *c, long long now, struct onb_row_cells *cells) {
    const char *first = c->source_count ? c->sources[0].name : "";
    onb_candidate_state(m, c, cells->state, sizeof(cells->state));
    onb_candidate_age(c, now, cells->age, sizeof(cells->age));
    cells->name = first[0] && strcmp(first, c->target) ? first : c->target;
    cells->source = onb_candidate_source(c); cells->platform = onb_candidate_platform(c);
    cells->typed = onb_candidate_typed(m, c);
}
static void record_cells(const struct onb_record *r, struct onb_row_cells *cells) {
    bool revoked = r->revocation.present;
    snprintf(cells->state, sizeof(cells->state), "%s: %s", revoked ? "revoked" : "conflicting", revoked ? "not imported" : r->reason);
    copy_text(cells->age, sizeof(cells->age), "-");
    cells->name = r->name; cells->source = r->source; cells->platform = "-";
    cells->typed = revoked ? ONB_REVOKED : ONB_CONFLICTING;
}
void onb_row_cells(const struct onb_model *m, const struct onb_row *row, long long now, struct onb_row_cells *cells) {
    if (row->kind == ONB_ROW_CANDIDATE) candidate_cells(m, &m->candidates[row->index], now, cells);
    else record_cells(&m->records[row->index], cells);
}

const char *onb_candidate_source(const struct onb_candidate *c) {
    size_t i;
    /* A live source names the acquisition; a plain alias is "ssh". */
    for (i = 0; i < c->source_count; i++) if (c->sources[i].scope[0]) return c->sources[i].kind;
    if (!c->source_count) return "-";
    if (!strcmp(c->sources[0].kind, "ssh-config")) return "ssh";
    if (!strcmp(c->sources[0].kind, "static")) return "inventory";
    return c->sources[0].kind;
}

const char *onb_candidate_platform(const struct onb_candidate *c) { return c->platform[0] ? c->platform : "unknown"; }

void onb_age_text(long long seconds, char *out, size_t size) {
    if (seconds < 0) seconds = 0;
    if (seconds < 90) snprintf(out, size, "%llds", seconds);
    else if (seconds < 5400) snprintf(out, size, "%lldm", seconds / 60);
    else if (seconds < 172800) snprintf(out, size, "%lldh", seconds / 3600);
    else snprintf(out, size, "%lldd", seconds / 86400);
}

void onb_candidate_age(const struct onb_candidate *c, long long now, char *out, size_t size) {
    long long at = c->qualified_at;
    size_t i;
    for (i = 0; !at && i < c->source_count; i++) at = c->sources[i].observed_at;
    if (!at) { snprintf(out, size, "never"); return; }
    onb_age_text(now - at, out, size);
}

void onb_time_text(long long at, char *out, size_t size) {
    time_t t = (time_t)at;
    struct tm tm;
    if (!at || !gmtime_r(&t, &tm)) { snprintf(out, size, "-"); return; }
    strftime(out, size, "%Y-%m-%d %H:%M UTC", &tm);
}

/* ---- Filters and rows ---- */

static bool labels_match(const struct onb_candidate *c, const char *label) {
    size_t i;
    if (!label[0]) return true;
    for (i = 0; i < c->source_count; i++) if (text_matches(c->sources[i].labels, label)) return true;
    return false;
}

static bool candidate_visible(const struct onb_model *m, const struct onb_filter *f, const struct onb_candidate *c) {
    if (f->typed >= 0 && onb_candidate_typed(m, c) != (enum onb_typed)f->typed) return false;
    if (f->source[0] && strcmp(onb_candidate_source(c), f->source)) return false;
    if (f->platform[0] && strcmp(onb_candidate_platform(c), f->platform)) return false;
    if (!labels_match(c, f->label)) return false;
    if (f->search[0] && !text_matches(c->target, f->search) && !text_matches(c->candidate_id, f->search) &&
        !(c->source_count && text_matches(c->sources[0].name, f->search))) return false;
    return true;
}

static bool record_visible(const struct onb_filter *f, const struct onb_record *r) {
    enum onb_typed typed = r->revocation.present || !strcmp(r->reason, "revoked") ? ONB_REVOKED : ONB_CONFLICTING;
    if (f->typed >= 0 && typed != (enum onb_typed)f->typed) return false;
    if (f->source[0] && strcmp(r->source, f->source)) return false;
    if (f->platform[0] && strcmp(f->platform, "unknown")) return false;
    if (f->label[0]) return false;
    return !f->search[0] || text_matches(r->name, f->search) || text_matches(r->target, f->search);
}

bool onb_row_visible(const struct onb_model *m, const struct onb_filter *f, const struct onb_row *row) {
    if (row->kind == ONB_ROW_CANDIDATE) return row->index < m->candidate_count && candidate_visible(m, f, &m->candidates[row->index]);
    return row->index < m->record_count && record_visible(f, &m->records[row->index]);
}

size_t onb_rows(const struct onb_model *m, const struct onb_filter *f, struct onb_row *rows, size_t capacity) {
    size_t i, count = 0;
    for (i = 0; i < m->candidate_count && count < capacity; i++) {
        struct onb_row row = {ONB_ROW_CANDIDATE, i};
        if (onb_row_visible(m, f, &row)) rows[count++] = row;
    }
    for (i = 0; i < m->record_count && count < capacity; i++) {
        struct onb_row row = {ONB_ROW_RECORD, i};
        if (onb_row_visible(m, f, &row)) rows[count++] = row;
    }
    return count;
}

/* The distinct values in the model order; the one after current, then all. */
static void next_distinct(char *current, size_t size, const char *const *values, size_t count) {
    size_t i, j, seen = 0;
    bool after = !current[0];
    for (i = 0; i < count; i++) {
        for (j = 0; j < i; j++) if (!strcmp(values[j], values[i])) break;
        if (j < i) continue;
        seen++;
        if (after) { copy_text(current, size, values[i]); return; }
        if (!strcmp(values[i], current)) after = true;
    }
    current[0] = '\0';
    (void)seen;
}

void onb_next_source(const struct onb_model *m, char *current, size_t size) {
    const char *values[ONB_CANDIDATES + ONB_RECORDS];
    size_t i, n = 0;
    for (i = 0; i < m->candidate_count; i++) values[n++] = onb_candidate_source(&m->candidates[i]);
    for (i = 0; i < m->record_count; i++) values[n++] = m->records[i].source;
    next_distinct(current, size, values, n);
}

void onb_next_platform(const struct onb_model *m, char *current, size_t size) {
    const char *values[ONB_CANDIDATES];
    size_t i, n = 0;
    for (i = 0; i < m->candidate_count; i++) values[n++] = onb_candidate_platform(&m->candidates[i]);
    next_distinct(current, size, values, n);
}

void onb_filter_text(const struct onb_filter *f, char *out, size_t size) {
    out[0] = '\0';
    if (f->typed >= 0) text_append(out, size, " state:%s", onb_typed_name((enum onb_typed)f->typed));
    if (f->source[0]) text_append(out, size, " source:%s", f->source);
    if (f->platform[0]) text_append(out, size, " platform:%s", f->platform);
    if (f->label[0]) text_append(out, size, " label:%s", f->label);
    if (f->search[0]) text_append(out, size, " search:%s", f->search);
}

/* ---- argv ---- */

void onb_argv_reset(struct onb_argv *a) { a->count = 0; a->words[0][0] = '\0'; }

bool onb_argv_add(struct onb_argv *a, const char *word) {
    if (a->count >= ONB_ARGV_MAX || strlen(word) >= ONB_ARG_TEXT || !word[0]) return false;
    copy_text(a->words[a->count++], ONB_ARG_TEXT, word);
    return true;
}

/* Space-separated words of a field, each added after option; false on overflow. */
static bool add_words(struct onb_argv *a, const char *option, const char *text, size_t *added) {
    char copy[ONB_SELECTION_TEXT], *save = NULL, *word;
    copy_text(copy, sizeof(copy), text);
    for (word = strtok_r(copy, " \t", &save); word; word = strtok_r(NULL, " \t", &save)) {
        if (!onb_argv_add(a, option) || !onb_argv_add(a, word)) return false;
        (*added)++;
    }
    return true;
}

/* An option and its value, added only when the value is set. */
static bool add_option(struct onb_argv *a, const char *option, const char *value) {
    return !value[0] || (onb_argv_add(a, option) && onb_argv_add(a, value));
}
/* The selection is one of: aliases, an inventory with names, a live source
 * (names optional for discover, required for qualify). */
static bool selection_complete(const struct onb_selection *s, size_t aliases, size_t names, bool qualify) {
    bool listed = s->inventory[0] || s->source[0];
    if (listed && !names && (qualify || s->inventory[0])) return false;
    if (names && !listed) return false;
    return aliases || names || s->source[0];
}
static bool add_mode(struct onb_argv *a, bool qualify, const char *export_path) {
    if (!qualify) return onb_argv_add(a, "--diff");
    if (!export_path || export_path[0] != '/') return false;
    return onb_argv_add(a, "--require") && onb_argv_add(a, "list") && onb_argv_add(a, "--progress") && onb_argv_add(a, export_path);
}
bool onb_discover_argv(struct onb_argv *a, const struct onb_selection *s, bool qualify, const char *export_path) {
    size_t aliases = 0, names = 0;
    onb_argv_reset(a);
    return onb_argv_add(a, "hydra") && onb_argv_add(a, "fleet") && onb_argv_add(a, qualify ? "qualify" : "discover") &&
        add_words(a, "--ssh", s->aliases, &aliases) && aliases <= 16 &&
        add_option(a, "--inventory", s->inventory) && add_option(a, "--source", s->source) &&
        add_words(a, "--select", s->names, &names) && names <= 100 && selection_complete(s, aliases, names, qualify) &&
        add_option(a, "--ssh-config", s->config) && add_mode(a, qualify, export_path) && onb_argv_add(a, "--json");
}

static bool append_word(char *text, size_t size, const char *word) {
    size_t i;
    char copy[ONB_SELECTION_TEXT], *save = NULL, *existing;
    if (strlen(text) + strlen(word) + 2 > size) return false;
    copy_text(copy, sizeof(copy), text);
    for (existing = strtok_r(copy, " ", &save); existing; existing = strtok_r(NULL, " ", &save)) if (!strcmp(existing, word)) return true;
    for (i = 0; word[i]; i++) if (word[i] == ' ' || word[i] == '\t') return false;
    if (text[0]) strcat(text, " ");
    strcat(text, word);
    return true;
}

/* A source names one live selector or one inventory for the whole export. */
static bool set_single(char *slot, size_t size, const char *value) {
    if (slot[0] && strcmp(slot, value)) return false;
    copy_text(slot, size, value);
    return true;
}
static bool select_source(struct onb_selection *s, const struct onb_source_ref *r) {
    char selector[ONB_PATH + 40];
    if (r->scope[0]) {
        snprintf(selector, sizeof(selector), "%s:%s", r->kind, r->locator);
        return set_single(s->source, sizeof(s->source), selector) && append_word(s->names, sizeof(s->names), r->name);
    }
    if (!strcmp(r->kind, "ssh-config")) return append_word(s->aliases, sizeof(s->aliases), r->name);
    if (!strcmp(r->kind, "static"))
        return set_single(s->inventory, sizeof(s->inventory), r->locator) && append_word(s->names, sizeof(s->names), r->name);
    return false;
}
bool onb_export_selection(const struct onb_model *m, struct onb_selection *s) {
    size_t i, j;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < m->candidate_count; i++) {
        const struct onb_candidate *c = &m->candidates[i];
        if (c->ssh_config[0]) copy_text(s->config, sizeof(s->config), c->ssh_config);
        for (j = 0; j < c->source_count; j++) if (!select_source(s, &c->sources[j])) return false;
    }
    return s->aliases[0] || s->names[0];
}

static bool absolute(const char *path) { return path && path[0] == '/'; }
/* A package, its digest and the install prefix go together or not at all. */
static bool add_package(struct onb_argv *a, const struct onb_review *r) {
    if (!r->package[0] && !r->sha256[0] && !r->prefix[0]) return true;
    if (!absolute(r->package) || !onb_hex_digest(r->sha256) || !absolute(r->prefix)) return false;
    return onb_argv_add(a, "--package") && onb_argv_add(a, r->package) && onb_argv_add(a, "--sha256") && onb_argv_add(a, r->sha256) &&
        onb_argv_add(a, "--prefix") && onb_argv_add(a, r->prefix);
}
bool onb_review_argv(struct onb_argv *a, const char *export_path, const char *candidate_id, const struct onb_review *r, const char *intent_path) {
    onb_argv_reset(a);
    if (!absolute(export_path) || !absolute(intent_path) || strncmp(candidate_id, "cand_", 5) || !absolute(r->project)) return false;
    return onb_argv_add(a, "hydra") && onb_argv_add(a, "fleet") && onb_argv_add(a, "enroll") && onb_argv_add(a, "review") &&
        onb_argv_add(a, "--input") && onb_argv_add(a, export_path) && onb_argv_add(a, "--candidate") && onb_argv_add(a, candidate_id) &&
        onb_argv_add(a, "--project") && onb_argv_add(a, r->project) && add_option(a, "--alias", r->alias) && add_package(a, r) &&
        onb_argv_add(a, "--output") && onb_argv_add(a, intent_path) && onb_argv_add(a, "--json");
}

bool onb_status_argv(struct onb_argv *a, const char *intent_path) {
    onb_argv_reset(a);
    if (!intent_path || intent_path[0] != '/') return false;
    return onb_argv_add(a, "hydra") && onb_argv_add(a, "fleet") && onb_argv_add(a, "enroll") && onb_argv_add(a, "status") &&
        onb_argv_add(a, "--input") && onb_argv_add(a, intent_path) && onb_argv_add(a, "--json");
}

bool onb_apply_argv(struct onb_argv *a, const char *intent_path, const char *digest) {
    onb_argv_reset(a);
    if (!intent_path || intent_path[0] != '/' || !onb_hex_digest(digest)) return false;
    return onb_argv_add(a, "hydra") && onb_argv_add(a, "fleet") && onb_argv_add(a, "enroll") && onb_argv_add(a, "apply") &&
        onb_argv_add(a, "--input") && onb_argv_add(a, intent_path) && onb_argv_add(a, "--confirm") && onb_argv_add(a, digest) && onb_argv_add(a, "--json");
}

bool onb_apply_argv_valid(const struct onb_argv *a, const char *intent_path, const char *digest) {
    const char *const fixed[] = {"hydra", "fleet", "enroll", "apply", "--input", NULL, "--confirm", NULL, "--json"};
    size_t i;
    if (a->count != 9 || !onb_hex_digest(digest) || !intent_path || intent_path[0] != '/') return false;
    for (i = 0; i < 9; i++) {
        const char *expected = i == 5 ? intent_path : i == 7 ? digest : fixed[i];
        if (strcmp(a->words[i], expected)) return false;
    }
    return true;
}

static bool under(const char *path, const char *directory) {
    size_t n = strlen(directory);
    return n && path[0] == '/' && !strncmp(path, directory, n) && path[n] == '/' && !strstr(path, "/../") && !strchr(path + n + 1, '/');
}

static bool valued_option(const char *word) {
    static const char *const valued[] = {"--ssh", "--inventory", "--select", "--source", "--ssh-config", "--require", "--progress", "--timeout", NULL};
    size_t i;
    for (i = 0; valued[i]; i++) if (!strcmp(word, valued[i])) return true;
    return false;
}
/* The option at i takes the next word as its value; --progress only for
 * qualify and only to a file directly under the onboarding directory. */
static bool option_valid(const struct onb_argv *a, size_t i, bool qualify, const char *directory, bool *progress) {
    const char *option = a->words[i], *value = a->words[i + 1];
    if (!valued_option(option) || i + 2 >= a->count || value[0] == '-' || !value[0]) return false;
    if (strcmp(option, "--progress")) return true;
    *progress = true;
    return qualify && under(value, directory);
}
bool onb_discover_argv_valid(const struct onb_argv *a, const char *directory) {
    size_t i;
    bool qualify, progress = false;
    if (a->count < 5 || strcmp(a->words[0], "hydra") || strcmp(a->words[1], "fleet") || strcmp(a->words[a->count - 1], "--json")) return false;
    qualify = !strcmp(a->words[2], "qualify");
    if (!qualify && strcmp(a->words[2], "discover")) return false;
    for (i = 3; i + 1 < a->count; i++) {
        if (!qualify && !strcmp(a->words[i], "--diff")) continue;
        if (!option_valid(a, i, qualify, directory, &progress)) return false;
        i++;
    }
    return !qualify || progress;
}

bool onb_hex_digest(const char *text) { return text && strlen(text) == 64 && strspn(text, "0123456789abcdef") == 64; }

/* The confirmation is the digest's leading characters as shown on screen:
 * at least twelve, all matching, so a prefix of another intent never confirms. */
bool onb_digest_confirms(const char *typed, const char *digest) {
    size_t n = strlen(typed);
    return onb_hex_digest(digest) && n >= 12 && n <= 64 && !strncmp(typed, digest, n);
}

const char *onb_path_problem(const char *path, bool required) {
    if (!path[0]) return required ? "This path is required" : NULL;
    if (path[0] != '/') return "Enter an absolute path (starting with /)";
    if (strlen(path) >= ONB_PATH) return "The path is too long";
    return NULL;
}
