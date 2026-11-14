#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "statistics.h"
/* The Stats screen's actions, from the binding table: moving, the pages, the
 * filters (typed as you go), the evidence and the jumps to Work. Nothing here
 * changes anything outside the screen. */

void statistics_toggle(struct app *app) {
    if (!statistics_init(app)) return;
    if (app->view == 8) app->view = app->statistics->back_view;
    else {
        app->statistics->back_view = app->view;
        app->statistics->graph_open = false;
        app->statistics->head_open = false;
        app->view = 8;
        statistics_request(app);
    }
    app->help = false; app->diagnostics = false;
    app->notice[0] = '\0';
}

/* ---- jumps --------------------------------------------------------------- */

#define GRAPH_LOADING "Opening the graph..."

/* Shows the selected run in the graph view when the retained workflow read
 * includes it. */
static bool statistics_graph_open(struct app *app) {
    struct statistics_view *v = app->statistics;
    size_t i;
    for (i=0;i<app->workflows->run_count;i++) if (!strcmp(app->workflows->runs[i].id,v->selected_id)) break;
    if (i == app->workflows->run_count) return false;
    app->workflow_run=i; app->workflow_node=0; app->graph_x=app->graph_y=0; app->graph_follow=true;
    app->view=5; v->graph_open=true;
    return true;
}

/* g asks for a fresh workflow read and does not wait for it. A retained read
 * that shows the run opens the graph at once; otherwise the graph opens, or
 * says why it cannot, when the read arrives. */
static void statistics_graph(struct app *app) {
    struct statistics_view *v = app->statistics;
    if (app->fleet || !v->model || !v->count) return;
    v->graph_run[0]='\0';
    native_observations_request(app,OBSERVE_WORKFLOWS);
    if (app->workflows && !app->workflow_stale && statistics_graph_open(app)) return;
    copy_text(v->graph_run,sizeof(v->graph_run),v->selected_id);
    copy_text(app->notice,sizeof(app->notice),GRAPH_LOADING);
}

/* A workflow read ended. The graph that g asked for opens only if the same
 * run is still selected in Statistics. */
void statistics_graph_arrived(struct app *app, bool ok) {
    struct statistics_view *v = app->statistics;
    bool wanted;
    if (!v || !v->graph_run[0]) return;
    wanted = app->view==8 && !strcmp(v->graph_run,v->selected_id);
    v->graph_run[0]='\0';
    if (!strcmp(app->notice,GRAPH_LOADING)) app->notice[0]='\0';
    if (!wanted) return;
    if (!ok) copy_text(app->notice,sizeof(app->notice),"Graph unavailable; statistics selection retained");
    else if (!statistics_graph_open(app))
        copy_text(app->notice,sizeof(app->notice),"Selected run outside graph sample; inspect recorded run evidence here");
}

/* g on a session shows its head in Details; Esc there comes back. A head
 * that is gone (removed since) is said so and nothing moves. */
static void statistics_head(struct app *app) {
    struct statistics_view *v = app->statistics;
    const struct hs_session *s;
    size_t i;
    bool detail;
    if (!v->session_count) return;
    s = &v->model->sessions[v->session_rows[v->session_selected]];
    for (i = 0; i < app->model.head_count && strcmp(app->model.heads[i].branch, s->branch); i++) {}
    if (i == app->model.head_count) {
        (void)snprintf(app->notice, sizeof(app->notice), "%.120s is not a head in this project any more", s->branch);
        return;
    }
    if (!head_matches(&app->model.heads[i], app->search)) app->search[0] = '\0';
    app->run_row = false; app->selected = i;
    detail = v->detail;
    enter_view(app, 1);
    v->detail = detail; v->head_open = true;
}

const char *statistics_graph_label(const struct app *app) {
    return statistics_sessions_shown(app) ? "open head" : "graph";
}

/* The pages key names the pages there are to go through. */
const char *statistics_page_label(const struct app *app) {
    enum stats_page pages[3];
    size_t count = app->statistics ? statistics_pages(app, pages) : 0;
    if (count == 3) return "pages";
    return count == 2 && pages[0] == STATS_RUNS ? "runs/cohorts" : "sessions/runs";
}

/* ---- what applies -------------------------------------------------------- */

static bool local_model(const struct app *app) {
    return !app->fleet && app->statistics && app->statistics->model;
}

static bool runs_page(const struct app *app) {
    return local_model(app) && statistics_page(app) == STATS_RUNS;
}

static size_t rows_shown(const struct app *app) {
    struct statistics_view *v = app->statistics;
    if (!v) return 0;
    if (statistics_cohorts_shown(app)) return statistics_cohort_count(v);
    return statistics_sessions_shown(app) ? v->session_count : v->count;
}

