#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
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
 * older than schema 5 carry no sessions and keep the runs page. */
void statistics_sessions_default(struct statistics_view *v) {
    if (v->sessions_chosen || !v->model) return;
    v->sessions = v->model->schema_version >= 5 && !v->model->run_count;
    v->sessions_chosen = true;
}

static void sessions_toggle(struct statistics_view *v) {
    v->sessions = !v->sessions; v->sessions_chosen = true;
    v->detail = false; v->metric_page = 0; v->step_scroll = 0;
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
    v->session_id[0] = '\0';
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
    if (v->detail) return;
    if (direction > 0) sessions_select(v, v->session_selected + 1);
    else if (v->session_selected) sessions_select(v, v->session_selected - 1);
}

/* A clicked row of whichever list is shown. */
void statistics_select(struct app *app, size_t item) {
    struct statistics_view *v = app->statistics;
    if (statistics_sessions_shown(app)) { sessions_select(v, item); return; }
    v->selected = item; v->selected_id[0] = '\0';
    statistics_visible(app);
}

/* ---- keys ---------------------------------------------------------------- */

static void sessions_find(struct app *app) {
    struct statistics_view *v = app->statistics;
    char query[80];
    if (prompt_text(app, "Find branch, agent or head: ", query, sizeof(query)) != 0) return;
    copy_text(v->filter.query, sizeof(v->filter.query), query);
    v->detail = false;
}

static bool sessions_filter_key(struct statistics_view *v, char key) {
    if (key == 'T') v->filter.days = v->filter.days == 0 ? 1 : v->filter.days == 1 ? 7 : 0;
    else if (key == '!') v->filter.attention = !v->filter.attention;
    else if (key == '0') memset(&v->filter, 0, sizeof(v->filter));
    else return false;
    v->detail = false;
    return true;
}

static bool sessions_key(struct app *app, char key) {
    struct statistics_view *v = app->statistics;
    app->notice[0] = '\0';
    if (key == 'j' || key == 'k') statistics_sessions_move(app, key == 'j' ? 1 : -1);
    else if (sessions_filter_key(v, key)) { }
    else if (key == '/') sessions_find(app);
    else if (key == '\r' || key == '\n') v->detail = !v->detail && v->session_count;
    else if (key == 'r') statistics_request(app);
    else if (strchr("Mg[]", key)) copy_text(app->notice, sizeof(app->notice), "That key is for workflow runs; s shows them");
    else if (strchr("pac AxGd", key)) copy_text(app->notice, sizeof(app->notice), "Statistics is read-only; Enter evidence / s workflow runs / D back");
    else return false;
    statistics_visible(app);
    return true;
}

/* s switches between workflow runs and head sessions; every other key goes
 * to the page that is shown. */
bool statistics_key(struct app *app, char key) {
    struct statistics_view *v = app->statistics;
    if (!v) return false;
    if (key == 's' && !app->fleet && v->model) {
        sessions_toggle(v);
        app->notice[0] = '\0';
        statistics_visible(app);
        return true;
    }
    return statistics_sessions_shown(app) ? sessions_key(app, key) : statistics_runs_key(app, key);
}

/* ---- text ---------------------------------------------------------------- */

static void seconds_text(uint64_t seconds, bool known, char *out, size_t size) {
    if (known) format_duration((long long)seconds, out, size);
    else copy_text(out, size, "--");
}

static void percent_text(uint64_t part, uint64_t whole, char *out, size_t size) {
    if (whole) (void)snprintf(out, size, "%.0f%%", (double)part * 100.0 / (double)whole);
    else copy_text(out, size, "--");
}

static void date_text(uint64_t epoch, bool known, char *out, size_t size) {
    time_t value = (time_t)epoch;
    struct tm utc;
    if (!known || !gmtime_r(&value, &utc) || !strftime(out, size, "%m-%d %H:%M UTC", &utc))
        copy_text(out, size, "unknown date");
}

/* A session a resume took over from is "replaced": the head went on in a
 * new instance. */
