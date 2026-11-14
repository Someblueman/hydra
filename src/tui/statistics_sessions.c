#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "statistics.h"
/* The head sessions page of the statistics view: every start or resume of a
 * head, how long it lasted and how much of that time its terminal was active
 * or quiet. Active and quiet are inferred from terminal output while a control
 * centre watched, unless an adapter reported them; the page says so wherever
 * it shows them, and shows what was not observed instead of calling it quiet.
 * Unknown is "--", never zero. */

/* ---- which page ---------------------------------------------------------- */

static bool sessions_page(const struct statistics_view *v) {
    return v && v->model && v->sessions;
}

bool statistics_sessions_shown(const struct app *app) {
    return !app->fleet && sessions_page(app->statistics);
}

/* The first sample chooses the page: one without workflow runs opens on
 * sessions. Later samples and the user's own choice leave it alone. Feeds
 * older than schema 6 carry no sessions and keep the runs page. */
void statistics_sessions_default(struct statistics_view *v) {
    if (v->sessions_chosen || !v->model) return;
    v->sessions = v->model->schema_version >= 6 && !v->model->run_count;
    v->sessions_chosen = true;
}

size_t statistics_pages(const struct app *app, enum stats_page pages[3]) {
    const struct statistics_view *v = app->statistics;
    size_t n = 0;
    if (app->fleet || !v || !v->model) return 0;
    if (v->model->schema_version >= 6 || v->sessions) pages[n++] = STATS_SESSIONS;
    pages[n++] = STATS_RUNS;
    if (statistics_cohorts_recorded(v->model)) pages[n++] = STATS_COHORTS;
    return n;
}

enum stats_page statistics_page(const struct app *app) {
    if (statistics_sessions_shown(app)) return STATS_SESSIONS;
    return statistics_cohorts_shown(app) ? STATS_COHORTS : STATS_RUNS;
}

bool statistics_cohorts_shown(const struct app *app) {
    const struct statistics_view *v = app->statistics;
    return !app->fleet && v && v->model && !v->sessions && v->metric_page == STATISTICS_COHORT_PAGE &&
        statistics_cohorts_recorded(v->model);
}

/* Showing a page closes the evidence and leaves the metric pages; the
 * cohorts page is the runs page's last metric page. */
void statistics_page_show(struct statistics_view *v, enum stats_page page) {
    v->sessions = page == STATS_SESSIONS; v->sessions_chosen = true;
    v->detail = false; v->evidence_focus = false; v->step_scroll = 0;
    v->metric_page = page == STATS_COHORTS ? STATISTICS_COHORT_PAGE : 0;
}

/* ---- selection ----------------------------------------------------------- */

/* The feed lists sessions newest first; the selection follows its instance. */
void statistics_sessions_visible(struct app *app) {
    struct statistics_view *v = app->statistics;
    size_t i;
    v->session_count = 0;
    if (app->fleet || !v->model) return;
    for (i = 0; i < v->model->session_count; i++) {
        if (!hs_session_matches(v->model, i, &v->filter)) continue;
        if (!strcmp(v->model->sessions[i].instance, v->session_id)) v->session_selected = v->session_count;
        v->session_rows[v->session_count++] = i;
    }
    if (v->session_selected >= v->session_count) v->session_selected = v->session_count ? v->session_count - 1 : 0;
    if (v->session_count)
        copy_text(v->session_id, sizeof(v->session_id), v->model->sessions[v->session_rows[v->session_selected]].instance);
}

static void sessions_select(struct statistics_view *v, size_t row) {
    if (row >= v->session_count) return;
    v->session_selected = row;
    copy_text(v->session_id, sizeof(v->session_id), v->model->sessions[v->session_rows[row]].instance);
}

void statistics_sessions_move(struct app *app, int direction) {
    struct statistics_view *v = app->statistics;
    if (stats_evidence_focused(app)) return;
    if (direction > 0) sessions_select(v, v->session_selected + 1);
    else if (v->session_selected) sessions_select(v, v->session_selected - 1);
}

