#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "onboarding_flow.h"
#include <dirent.h>
/* Onboarding controller: which child runs, what its envelope leads to, and
 * the keys of each screen. Every record shown comes from a CLI envelope or
 * from an export the CLI wrote; the only mutation is `fleet enroll apply`,
 * run with the exact intent and digest the operator confirmed. */
#define ONB_READ_BUDGET_MS (5L * 60L * 1000L)
#define ONB_APPLY_BUDGET_MS (60L * 60L * 1000L)

struct native_onboarding *native_onboarding_state(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    if (s) return s;
    s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->model = calloc(1, sizeof(*s->model));
    if (!s->model) { free(s); return NULL; }
    s->job.fd = -1;
    s->filter.typed = -1;
    app->onboarding = s;
    return s;
}

void native_onboarding_destroy(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    if (!s) return;
    /* An apply that is still running finishes and records its own rows. */
    setup_capture_release(&s->job);
    free(s->model); free(s); app->onboarding = NULL;
}

bool native_onboarding_active(const struct app *app) { return app->onboarding && app->onboarding->open; }

static void notice(struct native_onboarding *s, const char *text) { copy_text(s->notice, sizeof(s->notice), text); }

/* ---- Directory records ---- */

/* Highest N of PREFIX-N.json in the onboarding directory, 0 when none. */
static unsigned highest(const char *directory, const char *prefix) {
    DIR *d = opendir(directory);
    struct dirent *e;
    unsigned best = 0;
    size_t n = strlen(prefix);
    if (!d) return 0;
    while ((e = readdir(d))) {
        unsigned value;
        char *end;
        if (strncmp(e->d_name, prefix, n) || e->d_name[n] != '-') continue;
        value = (unsigned)strtoul(e->d_name + n + 1, &end, 10);
        if (end == e->d_name + n + 1 || strcmp(end, ".json") || value > 999999) continue;
        if (value > best) best = value;
    }
    closedir(d);
    return best;
}

static void numbered(const struct native_onboarding *s, const char *prefix, unsigned number, char *out, size_t size) {
    snprintf(out, size, "%.*s/%.16s-%u.json", (int)(ONB_PATH - 40), s->directory, prefix, number);
}

/* ---- Children ---- */

static char **argv_pointers(struct native_onboarding *s, const char *hydra, char **out) {
    size_t i;
    out[0] = (char *)hydra;
    for (i = 1; i < s->argv.count; i++) out[i] = s->argv.words[i];
    out[s->argv.count] = NULL;
    return out;
}

static bool job_start(struct app *app, struct native_onboarding *s, enum onb_job kind) {
    char *argv[ONB_ARGV_MAX + 1];
    long budget = kind == ONB_JOB_APPLY ? ONB_APPLY_BUDGET_MS : ONB_READ_BUDGET_MS;
    if (s->job.active) { notice(s, "A command is still running; wait for its result"); return false; }
    if (!s->argv.count) { notice(s, "Nothing to run"); return false; }
    if (!setup_capture_start(&s->job, argv_pointers(s, app->hydra, argv), budget)) { notice(s, "Hydra could not start the command"); return false; }
    s->job_kind = kind; s->job_started = time(NULL);
    onb_job_label(kind, s->job_label, sizeof(s->job_label));
    /* A cancelled or failed job returns to the screen it started from; a
     * form has nothing to show about it, so those return to the list. */
    s->back = s->screen == ONB_SCREEN_RUNNING ? s->back : s->screen == ONB_SCREEN_SELECT || s->screen == ONB_SCREEN_REVIEW ? ONB_SCREEN_LIST : s->screen;
    s->screen = ONB_SCREEN_RUNNING; s->notice[0] = '\0'; s->scroll = 0;
    return true;
}

static void status_start(struct app *app, struct native_onboarding *s) {
    if (!s->model->intent.loaded && !s->intent_path[0]) return;
    if (!onb_status_argv(&s->argv, s->intent_path[0] ? s->intent_path : s->model->intent.path)) return;
    (void)job_start(app, s, ONB_JOB_STATUS);
}