static const char *state_text(const struct hs_session *s) {
    if (s->state == HS_SESSION_OPEN) return "open";
    return s->state == HS_SESSION_ENDED ? "ended" : "replaced";
}

static const char *range_text(const struct statistics_view *v) {
    return v->filter.days == 1 ? "24 hours" : v->filter.days == 7 ? "7 days" : "all recorded";
}

static uint64_t observed_seconds(const struct hs_session *s) {
    return s->active_known && s->quiet_known ? s->active + s->quiet : 0;
}

/* The runs page when the sample also has sessions: s must stay visible. */
/* ---- drawing ------------------------------------------------------------- */

static void text_card(struct kt_canvas *c, int x, int width, const char *title, const char *value,
                      const char *caption, struct kt_style tone) {
    pane_rule(c, (struct kt_rect){x, 0, width, 5}, title);
    dashboard_text(c, x + 2, 1, width - 4, tone, "%s", value);
    dashboard_text(c, x + 2, 3, width - 4, KT_BASE, "%s", caption);
}

static void sessions_cards(struct kt_canvas *c, struct kt_rect r, const struct hs_session_summary *s) {
    struct kt_canvas view;
    char active[32], quiet[32], share[16], caption[64];
    int card = r.width / 4;
    bool roomy = card >= 26;
    uint64_t observed = s->active_sum + s->quiet_sum;
    (void)kt_canvas_view(&view, c, r);
    seconds_text(s->active_sum, s->activity_known > 0, active, sizeof(active));
    seconds_text(s->quiet_sum, s->activity_known > 0, quiet, sizeof(quiet));
    percent_text(s->quiet_sum, observed, share, sizeof(share));
    (void)snprintf(caption, sizeof(caption), roomy ? "inferred, %s of observed" : "inferred, %s", share);
    dashboard_card(&view, 0, card - 1, "SESSIONS", s->sessions, "in this scope", KT_STRONG);
    dashboard_card(&view, card, card - 1, "OPEN NOW", s->open, "not yet ended", KT_SUCCESS);
    text_card(&view, 2 * card, card - 1, "ACTIVE", active, roomy ? "inferred from output" : "inferred", KT_STRONG);
    text_card(&view, 3 * card, r.width - 3 * card, "QUIET", quiet, caption, KT_STRONG);
}

/* How much of the session time anyone observed, and how it is known. */
static void coverage_line(struct kt_canvas *c, int x, int y, int width, const struct hs_session_summary *s) {
    char share[16], total[32], reported[32];
    uint64_t observed = s->active_sum + s->quiet_sum;
    if (!s->sessions) return;
    bool wide = width >= 96;
    if (!s->coverage_known || !observed) {
        dashboard_text(c, x, y, width, KT_MUTED, wide ? "No session time observed yet: activity is recorded while this control centre is open" :
            "No session time observed yet; recorded while this is open");
        return;
    }
    percent_text(observed, observed + s->unobserved_sum, share, sizeof(share));
    seconds_text(observed + s->unobserved_sum, true, total, sizeof(total));
    seconds_text(s->reported_sum, true, reported, sizeof(reported));
    if (s->reported_sum) dashboard_text(c, x, y, width, KT_MUTED, wide ? "Observed %s of %s session time; %s reported by adapters, the rest inferred from terminal output" :
        "Observed %s of %s; %s reported, the rest inferred from output", share, total, reported);
    else dashboard_text(c, x, y, width, KT_MUTED, wide ? "Observed %s of %s session time; active and quiet are inferred from terminal output, not reported" :
        "Observed %s of %s; active and quiet inferred from output", share, total);
}

static void time_bar(struct kt_canvas *c, struct kt_rect r, int row, const char *label, uint64_t value, uint64_t whole, bool known) {
    char text[32];
    int bar = r.width - 30;
    seconds_text(value, known, text, sizeof(text));
    dashboard_text(c, r.x + 2, row, r.width - 4, KT_BASE, "%-11s %9s", label, text);
    if (bar > 1 && known) kt_bar(c, (struct kt_rect){r.x + 26, row, bar, 1}, value, whole ? whole : 1, KT_BASE);
}