/* A clicked row of whichever list is shown, or a page of the strip. */
void statistics_select(struct app *app, size_t item) {
    struct statistics_view *v = app->statistics;
    if (item == STATS_HIT_SESSIONS || item == STATS_HIT_RUNS || item == STATS_HIT_COHORTS) {
        enum stats_page page = item == STATS_HIT_SESSIONS ? STATS_SESSIONS : item == STATS_HIT_RUNS ? STATS_RUNS : STATS_COHORTS;
        if (page != statistics_page(app)) statistics_page_show(v, page);
        statistics_visible(app);
        return;
    }
    if (statistics_cohorts_shown(app)) { v->step_scroll = item; return; }
    if (statistics_sessions_shown(app)) { sessions_select(v, item); return; }
    v->selected = item; v->selected_id[0] = '\0';
    statistics_visible(app);
}


/* ---- text ---------------------------------------------------------------- */

/* A session a resume took over from is "replaced": the head went on in a
 * new instance. */
static const char *state_text(const struct hs_session *s) {
    if (s->state == HS_SESSION_OPEN) return "open";
    return s->state == HS_SESSION_ENDED ? "ended" : "replaced";
}

static bool session_failed(const struct hs_session *s) {
    return !strcmp(s->outcome, "failed") || !strcmp(s->outcome, "blocked");
}

static const char *state_glyph(const struct app *app, const struct hs_session *s) {
    if (s->state == HS_SESSION_SUPERSEDED) return app->ascii ? "r" : "\xe2\x86\xbb";
    if (s->state == HS_SESSION_OPEN) return app->ascii ? "*" : "\xe2\x97\x8f";
    if (session_failed(s)) return app->ascii ? "!" : "\xe2\x96\xb2";
    return app->ascii ? "+" : "\xe2\x9c\x93";
}

static kt_color state_color(const struct hs_session *s) {
    if (s->state == HS_SESSION_SUPERSEDED) return KT_COLOR_MUTED;
    return session_failed(s) ? KT_COLOR_WARNING : KT_COLOR_SUCCESS;
}

static uint64_t observed_seconds(const struct hs_session *s) {
    return s->active_known && s->quiet_known ? s->active + s->quiet : 0;
}

static void short_date(uint64_t epoch, bool known, char *out, size_t size) {
    time_t value = (time_t)epoch;
    struct tm utc;
    if (!known || !gmtime_r(&value, &utc) || !strftime(out, size, "%m-%d %H:%M", &utc)) copy_text(out, size, "--");
}

/* ---- tiles, notes and charts --------------------------------------------- */

static void session_tiles(struct kt_canvas *c, int y, int width, const struct hs_session_summary *s) {
    char count[24], open[48], active[32], quiet[32], unobserved[32], share[16], quiet_note[64];
    uint64_t observed = s->active_sum + s->quiet_sum;
    struct stats_tile tiles[4] = {
        {"Sessions", count, open, KT_BASE}, {"Active", active, "inferred from output", KT_SUCCESS},
        {"Quiet", quiet, quiet_note, KT_WARNING}, {"Unobserved", unobserved, "no Hydra open", KT_MUTED}};
    (void)snprintf(count, sizeof(count), "%zu", s->sessions);
    (void)snprintf(open, sizeof(open), "%zu open now", s->open);
    stats_seconds(s->active_sum, s->activity_known > 0, active, sizeof(active));
    stats_seconds(s->quiet_sum, s->activity_known > 0, quiet, sizeof(quiet));
    stats_seconds(s->unobserved_sum, s->coverage_known > 0, unobserved, sizeof(unobserved));
    stats_percent(s->quiet_sum, observed, share, sizeof(share));
    (void)snprintf(quiet_note, sizeof(quiet_note), width >= 128 ? "inferred, %s of observed" : "inferred, %s", share);
    stats_tiles(c, y, width, tiles);
}

/* How much of the session time anyone observed, and how it is known. */
static void coverage_text(const struct hs_session_summary *s, bool wide, char *out, size_t size) {
    char share[16], total[32], reported[32];
    uint64_t observed = s->active_sum + s->quiet_sum;
    if (!s->coverage_known || !observed) {
        copy_text(out, size, wide ? "No session time observed yet: activity is recorded while this control centre is open." :
                  "No session time observed yet; recorded while this is open.");
        return;
    }
    stats_percent(observed, observed + s->unobserved_sum, share, sizeof(share));
    stats_seconds(observed + s->unobserved_sum, true, total, sizeof(total));
    stats_seconds(s->reported_sum, true, reported, sizeof(reported));
    if (s->reported_sum) (void)snprintf(out, size, wide ? "Observed %s of %s session time; %s reported by adapters, the rest inferred from terminal output." :
        "Observed %s of %s; %s reported, the rest inferred from output.", share, total, reported);
    else (void)snprintf(out, size, wide ? "Observed %s of %s session time; active and quiet are inferred from terminal output, not reported." :
        "Observed %s of %s; active and quiet inferred from output.", share, total);
}