static void home_start(struct app *app, struct native_onboarding *s) {
    onb_argv_reset(&s->argv);
    (void)onb_argv_add(&s->argv, "hydra"); (void)onb_argv_add(&s->argv, "tui"); (void)onb_argv_add(&s->argv, "--onboarding-home");
    (void)job_start(app, s, ONB_JOB_HOME);
}

/* The newest export and intent of the directory; the intent's progress is
 * read through `enroll status`, never from the private enrollment state. */
static void load_records(struct app *app, struct native_onboarding *s) {
    unsigned export_n = highest(s->directory, "qualification"), intent_n = highest(s->directory, "intent");
    s->loaded = true;
    s->model->candidate_count = s->model->source_count = s->model->record_count = 0;
    s->model->export_path[0] = '\0';
    memset(&s->model->intent, 0, sizeof(s->model->intent));
    if (export_n) {
        char path[ONB_PATH];
        numbered(s, "qualification", export_n, path, sizeof(path));
        if (!onb_parse_file(s->model, path) && s->model->candidate_count) copy_text(s->model->export_path, sizeof(s->model->export_path), path);
        else snprintf(s->notice, sizeof(s->notice), "The newest export %.150s could not be read", path);
    }
    s->intent_path[0] = '\0';
    if (intent_n) { numbered(s, "intent", intent_n, s->intent_path, sizeof(s->intent_path)); status_start(app, s); }
}

/* ---- Results ---- */

static void show_error(struct native_onboarding *s, const char *code, const char *message) {
    struct onb_envelope *e = &s->model->last;
    e->parsed = true; e->ok = false;
    copy_text(e->code, sizeof(e->code), code); copy_text(e->message, sizeof(e->message), message); e->recovery[0] = '\0';
    s->screen = ONB_SCREEN_ERROR; s->scroll = 0;
}

static void counts_notice(struct native_onboarding *s, const char *verb) {
    const struct onb_model *m = s->model;
    snprintf(s->notice, sizeof(s->notice), "%s %zu candidate%s%s%s", verb, m->candidate_count, m->candidate_count == 1 ? "" : "s",
             m->record_count ? " and reported records" : "", m->last.ok ? "" : "; some could not be acquired, resolved or qualified (see each row)");
}

static void discovery_result(struct native_onboarding *s, bool qualify, const char *export_path) {
    struct onb_model *m = s->model;
    if (!m->last.parsed) return;
    if (!m->candidate_count && !m->record_count) { s->screen = ONB_SCREEN_ERROR; s->scroll = 0; return; }
    copy_text(m->export_path, sizeof(m->export_path), qualify ? export_path : "");
    s->screen = ONB_SCREEN_LIST; s->selected = 0; s->scroll = 0;
    counts_notice(s, qualify ? "Qualified" : "Discovered");
    if (qualify && !m->complete && m->remaining) text_append(s->notice, sizeof(s->notice), "; %lld remain: Q continues the batch", m->remaining);
}

static void review_result(struct native_onboarding *s, const char *intent_path) {
    struct onb_model *m = s->model;
    if (!m->last.ok) { s->screen = ONB_SCREEN_ERROR; s->scroll = 0; return; }
    copy_text(m->intent.path, sizeof(m->intent.path), intent_path);
    copy_text(s->intent_path, sizeof(s->intent_path), intent_path);
    s->screen = ONB_SCREEN_INTENT; s->scroll = 0;
    notice(s, "Reviewed: nothing has been applied. Check every binding; a applies after you confirm the digest");
}

static void status_result(struct native_onboarding *s) {
    struct onb_model *m = s->model;
    if (m->last.ok) { s->screen = s->back == ONB_SCREEN_RUNNING ? ONB_SCREEN_LIST : s->back; return; }
    s->screen = ONB_SCREEN_LIST;
    snprintf(s->notice, sizeof(s->notice), "Enrollment status unavailable (%.40s): %.150s", m->last.code, m->last.message);
    memset(&m->intent, 0, sizeof(m->intent)); s->intent_path[0] = '\0';
}