static void sessions_time(struct kt_canvas *c, struct kt_rect r, const struct hs_session_summary *s) {
    uint64_t whole = s->active_sum + s->quiet_sum + s->unobserved_sum;
    char reported[32];
    pane_rule(c, r, "SESSION TIME / inferred while observed");
    time_bar(c, r, r.y + 2, "Active", s->active_sum, whole, s->activity_known > 0);
    time_bar(c, r, r.y + 3, "Quiet", s->quiet_sum, whole, s->activity_known > 0);
    time_bar(c, r, r.y + 4, "Unobserved", s->unobserved_sum, whole, s->coverage_known > 0);
    seconds_text(s->reported_sum, s->coverage_known > 0, reported, sizeof(reported));
    dashboard_text(c, r.x + 2, r.y + 6, r.width - 4, KT_MUTED, "Reported by adapters (exact): %s", reported);
    dashboard_text(c, r.x + 2, r.y + 7, r.width - 4, KT_MUTED, "Measured in %zu of %zu sessions", s->coverage_known, s->sessions);
}

static void sessions_trend(struct kt_canvas *c, struct kt_rect r, const struct hs_session_summary *s) {
    size_t i, maximum = 1;
    pane_rule(c, r, "SESSIONS STARTED / 7 x 24h");
    for (i = 0; i < 7; i++) if (s->daily[i] > maximum) maximum = s->daily[i];
    for (i = 0; i < 7 && (int)i < r.height - 3; i++) {
        int row = r.y + 2 + (int)i;
        dashboard_text(c, r.x + 2, row, r.width - 4, KT_BORDER, "-%zud %3zu", 6 - i, s->daily[i]);
        kt_bar(c, (struct kt_rect){r.x + 12, row, r.width - 15, 1}, s->daily[i], maximum, KT_BORDER);
    }
}

/* One session, columns from the right: when wide its active and quiet time,
 * then its length, its start when there is room, state, agent and branch. */
static void session_row(const struct statistics_view *v, struct kt_canvas *c, struct kt_rect r, int row, size_t i) {
    const struct hs_session *s = &v->model->sessions[v->session_rows[i]];
    char date[40], length[32] = "--", active[32], quiet[32];
    struct kt_style tone = i == v->session_selected ? KT_SELECTED : KT_BASE;
    int right = r.x + r.width - 2;
    uint64_t seconds;
    date_text(s->started, s->started_known, date, sizeof(date));
    if (hs_session_length(v->model, s, &seconds)) format_duration((long long)seconds, length, sizeof(length));
    seconds_text(s->active, s->active_known, active, sizeof(active));
    seconds_text(s->quiet, s->quiet_known, quiet, sizeof(quiet));
    if (r.width > 100) {
        right -= 32;
        dashboard_text(c, right, row, 32, tone, "active %-8s  quiet %-8s", active, quiet);
    }
    right -= 9;
    dashboard_text(c, right, row, 9, tone, "%-9s", length);
    if (r.width > 70) { right -= 17; dashboard_text(c, right, row, 17, tone, "%-17s", date); }
    right -= 9;
    dashboard_text(c, right, row, 9, tone, "%-9s", state_text(s));
    right -= 9;
    dashboard_text(c, right, row, 9, tone, "%-9.8s", s->profile[0] ? s->profile : "-");
    dashboard_text(c, r.x + 2, row, right - r.x - 2, tone, "%c %-*s", i == v->session_selected ? '>' : ' ', right - r.x - 4, s->branch);
}

static void sessions_hit(struct app *app, struct kt_rect r, size_t item) {
    size_t n = app->hit_count;
    if (n == MAX_HEADS) return;
    app->hit_rows[n] = r.y + 1; app->hit_bottom[n] = r.y + r.height;
    app->hit_left[n] = r.x + 1; app->hit_right[n] = r.x + r.width;
    app->hit_items[n] = item; app->hit_count++;
}