static void session_notes(struct kt_canvas *c, const struct stats_frame *f, const struct hs_session_summary *s) {
    char text[256];
    coverage_text(s, f->width >= 110, text, sizeof(text));
    stats_note(c, f->top + 4, f->width, text);
    (void)snprintf(text, sizeof(text), "Known: Length %zu/%zu, Observed %zu/%zu, %zu undated, %zu outside range.%s",
                   s->length_known, s->sessions, s->activity_known, s->sessions, s->undated, s->excluded_undated,
                   f->width >= 110 ? " Tokens and cost are not measured for sessions." : "");
    stats_note(c, f->top + 5, f->width, text);
}

static void session_summary(const struct app *app, struct kt_canvas *c, int y, int width, const struct hs_session_summary *s) {
    char text[256], active[32], quiet[32];
    const char *sep = dot(app);
    stats_seconds(s->active_sum, s->activity_known > 0, active, sizeof(active));
    stats_seconds(s->quiet_sum, s->activity_known > 0, quiet, sizeof(quiet));
    (void)snprintf(text, sizeof(text), "%zu sessions%s%zu open%sactive %s%squiet %s%sinferred", s->sessions, sep, s->open,
                   sep, active, sep, quiet, sep);
    (void)kt_text_aligned(c, (struct kt_rect){1, y, width, 1}, text, KT_ALIGN_LEFT, KT_BASE);
}

#define AGENTS 6
struct agent_row { const char *name; size_t sessions, known; uint64_t active; };

/* Sessions and inferred active time per agent over the rows shown. */
static size_t agents_of(const struct statistics_view *v, struct agent_row *rows) {
    size_t i, j, n = 0;
    for (i = 0; i < v->session_count; i++) {
        const struct hs_session *s = &v->model->sessions[v->session_rows[i]];
        const char *name = s->profile[0] && strcmp(s->profile, "-") ? s->profile : "not recorded";
        for (j = 0; j < n && strcmp(rows[j].name, name); j++) {}
        if (j == n && n == AGENTS) continue;
        if (j == n) { memset(&rows[n], 0, sizeof(rows[n])); rows[n++].name = name; }
        rows[j].sessions++;
        if (s->active_known) { rows[j].known++; rows[j].active += s->active; }
    }
    return n;
}

static void by_agent(const struct statistics_view *v, struct kt_canvas *c, int x, int y, int width) {
    struct agent_row rows[AGENTS];
    char active[32];
    size_t i, n = agents_of(v, rows);
    kt_text(c, (struct kt_rect){x, y, width, 1}, "By agent", KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_BOLD));
    for (i = 0; i < n; i++) {
        stats_seconds(rows[i].active, rows[i].known > 0, active, sizeof(active));
        dashboard_text(c, x, y + 1 + (int)i, 12, KT_BASE, "%s", rows[i].name);
        dashboard_text(c, x + 13, y + 1 + (int)i, width - 13, KT_MUTED, "%2zu session%s   active %s", rows[i].sessions,
                       rows[i].sessions == 1 ? " " : "s", active);
    }
}

static void session_charts(const struct statistics_view *v, struct kt_canvas *c, int y, int width,
                           const struct hs_session_summary *s) {
    size_t i, maximum = 1;
    int chart = width / 2 - 4 < 52 ? width / 2 - 4 : 52;
    char label[8];
    for (i = 0; i < 7; i++) if (s->daily[i] > maximum) maximum = s->daily[i];
    kt_text(c, (struct kt_rect){1, y, chart, 1}, "Sessions started, last 7 days", KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_BOLD));
    for (i = 0; i < 7; i++) {
        (void)snprintf(label, sizeof(label), "-%zud", 6 - i);
        stats_bar(c, 1, y + 1 + (int)i, chart, label, s->daily[i], (double)s->daily[i] / (double)maximum);
    }
    by_agent(v, c, chart + 4, y, width - chart - 4);
}