static void apply_result(struct native_onboarding *s) {
    struct onb_model *m = s->model;
    size_t i, enrolled = 0, unknown = 0;
    if (!m->last.parsed || (!m->last.ok && !m->intent.progress_recorded)) { s->screen = ONB_SCREEN_ERROR; s->scroll = 0; return; }
    for (i = 0; i < m->intent.host_count; i++) {
        if (!strcmp(m->intent.hosts[i].status, "enrolled")) enrolled++;
        if (!strcmp(m->intent.hosts[i].status, "outcome_unknown")) unknown++;
    }
    s->screen = ONB_SCREEN_INTENT; s->scroll = 0;
    snprintf(s->notice, sizeof(s->notice), "Apply finished: %zu of %zu enrolled%s%s", enrolled, m->intent.host_count,
             unknown ? ", some outcomes unknown (inspect before continuing)" : "", m->intent.partial && !unknown ? ", some rows did not finish" : "");
}

static void home_result(struct app *app, struct native_onboarding *s, const char *text) {
    char line[ONB_PATH];
    size_t n = text ? strcspn(text, "\n") : 0;
    if (!n || n >= sizeof(line) || text[0] != '/') { show_error(s, "onboarding_unavailable", "Hydra did not name the onboarding directory; is this the installed hydra CLI?"); return; }
    memcpy(line, text, n); line[n] = '\0';
    copy_text(s->directory, sizeof(s->directory), line);
    s->directory_known = true;
    s->screen = ONB_SCREEN_LIST;
    load_records(app, s);
}

static void no_result(struct native_onboarding *s, int exit_status) {
    char message[256];
    if (exit_status == 124) snprintf(message, sizeof(message), "The command did not finish before its deadline and was stopped.");
    else snprintf(message, sizeof(message), "The command ended with exit status %d and no readable result.", exit_status);
    show_error(s, "no_result", message);
}

static void job_finish(struct app *app, struct native_onboarding *s) {
    enum onb_job kind = s->job_kind;
    size_t length;
    int exit_status;
    bool cancelled = s->job.cancelled, parsed;
    char *text = setup_capture_finish(&s->job, &length, &exit_status);
    char path[ONB_PATH];
    s->job_kind = ONB_JOB_NONE; s->exit_status = exit_status;
    copy_text(path, sizeof(path), s->argv.count > 1 ? s->argv.words[s->argv.count - 2] : "");
    if (cancelled) {
        free(text);
        char label[sizeof(s->job_label)];
        copy_text(label, sizeof(label), s->job_label);
        s->screen = s->back;
        /* The status read replaces the label and notice when it starts; set it after. */
        if (kind == ONB_JOB_APPLY) status_start(app, s);
        snprintf(s->notice, sizeof(s->notice), kind == ONB_JOB_APPLY ? "Cancelled: %s. Rows it reached may be outcome_unknown; reading the recorded status, nothing reruns"
                                                                     : "Cancelled: %s. Nothing further runs until you ask", label);
        return;
    }
    if (kind == ONB_JOB_HOME) { home_result(app, s, text); free(text); return; }
    parsed = text && !onb_parse(s->model, text, length, exit_status);
    free(text);
    if (!parsed) { no_result(s, exit_status); return; }
    switch (kind) {
        case ONB_JOB_DISCOVER: discovery_result(s, false, NULL); break;
        case ONB_JOB_QUALIFY: discovery_result(s, true, path); break;
        case ONB_JOB_REVIEW: review_result(s, path); break;
        case ONB_JOB_STATUS: status_result(s); break;
        case ONB_JOB_APPLY: apply_result(s); break;
        default: s->screen = s->back; break;
    }
}

void native_onboarding_tick(struct app *app) {
    struct native_onboarding *s = app->onboarding;
    if (!s) return;
    if (s->job.active && setup_capture_step(&s->job)) job_finish(app, s);
}

/* ---- Entry points ---- */

void native_onboarding_open(struct app *app) {
    struct native_onboarding *s = native_onboarding_state(app);
    if (!s) { copy_text(app->notice, sizeof(app->notice), "Onboarding is unavailable: out of memory"); return; }
    s->open = true; s->notice[0] = '\0';
    if (s->job.active) { s->screen = ONB_SCREEN_RUNNING; return; }
    s->screen = ONB_SCREEN_LIST;
    if (!s->directory_known) home_start(app, s);
    else if (!s->loaded) load_records(app, s);
}