/* Says why the list is empty: nothing recorded, or the filters hide it. */
static void sessions_empty(struct kt_canvas *c, struct kt_rect r, const struct statistics_view *v) {
    int row = r.y + 2;
    if (v->model->session_count) {
        dashboard_text(c, r.x + 2, row, r.width - 4, KT_WARNING, "No sessions match: %zu recorded, all filtered out", v->model->session_count);
        dashboard_text(c, r.x + 2, row + 1, r.width - 4, KT_BORDER, "0 reset filters / / search / T range");
        return;
    }
    dashboard_text(c, r.x + 2, row, r.width - 4, KT_STRONG, "No head sessions recorded yet");
    if (r.height < 10) return;
    dashboard_text(c, r.x + 2, row + 2, r.width - 4, KT_BASE, "A session is recorded each time a head is started or resumed. While a");
    dashboard_text(c, r.x + 2, row + 3, r.width - 4, KT_BASE, "control centre is open, it also records when the head's terminal is");
    dashboard_text(c, r.x + 2, row + 4, r.width - 4, KT_BASE, "active or quiet.");
    dashboard_text(c, r.x + 2, row + 6, r.width - 4, KT_MUTED, "Start work from the Work tab; r retries the sample");
}

static void sessions_table(struct app *app, struct kt_canvas *c, struct kt_rect r) {
    struct statistics_view *v = app->statistics;
    size_t i, start;
    int available = r.height - 4, row = r.y + 2;
    pane_rule(c, r, "HEAD SESSIONS / newest start first");
    if (!v->session_count) { sessions_empty(c, r, v); return; }
    if (available < 1) return;
    start = v->session_selected >= (size_t)available ? v->session_selected - (size_t)available + 1 : 0;
    for (i = start; i < v->session_count && row < r.y + r.height - 2; i++, row++) {
        session_row(v, c, r, row, i);
        sessions_hit(app, (struct kt_rect){r.x + 1, row, r.width - 2, 1}, i);
    }
    dashboard_text(c, r.x + 2, r.y + r.height - 2, r.width - 4, KT_BORDER, "%zu/%zu / Enter evidence / -- unknown",
        v->session_selected + 1, v->session_count);
}

/* What the selected session's times mean and where they come from. */
static void session_sources(struct kt_canvas *c, struct kt_rect r, const struct hs_session *s) {
    char reported[32];
    const char *text = "Nobody observed this session: activity is recorded while a control centre is open.";
    seconds_text(s->reported, s->reported_known, reported, sizeof(reported));
    if (s->headless) text = "A headless head has no terminal to observe; its owner reports its status.";
    else if (!s->unobserved_known) text = "The observer history does not reach back to this session: its times are unknown.";
    else if (observed_seconds(s)) text = "Active and quiet are inferred from terminal output (tmux) while a control centre watched; they do not say what the agent was waiting for.";
    r.y += statistics_wrap(c, r.x, r.y, r.width, r.height - 2, KT_MUTED, text);
    if (s->reported_known && s->reported) dashboard_text(c, r.x, r.y++, r.width, KT_MUTED, "Reported by an adapter (exact): %s", reported);
    dashboard_text(c, r.x, r.y, r.width, KT_MUTED, "Tokens and cost: not measured for sessions.");
}