/* ---- the list ------------------------------------------------------------ */

enum { SESSION_HEAD, SESSION_AGENT, SESSION_STATE, SESSION_STARTED, SESSION_LENGTH, SESSION_ACTIVE, SESSION_QUIET,
       SESSION_OUTCOME };
static const struct stats_column session_columns[] = {
    {"HEAD", 20, 0}, {"AGENT", 9, 50}, {"STATE", 11, 0}, {"STARTED", 12, 60}, {"LENGTH", 7, 44},
    {"ACTIVE", 7, 70}, {"QUIET", 8, 88}, {"OUTCOME", 10, 110}};

static void session_time_cell(const struct statistics_view *v, const struct hs_session *s, int column, char *out, size_t size) {
    uint64_t seconds;
    if (column == SESSION_STARTED) short_date(s->started, s->started_known, out, size);
    else if (column == SESSION_ACTIVE) stats_seconds(s->active, s->active_known, out, size);
    else if (column == SESSION_QUIET) stats_seconds(s->quiet, s->quiet_known, out, size);
    else if (hs_session_length(v->model, s, &seconds)) format_duration((long long)seconds, out, size);
    else copy_text(out, size, "--");
}

static void session_cell(void *context, size_t row, int column, char *out, size_t size, kt_color *color) {
    struct app *app = context;
    const struct statistics_view *v = app->statistics;
    const struct hs_session *s = &v->model->sessions[v->session_rows[row]];
    if (column != SESSION_HEAD) *color = KT_COLOR_MUTED;
    switch (column) {
    case SESSION_HEAD: copy_text(out, size, s->branch); break;
    case SESSION_AGENT: copy_text(out, size, s->profile[0] ? s->profile : "-"); break;
    case SESSION_STATE: (void)snprintf(out, size, "%s %s", state_glyph(app, s), state_text(s)); *color = state_color(s); break;
    case SESSION_OUTCOME: copy_text(out, size, s->headless ? "headless" : s->outcome[0] ? s->outcome : "--"); break;
    default: session_time_cell(v, s, column, out, size); break;
    }
}

static void session_list(struct app *app, struct kt_canvas *c, struct kt_rect area) {
    const struct statistics_view *v = app->statistics;
    struct stats_list l;
    if (!v->session_count) {
        stats_empty(app, c, area, "Head sessions", v->model->session_count, "No sessions recorded yet",
                    "A session is recorded each time a head is started or resumed. While a control centre is open, it "
                    "also records when the head's terminal is active or quiet. Start work from the Work tab.");
        return;
    }
    memset(&l, 0, sizeof(l));
    l.title = "Head sessions"; l.order = "newest first"; l.columns = session_columns;
    l.column_count = sizeof(session_columns) / sizeof(session_columns[0]);
    l.count = v->session_count; l.selected = v->session_selected; l.cell = session_cell; l.context = app;
    l.focused = !stats_evidence_focused(app);
    stats_list_view(app, c, area, &l);
}

/* ---- evidence ------------------------------------------------------------ */

/* What the selected session's times mean and where they come from. */
static void session_sources(struct kt_canvas *c, struct kt_rect r, const struct hs_session *s) {
    char text[384], reported[32];
    const char *why = "Nobody observed this session: activity is recorded while a control centre is open.";
    if (s->headless) why = "A headless head has no terminal to observe; its owner reports its status.";
    else if (!s->unobserved_known) why = "The observer history does not reach back to this session: its times are unknown.";
    else if (observed_seconds(s)) why = "Active and quiet are inferred from terminal output (tmux) while a control centre watched; they do not say what the agent was waiting for.";
    copy_text(text, sizeof(text), why);
    stats_seconds(s->reported, s->reported_known, reported, sizeof(reported));
    if (s->reported_known && s->reported) text_append(text, sizeof(text), "\nReported by an adapter (exact): %s", reported);
    text_append(text, sizeof(text), "\nTokens and cost: not measured for sessions.");
    (void)kt_paragraph(c, r, text, KT_MUTED);
}