static void close_view(struct app *app, struct native_onboarding *s) {
    s->open = false;
    if (s->job.active) snprintf(app->notice, sizeof(app->notice), "%s keeps running; O shows its result", s->job_label);
}

void native_onboarding_close(struct app *app) {
    if (app->onboarding && app->onboarding->open) close_view(app, app->onboarding);
}

/* ---- Actions ---- */

static size_t rows_of(const struct native_onboarding *s, struct onb_row *rows) {
    return onb_rows(s->model, &s->filter, rows, ONB_CANDIDATES + ONB_RECORDS);
}

static const struct onb_candidate *selected_candidate(const struct native_onboarding *s) {
    struct onb_row rows[ONB_CANDIDATES + ONB_RECORDS];
    size_t count = rows_of(s, rows);
    if (s->selected >= count || rows[s->selected].kind != ONB_ROW_CANDIDATE) return NULL;
    return &s->model->candidates[rows[s->selected].index];
}

void onb_open_detail(struct native_onboarding *s) {
    struct onb_row rows[ONB_CANDIDATES + ONB_RECORDS];
    size_t count = rows_of(s, rows);
    if (s->selected >= count) { notice(s, "No row is selected"); return; }
    s->detail_record = rows[s->selected].kind == ONB_ROW_RECORD;
    s->detail_index = rows[s->selected].index;
    s->screen = ONB_SCREEN_DETAIL; s->scroll = 0;
}

static void open_form(struct native_onboarding *s, enum onb_screen screen) {
    s->screen = screen; s->focus = 0; s->problem[0] = '\0'; s->scroll = 0; s->form_qualify = false;
}

static void submit_select(struct app *app, struct native_onboarding *s) {
    struct onb_selection sel;
    const char *problem;
    char path[ONB_PATH] = "";
    memset(&sel, 0, sizeof(sel));
    copy_text(sel.aliases, sizeof(sel.aliases), s->select_fields[ONB_SELECT_ALIASES].text);
    copy_text(sel.source, sizeof(sel.source), s->select_fields[ONB_SELECT_SOURCE].text);
    copy_text(sel.names, sizeof(sel.names), s->select_fields[ONB_SELECT_NAMES].text);
    copy_text(sel.inventory, sizeof(sel.inventory), s->select_fields[ONB_SELECT_INVENTORY].text);
    copy_text(sel.config, sizeof(sel.config), s->select_fields[ONB_SELECT_CONFIG].text);
    if ((problem = onb_path_problem(sel.inventory, false)) || (problem = onb_path_problem(sel.config, false))) { copy_text(s->problem, sizeof(s->problem), problem); return; }
    if (s->form_qualify) numbered(s, "qualification", highest(s->directory, "qualification") + 1, path, sizeof(path));
    if (!onb_discover_argv(&s->argv, &sel, s->form_qualify, path) || (s->form_qualify && !onb_discover_argv_valid(&s->argv, s->directory))) {
        copy_text(s->problem, sizeof(s->problem), "Name at most 16 SSH aliases, an inventory or source with its selected names; an inventory or a qualify needs names");
        return;
    }
    s->problem[0] = '\0';
    (void)job_start(app, s, s->form_qualify ? ONB_JOB_QUALIFY : ONB_JOB_DISCOVER);
}

/* Q: one strict read-only handshake per listed candidate, recorded in the
 * loaded export (continuing its batch) or in a new export of this selection. */
static void qualify_start(struct app *app, struct native_onboarding *s) {
    struct onb_selection sel;
    char path[ONB_PATH];
    if (!s->directory_known) { notice(s, "The onboarding directory is not known yet"); return; }
    if (!onb_export_selection(s->model, &sel)) {
        /* Nothing listed: name the selection and qualify it in one step. */
        open_form(s, ONB_SCREEN_SELECT); s->form_qualify = true;
        return;
    }
    if (s->model->export_path[0]) copy_text(path, sizeof(path), s->model->export_path);
    else numbered(s, "qualification", highest(s->directory, "qualification") + 1, path, sizeof(path));
    if (!onb_discover_argv(&s->argv, &sel, true, path) || !onb_discover_argv_valid(&s->argv, s->directory)) { notice(s, "This selection cannot be qualified from here; use the CLI"); return; }
    (void)job_start(app, s, ONB_JOB_QUALIFY);
}