static void session_detail(struct app *app, struct kt_canvas *c, struct kt_rect r) {
    struct statistics_view *v = app->statistics;
    const struct hs_session *s;
    char started[40], ended[40], length[32] = "--", active[32], quiet[32], unobserved[32], share[16];
    uint64_t seconds;
    int x = r.x + 2, width = r.width - 4;
    pane_rule(c, r, "SESSION EVIDENCE / one head instance");
    if (!v->session_count) { dashboard_text(c, x, r.y + 2, width, KT_WARNING, "Selected session no longer matches"); return; }
    s = &v->model->sessions[v->session_rows[v->session_selected]];
    date_text(s->started, s->started_known, started, sizeof(started));
    date_text(s->ended, s->ended_known, ended, sizeof(ended));
    if (hs_session_length(v->model, s, &seconds)) format_duration((long long)seconds, length, sizeof(length));
    seconds_text(s->active, s->active_known, active, sizeof(active));
    seconds_text(s->quiet, s->quiet_known, quiet, sizeof(quiet));
    seconds_text(s->unobserved, s->unobserved_known, unobserved, sizeof(unobserved));
    percent_text(observed_seconds(s), s->unobserved_known ? observed_seconds(s) + s->unobserved : 0, share, sizeof(share));
    dashboard_text(c, x, r.y + 1, width, KT_STRONG, "%s / %s", s->branch, state_text(s));
    dashboard_text(c, x, r.y + 2, width, KT_BORDER, "%s / %s", s->head, s->instance);
    dashboard_text(c, x, r.y + 4, width, KT_BASE, "Agent %s, %s terminal, outcome %s", s->profile[0] ? s->profile : "not recorded",
        s->headless ? "no" : "interactive", s->outcome[0] ? s->outcome : "not declared");
    if (s->state == HS_SESSION_OPEN) dashboard_text(c, x, r.y + 5, width, KT_BASE, "Started %s, still open, length so far %s", started, length);
    else dashboard_text(c, x, r.y + 5, width, KT_BASE, "Started %s, ended %s, length %s", started, ended, length);
    dashboard_text(c, x, r.y + 7, width, KT_BASE, "Active %s   Quiet %s   Unobserved %s   Observed %s", active, quiet, unobserved, share);
    if (r.height > 13) session_sources(c, (struct kt_rect){x, r.y + 9, width, r.height - 11}, s);
    dashboard_text(c, x, r.y + r.height - 2, width, KT_BORDER, "Esc statistics / -- unknown");
}

static void sessions_scope(struct app *app, struct kt_canvas *c, int width, int height, const struct hs_session_summary *s) {
    const struct statistics_view *v = app->statistics;
    char updated[40];
    pane_rule(c, (struct kt_rect){0, 2, width - 1, height - 4}, "SCOPE");
    dashboard_text(c, 2, 4, width - 5, KT_STRONG, "%s", range_text(v));
    dashboard_text(c, 2, 5, width - 5, KT_MUTED, "T changes the range");
    dashboard_text(c, 2, 7, width - 5, KT_BASE, "Head sessions");
    dashboard_text(c, 2, 8, width - 5, KT_MUTED, "s shows runs (%zu)", v->model->run_count);
    dashboard_text(c, 2, 10, width - 5, KT_BASE, "%zu undated", s->undated);
    dashboard_text(c, 2, 11, width - 5, KT_BASE, "%zu outside range", s->excluded_undated);
    dashboard_text(c, 2, 13, width - 5, KT_BASE, "Length %zu/%zu", s->length_known, s->sessions);
    dashboard_text(c, 2, 14, width - 5, KT_BASE, "Observed %zu/%zu", s->activity_known, s->sessions);
    if (height > 23) {
        int row = 16 + statistics_wrap(c, 2, 16, width - 4, height - 24, KT_MUTED,
            "Active and quiet: inferred from terminal output while a control centre watches. Tokens and cost: not measured for sessions.");
        if (row + 1 < height - 6) dashboard_text(c, 2, row + 1, width - 4, KT_MUTED, "0 resets filters");
    }
    date_text(v->model->observed, true, updated, sizeof(updated));
    if (height >= 25) dashboard_text(c, 2, height - 5, 20, KT_MUTED, "%s", updated);
}