/* Keys of the list and its evidence: the cohorts page has a list to move
 * through but no evidence or graph behind its rows. */
static bool list_applies(const struct app *app, enum key_action action) {
    const struct statistics_view *v = app->statistics;
    bool cohorts = statistics_cohorts_shown(app);
    switch (action) {
    case KEY_STATS_GRAPH: return local_model(app) && !cohorts && rows_shown(app) > 0;
    case KEY_STATS_OPEN: return !cohorts && rows_shown(app) > 0;
    case KEY_STATS_PANE: return v && v->detail && stats_side(app);
    default: return rows_shown(app) > 0;
    }
}

bool statistics_applies(const struct app *app, enum key_action action) {
    const struct statistics_view *v = app->statistics;
    enum stats_page pages[3];
    switch (action) {
    case KEY_STATS_PAGE_PREVIOUS: case KEY_STATS_PAGE_NEXT: return statistics_pages(app, pages) > 1;
    case KEY_STATS_METRIC: return runs_page(app);
    case KEY_STATS_WORKFLOW: return runs_page(app) || statistics_cohorts_shown(app);
    case KEY_STATS_COHORTS: return local_model(app) && statistics_cohorts_recorded(v->model);
    case KEY_STATS_LOCATION: return local_model(app);
    case KEY_STATS_GRAPH: case KEY_STATS_OPEN: case KEY_STATS_UP: case KEY_STATS_DOWN: case KEY_STATS_PANE:
        return list_applies(app, action);
    case KEY_STATS_RANGE: case KEY_STATS_FILTER: case KEY_STATS_ATTENTION: case KEY_STATS_RESET:
        return v && (v->model || app->fleet);
    default: return true;
    }
}

/* ---- the filter, typed as you go ----------------------------------------- */

bool statistics_typing(const struct app *app) {
    return app->statistics && app->statistics->typing;
}

static void filter_changed(struct app *app) {
    app->statistics->step_scroll = 0;
    statistics_visible(app);
}

/* Appends text up to the filter's size, never cutting a character. */
static void filter_append(struct app *app, const char *bytes, size_t length) {
    char *query = app->statistics->filter.query;
    size_t used = strlen(query), room = sizeof(app->statistics->filter.query) - 1 - used, i;
    if (length > room) length = kt_text_fit(bytes, room, (int)room, NULL);
    for (i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)bytes[i];
        if (byte < 0x20 || byte == 0x7f) break;
        query[used++] = (char)byte;
    }
    query[used] = '\0';
    filter_changed(app);
}

static void filter_erase(struct app *app) {
    char *query = app->statistics->filter.query;
    size_t used = strlen(query);
    while (used && ((unsigned char)query[used - 1] & 0xc0U) == 0x80U) used--;
    if (used) used--;
    query[used] = '\0';
    filter_changed(app);
}

/* Text keys and Backspace edit the filter; Enter, Esc and the arrows are
 * bindings of the filter's chain and go to the table. */
bool statistics_filter_event(struct app *app, const struct kt_event *e) {
    if (!statistics_typing(app) || e->type != KT_KEY) return false;
    if (e->key == KT_KEY_BACKSPACE || (e->key == 'h' && e->mods == KT_MOD_CTRL)) { filter_erase(app); return true; }
    if (e->key >= KT_KEY_UP || e->key < 0x20U || (e->mods & (KT_MOD_CTRL | KT_MOD_ALT))) return false;
    filter_append(app, e->bytes, e->length);
    return true;
}

/* Pasted text fills the filter while it is typed: its first line. */
bool statistics_paste(struct app *app, const char *bytes, size_t length) {
    size_t line = 0;
    if (app->view != 8 || !statistics_typing(app)) return false;
    while (line < length && bytes[line] != '\n' && bytes[line] != '\r') line++;
    filter_append(app, bytes, line);
    return true;
}

/* ---- actions ------------------------------------------------------------- */

static void workflow_next(struct statistics_view *v) {
    const char *names[HS_RUNS+1] = {""};
    size_t count=1, selected=0, i, j;
    if (!v->model) return;
    for(i=0;i<v->model->run_count;i++) {
        for(j=1;j<count;j++) if(!strcmp(names[j],v->model->runs[i].name)) break;
        if(j==count) names[count++]=v->model->runs[i].name;
    }
    for(i=1;i<count;i++) if(!strcmp(names[i],v->filter.workflow)) selected=i;
    selected=(selected+1)%count;
    copy_text(v->filter.workflow,sizeof(v->filter.workflow),names[selected]);
}