static void review_open(struct native_onboarding *s) {
    const struct onb_candidate *c = selected_candidate(s);
    if (!c) { notice(s, "Select a qualified candidate to review; reported records are inspection only"); return; }
    if (!strcmp(c->status, "revoked")) { notice(s, "This identity is revoked: it can be inspected, not reviewed or enrolled"); return; }
    if (strcmp(c->status, "compatible")) { notice(s, "Only a compatible (qualified) candidate can be reviewed; Q qualifies"); return; }
    if (!s->model->export_path[0]) { notice(s, "Review needs a recorded export: Q qualifies and records one"); return; }
    copy_text(s->candidate, sizeof(s->candidate), c->candidate_id);
    open_form(s, ONB_SCREEN_REVIEW);
}

static void submit_review(struct app *app, struct native_onboarding *s) {
    struct onb_review r;
    const char *problem;
    char path[ONB_PATH];
    memset(&r, 0, sizeof(r));
    copy_text(r.project, sizeof(r.project), s->review_fields[ONB_REVIEW_PROJECT].text);
    copy_text(r.alias, sizeof(r.alias), s->review_fields[ONB_REVIEW_ALIAS].text);
    copy_text(r.package, sizeof(r.package), s->review_fields[ONB_REVIEW_PACKAGE].text);
    copy_text(r.sha256, sizeof(r.sha256), s->review_fields[ONB_REVIEW_SHA256].text);
    copy_text(r.prefix, sizeof(r.prefix), s->review_fields[ONB_REVIEW_PREFIX].text);
    if ((problem = onb_path_problem(r.project, true))) { s->focus = ONB_REVIEW_PROJECT; copy_text(s->problem, sizeof(s->problem), problem); return; }
    if (r.package[0] || r.sha256[0] || r.prefix[0]) {
        if ((problem = onb_path_problem(r.package, true))) s->focus = ONB_REVIEW_PACKAGE;
        else if (!onb_hex_digest(r.sha256)) { problem = "The package digest is its 64-character SHA-256"; s->focus = ONB_REVIEW_SHA256; }
        else if ((problem = onb_path_problem(r.prefix, true))) s->focus = ONB_REVIEW_PREFIX;
        if (problem) { copy_text(s->problem, sizeof(s->problem), problem); return; }
    }
    numbered(s, "intent", highest(s->directory, "intent") + 1, path, sizeof(path));
    if (!onb_review_argv(&s->argv, s->model->export_path, s->candidate, &r, path)) { copy_text(s->problem, sizeof(s->problem), "The review could not be composed; check the alias and paths"); return; }
    s->problem[0] = '\0';
    (void)job_start(app, s, ONB_JOB_REVIEW);
}

/* a on the intent: the confirmation names the intent, its full digest and
 * the exact command, and defaults to Cancel; the command then runs exactly
 * as displayed, or not at all. */
static void apply_confirm(struct app *app, struct native_onboarding *s) {
    const struct onb_intent *in = &s->model->intent;
    char body[ONB_PATH + 512], digest[72], path[ONB_PATH];
    size_t i, open = 0;
    if (!in->loaded || !onb_hex_digest(in->digest)) { notice(s, "No reviewed intent is loaded"); return; }
    for (i = 0; i < in->host_count; i++) if (strcmp(in->hosts[i].status, "enrolled") && strcmp(in->hosts[i].status, "revoked")) open++;
    if (!open) { notice(s, "Every reviewed host is enrolled or revoked; there is nothing to apply"); return; }
    copy_text(digest, sizeof(digest), in->digest); copy_text(path, sizeof(path), in->path);
    snprintf(body, sizeof(body), "Enroll the %zu host%s this intent reviewed?\n\nIntent digest %s\n\nRuns: hydra fleet enroll apply "
             "--input %s --confirm %s\n\nIf the intent changes before you confirm, nothing runs.",
             open, open == 1 ? "" : "s", digest, path, digest);
    if (!dialog_confirm(app, "Apply reviewed intent", body, "Apply", true)) { notice(s, "Not applied: nothing was run"); return; }
    if (!s->model->intent.loaded || strcmp(s->model->intent.digest, digest) || strcmp(s->model->intent.path, path) ||
        !onb_apply_argv(&s->argv, path, digest) || !onb_apply_argv_valid(&s->argv, path, digest)) {
        notice(s, "The intent changed during confirmation; nothing was run"); return;
    }
    (void)job_start(app, s, ONB_JOB_APPLY);
}