static void sessions_compact(struct app *app, struct kt_canvas *c, int width, int height, const struct hs_session_summary *s) {
    struct statistics_view *v = app->statistics;
    char active[32], quiet[32], share[16], length[32] = "--";
    uint64_t observed = s->active_sum + s->quiet_sum, seconds;
    seconds_text(s->active_sum, s->activity_known > 0, active, sizeof(active));
    seconds_text(s->quiet_sum, s->activity_known > 0, quiet, sizeof(quiet));
    percent_text(observed, s->coverage_known ? observed + s->unobserved_sum : 0, share, sizeof(share));
    dashboard_text(c, 1, 2, width - 2, KT_STRONG, "%zu sessions / %zu open", s->sessions, s->open);
    dashboard_text(c, 1, 3, width - 2, KT_BASE, "Active %s / quiet %s / observed %s", active, quiet, share);
    if (v->session_count) {
        const struct hs_session *one = &v->model->sessions[v->session_rows[v->session_selected]];
        if (hs_session_length(v->model, one, &seconds)) format_duration((long long)seconds, length, sizeof(length));
        seconds_text(one->active, one->active_known, active, sizeof(active));
        seconds_text(one->quiet, one->quiet_known, quiet, sizeof(quiet));
        dashboard_text(c, 1, 4, width - 2, KT_SELECTED, "%zu/%zu %s", v->session_selected + 1, v->session_count, one->branch);
        if (v->detail) dashboard_text(c, 1, 5, width - 2, KT_BASE, "active %s / quiet %s / %s", active, quiet, length);
        else dashboard_text(c, 1, 5, width - 2, KT_BASE, "%s / %s / Enter evidence", state_text(one), length);
    } else dashboard_text(c, 1, 4, width - 2, KT_WARNING, v->model->session_count ? "No matching sessions / 0 reset filters" : "No head sessions recorded yet");
    if (height > 9) dashboard_text(c, 1, 6, width - 2, KT_BORDER, "Inferred from terminal output / unknown stays --");
}

static void sessions_body(struct app *app, struct kt_canvas *c, int width, int height, const struct hs_session_summary *s) {
    struct statistics_view *v = app->statistics;
    int left = width >= 110 ? 25 : 0, body = width - left, chart = height >= 32 && s->sessions && !v->detail ? 10 : 0;
    if (height < 18 || width < 65) { sessions_compact(app, c, width, height, s); return; }
    if (left) sessions_scope(app, c, left, height, s);
    sessions_cards(c, (struct kt_rect){left, 2, body, 5}, s);
    coverage_line(c, left + 1, 7, body - 2, s);
    if (v->detail) { session_detail(app, c, (struct kt_rect){left, 8, body, height - 10}); return; }
    if (chart) {
        sessions_time(c, (struct kt_rect){left, 8, body / 2 - 1, chart}, s);
        sessions_trend(c, (struct kt_rect){left + body / 2, 8, body - body / 2, chart}, s);
    }
    sessions_table(app, c, (struct kt_rect){left, 8 + chart, body, height - 10 - chart});
}

/* The footer line: an error, a notice, the sample's last warning, or what
 * the page covers. */
static void sessions_status(struct app *app, char *status, size_t size) {
    const struct statistics_view *v = app->statistics;
    const char *sep = dot(app), *text = v->model->warning;
    char scope[256];
    (void)snprintf(scope, sizeof(scope), "Head sessions%s%s%s%.60s%s", sep, range_text(v), sep,
        v->filter.query[0] ? v->filter.query : "all heads", v->filter.attention ? " / attention only" : "");
    if (!v->model->warnings) text = scope;
    if (app->notice[0]) text = app->notice;
    if (v->error[0]) text = v->error;
    (void)snprintf(status, size, "%s%s", v->model->warnings ? "PARTIAL / " : "", text);
}

static bool render_sessions(struct app *app, unsigned frame, bool headless) {
    struct statistics_view *v;
    struct hs_session_summary summary;
    struct kt_canvas c;
    char status[512];
    bool stale;
    if (!native_workspace_init(app) || !frame_begin(app, headless)) return false;
    c = app->frame;
    if (c.width < 19 || c.height < 6) return false;
    v = app->statistics;
    statistics_visible(app);
    hs_session_summarize(v->model, &v->filter, &summary);
    stale = v->stale || app->snapshot_stale;
    chrome_header(app, &c, stale ? "STATISTICS (STALE)" : "STATISTICS");
    sessions_body(app, &c, c.width, c.height, &summary);
    sessions_status(app, status, sizeof(status));
    chrome_footer(app, &c, status, (v->stale || v->model->warnings) ? KT_WARNING : KT_MUTED, NULL);
    frame_end(app, frame, headless);
    return true;
}

bool render_statistics(struct app *app, unsigned frame, bool headless) {
    if (!statistics_init(app)) return false;
    if (statistics_sessions_shown(app)) return render_sessions(app, frame, headless);
    return render_statistics_runs(app, frame, headless);
}