static void open_evidence(struct app *app) {
    struct statistics_view *v = app->statistics;
    v->detail = !v->detail && rows_shown(app) > 0;
    v->evidence_focus = false; v->step_scroll = 0;
}

/* The filters: range, needs you only, workflow, location, everything
 * reset. A changed filter closes the evidence, which may no longer match. */
static bool filter_action(struct app *app, enum key_action action) {
    struct statistics_view *v = app->statistics;
    if (action == KEY_STATS_RANGE) v->filter.days = v->filter.days == 0 ? 1 : v->filter.days == 1 ? 7 : 0;
    else if (action == KEY_STATS_ATTENTION) v->filter.attention = !v->filter.attention;
    else if (action == KEY_STATS_WORKFLOW) workflow_next(v);
    else if (action == KEY_STATS_LOCATION) statistics_location_cycle(v, 1);
    else if (action == KEY_STATS_RESET) { memset(&v->filter, 0, sizeof(v->filter)); v->typing = false; }
    else return false;
    v->detail = false; v->evidence_focus = false;
    if (statistics_cohorts_shown(app)) v->step_scroll = 0;
    return true;
}

/* [ and ] go through the pages the sample has, around; C goes to the
 * cohorts page and back to the runs. */
static void page_step(struct app *app, int direction) {
    enum stats_page pages[3], shown = statistics_page(app);
    size_t count = statistics_pages(app, pages), i;
    for (i = 0; i < count && pages[i] != shown; i++) {}
    if (i == count) i = 0;
    i = (i + (direction > 0 ? 1 : count - 1)) % count;
    statistics_page_show(app->statistics, pages[i]);
}

static void typing_action(struct app *app, enum key_action action) {
    struct statistics_view *v = app->statistics;
    if (action == KEY_STATS_FILTER) v->typing = true;
    else if (action == KEY_STATS_FILTER_KEEP) v->typing = false;
    else if (action == KEY_STATS_FILTER_CLEAR) { v->typing = false; v->filter.query[0] = '\0'; }
    else if (action == KEY_STATS_FILTER_UP || action == KEY_STATS_FILTER_DOWN)
        statistics_move(app, action == KEY_STATS_FILTER_DOWN ? 1 : -1);
}

/* The pages, the metric pages, the evidence pane and the jumps. */
static bool page_action(struct app *app, enum key_action action) {
    struct statistics_view *v = app->statistics;
    if (action == KEY_STATS_OPEN) open_evidence(app);
    else if (action == KEY_STATS_PANE) v->evidence_focus = !v->evidence_focus;
    else if (action == KEY_STATS_PAGE_PREVIOUS || action == KEY_STATS_PAGE_NEXT)
        page_step(app, action == KEY_STATS_PAGE_NEXT ? 1 : -1);
    else if (action == KEY_STATS_COHORTS)
        statistics_page_show(v, statistics_cohorts_shown(app) ? STATS_RUNS : STATS_COHORTS);
    else if (action == KEY_STATS_METRIC) { v->metric_page = (v->metric_page + 1) % (HS_METRICS + 1); v->step_scroll = 0; }
    else if (action == KEY_STATS_GRAPH && statistics_sessions_shown(app)) statistics_head(app);
    else if (action == KEY_STATS_GRAPH) statistics_graph(app);
    else return false;
    return true;
}

void statistics_perform(struct app *app, enum key_action action) {
    if (!app->statistics || !statistics_applies(app, action)) return;
    app->notice[0] = '\0';
    if (action == KEY_STATS_UP || action == KEY_STATS_DOWN) statistics_move(app, action == KEY_STATS_DOWN ? 1 : -1);
    else if (!filter_action(app, action) && !page_action(app, action)) typing_action(app, action);
    statistics_visible(app);
}

/* Esc on Stats: clear the typed filter, give the keys back to the list,
 * close the evidence, then return to where Stats was opened from. Esc in a
 * view a jump opened comes back to Stats. */
static bool stats_back(struct app *app) {
    struct statistics_view *v = app->statistics;
    if (app->help) app->help = false;
    else if (v->filter.query[0]) { v->filter.query[0] = '\0'; statistics_visible(app); }
    else if (v->detail && v->evidence_focus && stats_side(app)) v->evidence_focus = false;
    else if (v->detail) { v->detail = false; v->evidence_focus = false; }
    else app->view = v->back_view;
    return true;
}

bool statistics_back(struct app *app) {
    struct statistics_view *v = app->statistics;
    if (!v) return false;
    if (app->view == 8) return stats_back(app);
    if (app->view == 5 && v->graph_open) { app->view = 8; v->graph_open = false; return true; }
    if (app->view == 1 && v->head_open && app->previous_view == 8) { app->view = 8; v->head_open = false; return true; }
    return false;
}