static void open_export(struct app *app, struct native_onboarding *s) {
    char path[ONB_PATH];
    if (prompt_text(app, "Path of a qualification export (written by hydra fleet qualify --progress): ", path, sizeof(path)) || !path[0]) return;
    if (onb_path_problem(path, true)) { notice(s, "Enter an absolute path"); return; }
    if (onb_parse_file(s->model, path) || !s->model->candidate_count) { snprintf(s->notice, sizeof(s->notice), "%.200s is not a readable qualification export", path); return; }
    copy_text(s->model->export_path, sizeof(s->model->export_path), path);
    s->selected = 0; s->scroll = 0;
    counts_notice(s, "Loaded");
}

static void filter_prompt(struct app *app, struct native_onboarding *s, char *field, size_t size, const char *prompt) {
    char text[128];
    if (prompt_text(app, prompt, text, sizeof(text))) return;
    copy_text(field, size, text);
    s->selected = 0; s->scroll = 0;
}

static void clear_filters(struct native_onboarding *s) {
    memset(&s->filter, 0, sizeof(s->filter)); s->filter.typed = -1; s->selected = 0; s->scroll = 0;
    notice(s, "Filters cleared");
}

static void show_intent(struct native_onboarding *s) {
    if (!s->model->intent.loaded) { notice(s, "No reviewed intent: r reviews a qualified candidate"); return; }
    s->screen = ONB_SCREEN_INTENT; s->scroll = 0; s->notice[0] = '\0';
}

static void reload(struct app *app, struct native_onboarding *s) {
    if (!s->directory_known) return;
    load_records(app, s);
    notice(s, "Reloaded the recorded export and enrollment status");
}

/* The filters cycle or are typed; each starts the list from its first row. */
static void filter_action(struct app *app, struct native_onboarding *s, enum key_action action) {
    switch (action) {
    case KEY_ONB_STATE: s->filter.typed = (s->filter.typed + 2) % (ONB_TYPED_COUNT + 1) - 1; break;
    case KEY_ONB_SOURCE: onb_next_source(s->model, s->filter.source, sizeof(s->filter.source)); break;
    case KEY_ONB_PLATFORM: onb_next_platform(s->model, s->filter.platform, sizeof(s->filter.platform)); break;
    case KEY_ONB_LABEL: filter_prompt(app, s, s->filter.label, sizeof(s->filter.label), "Show candidates with a label containing: "); break;
    case KEY_ONB_SEARCH: filter_prompt(app, s, s->filter.search, sizeof(s->filter.search), "Search candidates (name, target, ID): "); break;
    default: clear_filters(s); return;
    }
    s->selected = 0; s->scroll = 0;
}

static void enter(struct app *app, struct native_onboarding *s) {
    switch (s->screen) {
        case ONB_SCREEN_LIST: onb_open_detail(s); break;
        case ONB_SCREEN_SELECT:
            if (s->focus + 1 < ONB_FORM_FIELDS) s->focus++; else submit_select(app, s);
            break;
        case ONB_SCREEN_REVIEW:
            if (s->focus + 1 < ONB_FORM_FIELDS) s->focus++; else submit_review(app, s);
            break;
        case ONB_SCREEN_ERROR:
            if (!strcmp(s->model->last.code, "no_result") || !strcmp(s->model->last.code, "apply_in_progress")) status_start(app, s);
            else { s->screen = ONB_SCREEN_LIST; s->scroll = 0; }
            break;
        default: break;
    }
}