/* A label in a fixed column, muted, and its value. */
static void evidence_pair(struct kt_canvas *c, int x, int y, int width, const char *label, const char *value) {
    dashboard_text(c, x, y, 11, KT_MUTED, "%s", label);
    dashboard_text(c, x + 11, y, width - 11, KT_BASE, "%s", value);
}

static void session_lines(const struct statistics_view *v, const struct hs_session *s, struct kt_canvas *c, int x, int y, int width) {
    char started[40], ended[40], length[40] = "--", active[32], quiet[32], unobserved[32], share[16], pair[96];
    uint64_t seconds;
    stats_time(s->started, s->started_known, started, sizeof(started));
    stats_time(s->ended, s->ended_known, ended, sizeof(ended));
    if (hs_session_length(v->model, s, &seconds)) format_duration((long long)seconds, length, sizeof(length));
    stats_seconds(s->active, s->active_known, active, sizeof(active));
    stats_seconds(s->quiet, s->quiet_known, quiet, sizeof(quiet));
    stats_seconds(s->unobserved, s->unobserved_known, unobserved, sizeof(unobserved));
    stats_percent(observed_seconds(s), s->unobserved_known ? observed_seconds(s) + s->unobserved : 0, share, sizeof(share));
    dashboard_text(c, x, y, width, KT_STRONG, "%s / %s", s->branch, state_text(s));
    dashboard_text(c, x, y + 1, width, KT_MUTED, "%s", s->head);
    dashboard_text(c, x, y + 2, width, KT_MUTED, "%s", s->instance);
    evidence_pair(c, x, y + 4, width, "Agent", s->profile[0] ? s->profile : "not recorded");
    evidence_pair(c, x, y + 5, width, "Terminal", s->headless ? "none (headless)" : "interactive");
    evidence_pair(c, x, y + 6, width, "Outcome", s->outcome[0] ? s->outcome : "not declared");
    evidence_pair(c, x, y + 7, width, "Started", started);
    evidence_pair(c, x, y + 8, width, "Ended", s->state == HS_SESSION_OPEN ? "still open" : ended);
    if (s->state == HS_SESSION_OPEN) text_append(length, sizeof(length), " so far");
    evidence_pair(c, x, y + 9, width, "Length", length);
    (void)snprintf(pair, sizeof(pair), "%s   Quiet %s", active, quiet);
    evidence_pair(c, x, y + 10, width, "Active", pair);
    (void)snprintf(pair, sizeof(pair), "%s   Observed %s", unobserved, share);
    evidence_pair(c, x, y + 11, width, "Unobserved", pair);
}

static void session_evidence(struct app *app, struct kt_canvas *c, struct kt_rect r) {
    const struct statistics_view *v = app->statistics;
    const struct hs_session *s;
    int x = r.x + 2, width = r.width - 3;
    stats_rule(c, (struct kt_rect){r.x, r.y, r.width, 1}, "Session evidence", "one head instance", stats_evidence_focused(app));
    if (!v->session_count) { dashboard_text(c, x, r.y + 1, width, KT_WARNING, "Selected session no longer matches"); return; }
    s = &v->model->sessions[v->session_rows[v->session_selected]];
    session_lines(v, s, c, x, r.y + 1, width);
    if (r.height > 15) session_sources(c, (struct kt_rect){x, r.y + 14, width, r.height - 14}, s);
}

/* ---- the page ------------------------------------------------------------ */

void stats_sessions_page(struct app *app, struct kt_canvas *c) {
    struct statistics_view *v = app->statistics;
    struct hs_session_summary summary;
    struct stats_frame f;
    struct kt_rect list, evidence;
    int row;
    hs_session_summarize(v->model, &v->filter, &summary);
    stats_layout(app, c, 2, summary.sessions > 0 && !v->detail, &f);
    stats_strip(app, c, &f);
    if (!v->model->session_count) f.tiles = f.summary = f.charts = false;
    row = stats_list_row(&f, 2);
    if (f.tiles) { session_tiles(c, f.top, f.width, &summary); session_notes(c, &f, &summary); }
    else if (f.summary) session_summary(app, c, f.top, f.width, &summary);
    if (f.charts) session_charts(v, c, row - 9, f.width, &summary);
    stats_areas(app, c, &f, row, &list, &evidence);
    session_list(app, c, list);
    if (v->detail) session_evidence(app, c, evidence);
}