static void back(struct app *app, struct native_onboarding *s) {
    switch (s->screen) {
        case ONB_SCREEN_RUNNING:
            if (s->job.active && !s->job.cancelled) { setup_capture_cancel(&s->job); snprintf(s->notice, sizeof(s->notice), "Cancelling: %s...", s->job_label); }
            break;
        case ONB_SCREEN_LIST: close_view(app, s); break;
        default: s->screen = ONB_SCREEN_LIST; s->scroll = 0; s->problem[0] = '\0'; break;
    }
}

void native_onboarding_back(struct app *app) {
    if (native_onboarding_active(app)) back(app, app->onboarding);
}

/* Steps a position by direction, page rows at a time (one when page is 0),
 * within 0..limit. */
static size_t step_within(size_t at, size_t limit, int direction, int page) {
    int steps = page > 0 ? page : 1;
    while (steps-- > 0) {
        if (direction < 0 && at) at--;
        else if (direction > 0 && at < limit) at++;
    }
    return at;
}

/* The list's selection, a form's focused field, or a page's scroll. */
static void move(struct native_onboarding *s, int direction, int page) {
    struct onb_row rows[ONB_CANDIDATES + ONB_RECORDS];
    size_t count;
    if (s->screen == ONB_SCREEN_SELECT || s->screen == ONB_SCREEN_REVIEW) {
        s->focus = (s->focus + ONB_FORM_FIELDS + direction) % ONB_FORM_FIELDS; return;
    }
    if (s->screen != ONB_SCREEN_LIST) { s->scroll = step_within(s->scroll, s->scroll_limit, direction, page); return; }
    count = rows_of(s, rows);
    s->selected = step_within(s->selected, count ? count - 1 : 0, direction, page);
}

static bool text_screen(const struct native_onboarding *s) { return s->screen == ONB_SCREEN_SELECT || s->screen == ONB_SCREEN_REVIEW; }

/* ---- Keys (the binding table: keys.c) ---- */

enum key_chain_id native_onboarding_chain(const struct app *app) {
    switch (app->onboarding->screen) {
    case ONB_SCREEN_LIST: return KEY_CHAIN_ONBOARDING;
    case ONB_SCREEN_INTENT: return KEY_CHAIN_ONBOARDING_INTENT;
    case ONB_SCREEN_RUNNING: return KEY_CHAIN_ONBOARDING_RUNNING;
    case ONB_SCREEN_SELECT: case ONB_SCREEN_REVIEW: return KEY_CHAIN_ONBOARDING_FORM;
    default: return KEY_CHAIN_ONBOARDING_PAGE;
    }
}

/* Reviewed hosts that are neither enrolled nor revoked: what a apply runs on. */
static size_t open_hosts(const struct onb_intent *in) {
    size_t i, open = 0;
    for (i = 0; i < in->host_count; i++) if (strcmp(in->hosts[i].status, "enrolled") && strcmp(in->hosts[i].status, "revoked")) open++;
    return open;
}

/* Quitting waits for a command that runs on its own screen to finish or be
 * cancelled; elsewhere it keeps running and records its own rows. */
bool native_onboarding_applies(const struct app *app, enum key_action action, bool *applies) {
    const struct native_onboarding *s = app->onboarding;
    bool open = native_onboarding_active(app) && app->view == 6;
    if (action == KEY_QUIT && open && s->screen == ONB_SCREEN_RUNNING) { *applies = !s->job.active; return true; }
    if (action < KEY_ONB_FIRST || action > KEY_ONB_LAST) return false;
    switch (action) {
    case KEY_ONB_CONTINUE: *applies = open && s->screen == ONB_SCREEN_ERROR; break;
    case KEY_ONB_APPLY: *applies = open && s->model->intent.loaded && open_hosts(&s->model->intent) > 0; break;
    case KEY_ONB_CANCEL: *applies = open && s->job.active && !s->job.cancelled; break;
    case KEY_ONB_BACKGROUND: *applies = open && !s->job.cancelled; break;
    default: *applies = open; break;
    }
    return true;
}

/* Labels that follow the screen: Enter on a form's last field submits it,
 * Enter on an error reads the recorded status where a result is missing, and
 * Esc cancels a command that runs. */
void native_onboarding_labels(const struct app *app, struct kt_binding *rows) {
    const struct native_onboarding *s = app->onboarding;
    if (!native_onboarding_active(app) || app->view != 6) return;
    if (text_screen(s) && s->focus + 1 >= ONB_FORM_FIELDS) rows[KEY_ONB_SUBMIT].label = "run";
    if (!strcmp(s->model->last.code, "no_result") || !strcmp(s->model->last.code, "apply_in_progress"))
        rows[KEY_ONB_CONTINUE].label = "read the recorded status";
    if (s->screen == ONB_SCREEN_RUNNING) rows[KEY_BACK].label = "cancel";
}

static void list_action(struct app *app, struct native_onboarding *s, enum key_action action) {
    switch (action) {
    case KEY_ONB_OPEN: onb_open_detail(s); break;
    case KEY_ONB_FIND: open_form(s, ONB_SCREEN_SELECT); break;
    case KEY_ONB_QUALIFY: qualify_start(app, s); break;
    case KEY_ONB_REVIEW: review_open(s); break;
    case KEY_ONB_INTENT: show_intent(s); break;
    case KEY_ONB_CHANGES: s->screen = ONB_SCREEN_CHANGES; s->scroll = 0; s->notice[0] = '\0'; break;
    case KEY_ONB_EXPORT: open_export(app, s); break;
    case KEY_ONB_RELOAD: reload(app, s); break;
    case KEY_ONB_PAGE_PREVIOUS: case KEY_ONB_PAGE_NEXT: close_view(app, s); break;
    default: filter_action(app, s, action); break;
    }
}

/* Moves: the list's selection, a page's scroll or a form's field. */
static bool move_action(struct app *app, struct native_onboarding *s, enum key_action action) {
    int page = app->rows > 12 ? app->rows / 2 : 6;
    switch (action) {
    case KEY_ONB_UP: case KEY_ONB_SCROLL_UP: case KEY_ONB_FIELD_PREVIOUS: move(s, -1, 0); return true;
    case KEY_ONB_DOWN: case KEY_ONB_SCROLL_DOWN: case KEY_ONB_FIELD_NEXT: move(s, 1, 0); return true;
    case KEY_ONB_PAGE_UP: case KEY_ONB_SCROLL_PAGE_UP: move(s, -1, page); return true;
    case KEY_ONB_PAGE_DOWN: case KEY_ONB_SCROLL_PAGE_DOWN: move(s, 1, page); return true;
    default: return false;
    }
}

bool native_onboarding_perform(struct app *app, enum key_action action) {
    struct native_onboarding *s = app->onboarding;
    if (action < KEY_ONB_FIRST || action > KEY_ONB_LAST) return false;
    if (!native_onboarding_active(app) || move_action(app, s, action)) return true;
    switch (action) {
    case KEY_ONB_CONTINUE: case KEY_ONB_SUBMIT: enter(app, s); break;
    case KEY_ONB_APPLY: apply_confirm(app, s); break;
    case KEY_ONB_STATUS: status_start(app, s); break;
    case KEY_ONB_CANCEL: case KEY_ONB_FORM_BACK: back(app, s); break;
    case KEY_ONB_BACKGROUND: close_view(app, s); break;
    default: list_action(app, s, action); break;
    }
    return true;
}

/* While a form is shown its focused field takes text, pastes included;
 * Escape and the keys that are not text go to the binding table. */
bool native_onboarding_text(struct app *app, const struct kt_event *e) {
    struct native_onboarding *s = app->onboarding;
    if (!s || !s->open || app->view != 6) return false;
    if (e->type == KT_MOUSE || e->type == KT_PASTE_BEGIN || e->type == KT_PASTE_END) return true;
    if (e->type == KT_PASTE) { if (text_screen(s)) onb_form_insert(s, e); return true; }
    return text_screen(s) && e->key != KT_KEY_ESCAPE && onb_form_key(s, e);
}
