#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "onboarding_flow.h"
/* Hosts on screen and its keys. From 90 columns the list (44 columns from
 * 120, 40 below) and the detail sit side by side; from 70 the list is on top
 * with the detail below it; narrower, the list takes the screen and Enter
 * opens the detail in its place. A setup that is open fills the detail with
 * its steps and the current step; its form and approvals are modals. The
 * list's rule carries the page strip, Hosts and Onboarding. */

#define FAINT KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0)
#define ACCENT KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SURFACE, 0)
#define HEADING KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_BOLD)
#define SIDE_BY_SIDE 90
#define STACKED 70
/* Display rows that are section headings rather than list rows. */
#define ROW_OBSERVED ((size_t)-1)
#define ROW_SETTING_UP ((size_t)-2)

static const char *glyph(const struct app *app, const char *utf8, const char *ascii) { return app->ascii ? ascii : utf8; }

struct native_hosts *hosts_state(struct app *app) {
    if (app->hosts) return app->hosts;
    app->hosts = calloc(1, sizeof(*app->hosts));
    if (app->hosts) app->hosts->machine.job.fd = -1;
    return app->hosts;
}

void hosts_destroy(struct app *app) {
    hosts_machine_stop(app);
    free(app->hosts);
    app->hosts = NULL;
}

void hosts_opened(struct app *app) {
    native_setup_hosts_refresh(app);
    hosts_machine_request(app);
}

/* ---- focus, the flow and the layout ------------------------------------------ */

/* The open setup fills the detail; its form is a modal over the screen. */
static bool flow_shown(const struct app *app) {
    return native_setup_active(app) && app->setup->screen != SETUP_SCREEN_FORM;
}

static bool detail_focused(const struct app *app) {
    return app->hosts && app->hosts->focus_detail && flow_shown(app);
}

void hosts_focus_detail(struct app *app, bool focus) {
    struct native_hosts *h = hosts_state(app);
    if (!h) return;
    h->focus_detail = focus;
    if (!focus) h->detail_open = false;
}

bool hosts_modal_open(const struct app *app) { return app->view == 6 && native_setup_modal_open(app); }

enum key_chain_id hosts_chain(const struct app *app) {
    return detail_focused(app) ? KEY_CHAIN_HOSTS_SETUP : KEY_CHAIN_HOSTS;
}

/* A pane: its titled rule on row y, content from row body to bottom. */
struct pane { int x, y, width, bottom, body; bool focused; };
struct hosts_layout { struct pane list, detail; bool list_shown, detail_shown; };

/* Rows the list needs: its rule, a heading before the observed hosts and
 * before the setups, the rows and the notes over them. */
static int list_need(const struct app *app, const struct host_row *rows, size_t count) {
    size_t i;
    int need = (int)count + 1 + (app->remote_stale ? 1 : 0) + (app->hosts && app->hosts->filter[0] ? 1 : 0);
    for (i = 0; i < count; i++) need += (rows[i].kind == HOST_ROW_HOST || rows[i].kind == HOST_ROW_SETUP) && (!i || rows[i - 1].kind != rows[i].kind);
    return need;
}

static void layout_side(const struct app *app, struct hosts_layout *l, int top, int bottom) {
    int width = app->cols >= 120 ? 44 : 40;
    l->list = (struct pane){1, top, width, bottom, top + 1, !detail_focused(app)};
    l->detail = (struct pane){width + 4, top, app->cols - width - 6, bottom, top + 1, detail_focused(app)};
    l->list_shown = l->detail_shown = true;
}

/* List on top, the detail below; an open setup with the focus takes it all. */
static void layout_stacked(const struct app *app, struct hosts_layout *l, int top, int bottom, int need) {
    int rows = need, most = (bottom - top) / 2;
    if (rows > most) rows = most;
    l->list = (struct pane){1, top, app->cols - 3, top + rows, top + 1, !detail_focused(app)};
    l->detail = (struct pane){1, top + rows + 1, app->cols - 3, bottom, top + rows + 2, detail_focused(app)};
    l->list_shown = !detail_focused(app);
    l->detail_shown = true;
    if (!l->list_shown) l->detail.y = top, l->detail.body = top + 1;
}

static void hosts_layout(const struct app *app, struct hosts_layout *l, const struct host_row *rows, size_t count) {
    const struct native_hosts *h = app->hosts;
    int top = 2, bottom = app->rows - 2;
    memset(l, 0, sizeof(*l));
    if (app->cols >= SIDE_BY_SIDE) { layout_side(app, l, top, bottom); return; }
    if (app->cols >= STACKED) { layout_stacked(app, l, top, bottom, list_need(app, rows, count)); return; }
    l->list = l->detail = (struct pane){1, top, app->cols - 3, bottom, top + 1, true};
    l->detail_shown = (h && h->detail_open) || detail_focused(app);
    l->list_shown = !l->detail_shown;
}

/* ---- the rows ------------------------------------------------------------------ */

struct row_text { const char *glyph; struct kt_style tone; char name[160], note[200]; };

static void machine_text(const struct app *app, struct row_text *t) {
    size_t heads, running, gone, agents = hosts_machine_agents(app);
    hosts_machine_heads(app, &heads, &running, &gone);
    t->glyph = glyph(app, "\xe2\x97\x86", "*"); t->tone = ACCENT;
    copy_text(t->name, sizeof(t->name), "This machine");
    snprintf(t->note, sizeof(t->note), "%zu head%s", heads, heads == 1 ? "" : "s");
    if (app->hosts && app->hosts->machine.agents_read)
        text_append(t->note, sizeof(t->note), "%s%zu agent%s", dot(app), agents, agents == 1 ? "" : "s");
}

static bool host_answering(const struct host_observation *host) { return !strcmp(host->state, "responded"); }

/* A host that did not answer: the rows kept from its last observation (the
 * glyph says it is disconnected), or no head count at all. */
static void silent_host_text(const struct app *app, const struct host_observation *host, struct row_text *t) {
    t->glyph = glyph(app, host->retained ? "\xe2\x86\xaf" : "\xe2\x96\xb2", "!");
    t->tone = host->retained ? KT_MUTED : KT_WARNING;
    if (host->retained) snprintf(t->note, sizeof(t->note), "%u retained row%s", host->heads, host->heads == 1 ? "" : "s");
    else snprintf(t->note, sizeof(t->note), "disconnected%s-- heads", dot(app));
}

/* "2 heads · fresh", "1 head · stale 300s", "disconnected · -- heads". */
static void host_text(const struct app *app, const struct host_observation *host, struct row_text *t) {
    bool stale = app->remote_stale || strcmp(host->freshness, "fresh");
    copy_text(t->name, sizeof(t->name), host->name);
    if (!host_answering(host)) { silent_host_text(app, host, t); return; }
    t->glyph = glyph(app, stale ? "\xe2\x96\xb2" : "\xe2\x97\x8f", stale ? "!" : "*");
    t->tone = stale ? KT_WARNING : KT_SUCCESS;
    snprintf(t->note, sizeof(t->note), "%u head%s%s", host->heads, host->heads == 1 ? "" : "s", dot(app));
    if (app->remote_stale) text_append(t->note, sizeof(t->note), "stale %lds", remote_age(app));
    else if (stale && host->age[0] && strcmp(host->age, "-")) text_append(t->note, sizeof(t->note), "%s %ss", host->freshness, host->age);
    else text_append(t->note, sizeof(t->note), "%s", host->freshness);
}

static void row_text(const struct app *app, const struct host_row *row, struct row_text *t) {
    memset(t, 0, sizeof(*t));
    if (row->kind == HOST_ROW_MACHINE) { machine_text(app, t); return; }
    if (row->kind == HOST_ROW_HOST) { host_text(app, &app->model.hosts[row->index], t); return; }
    setup_row_text(app, row, t->name, sizeof(t->name), t->note, sizeof(t->note));
    if (row->kind == HOST_ROW_ADD) { t->glyph = "+"; t->tone = KT_STRONG; return; }
    t->glyph = glyph(app, "\xe2\x97\x90", "~"); t->tone = KT_WARNING;
}

/* The rows as shown: a heading before the observed hosts and before the
 * setups, then the rows themselves (indexes into rows). */
static size_t display_rows(const struct host_row *rows, size_t count, size_t *out) {
    size_t i, n = 0;
    for (i = 0; i < count; i++) {
        bool first = !i || rows[i - 1].kind != rows[i].kind;
        if (first && rows[i].kind == HOST_ROW_HOST) out[n++] = ROW_OBSERVED;
        if (first && rows[i].kind == HOST_ROW_SETUP) out[n++] = ROW_SETTING_UP;
        out[n++] = i;
    }
    return n;
}

static size_t kind_count(const struct host_row *rows, size_t count, enum host_row_kind kind) {
    size_t i, n = 0;
    for (i = 0; i < count; i++) n += rows[i].kind == kind;
    return n;
}

static void record_hit(struct app *app, const struct pane *p, int y, size_t row) {
    if (app->hit_count >= MAX_HEADS) return;
    app->hit_rows[app->hit_count] = app->hit_bottom[app->hit_count] = y + 1;
    app->hit_left[app->hit_count] = p->x + 1;
    app->hit_right[app->hit_count] = p->x + p->width;
    app->hit_items[app->hit_count++] = row;
}

static void draw_heading(struct app *app, const struct pane *p, int y, const char *name, size_t count) {
    char text[24];
    int width = kt_text_width(name);
    snprintf(text, sizeof(text), "%zu", count);
    kt_text(&app->frame, (struct kt_rect){p->x + 1, y, p->width - 1, 1}, name, HEADING);
    kt_text(&app->frame, (struct kt_rect){p->x + width + 2, y, p->width - width - 2, 1}, text, FAINT);
}

/* The selected row is filled and barred while the list has the focus;
 * without it only a faint bar marks it. A name takes its column, and less
 * when the note beside it needs the room. */
static void draw_row(struct app *app, const struct pane *p, int y, const struct row_text *t, bool selected, int name_width) {
    struct kt_canvas *c = &app->frame;
    bool lit = selected && p->focused;
    int note = kt_text_width(t->note), room = p->width - 4 - note, name = kt_text_width(t->name), note_x;
    if (room > name_width) room = name_width;
    if (room < 8) room = 8;
    if (name > room) name = room;
    note_x = p->x + 3 + name + 1;
    if (lit) kt_fill(c, (struct kt_rect){p->x, y, p->width, 1}, KT_SELECTED);
    if (selected) kt_text(c, (struct kt_rect){p->x, y, 1, 1}, glyph(app, "\xe2\x96\x8c", ">"), lit ? KT_SELECTED : FAINT);
    kt_text(c, (struct kt_rect){p->x + 1, y, 1, 1}, t->glyph, lit ? KT_SELECTED : t->tone);
    kt_text_aligned(c, (struct kt_rect){p->x + 3, y, name, 1}, t->name, KT_ALIGN_LEFT,
                    lit ? KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, KT_BOLD) : KT_BASE);
    if (note_x < p->x + p->width)
        kt_text_aligned(c, (struct kt_rect){note_x, y, p->x + p->width - note_x, 1}, t->note, KT_ALIGN_RIGHT, lit ? KT_SELECTED : KT_MUTED);
}

/* Names take what the longest needs, up to half the row. */
static int name_column(const struct app *app, const struct pane *p, const struct host_row *rows, size_t count) {
    int widest = 8, most = (p->width - 4) / 2;
    size_t i;
    for (i = 0; i < count; i++) {
        struct row_text t;
        int width;
        row_text(app, &rows[i], &t);
        width = kt_text_width(t.name);
        if (width > widest) widest = width;
    }
    return widest < most ? widest : most;
}

/* The first display row shown keeps the selection in view. */
static size_t list_top(struct native_hosts *h, const size_t *shown, size_t n, size_t selected, int room) {
    size_t at = 0, i;
    for (i = 0; i < n; i++) if (shown[i] == selected) { at = i; break; }
    if (at > 0 && shown[at - 1] >= ROW_SETTING_UP) at--;    /* keep its heading */
    if (h->top > at) h->top = at;
    if (room > 0 && i < n && i >= h->top + (size_t)room) h->top = i - (size_t)room + 1;
    return h->top;
}

static void page_strip(struct app *app, const struct pane *p, bool onboarding) {
    static const char *const names[] = {"Hosts", "Onboarding"};
    int x = p->x, i;
    pane_rule_styled(&app->frame, (struct kt_rect){p->x, p->y, p->width + 1, 1}, NULL, p->focused);
    for (i = 0; i < 2; i++) {
        int width = kt_text_width(names[i]);
        bool active = (i == 1) == onboarding;
        struct kt_style tone = active ? (p->focused ? KT_FOCUS : KT_STRONG) : KT_MUTED;
        if (x + width + 2 > p->x + p->width) break;
        kt_fill(&app->frame, (struct kt_rect){x, p->y, width + 2, 1}, KT_BASE);
        kt_text(&app->frame, (struct kt_rect){x, p->y, width, 1}, names[i], tone);
        x += width + 2;
    }
}

static void rule_note(struct app *app, const struct pane *p, const char *note) {
    int width = kt_text_width(note);
    if (!note[0] || width + 16 > p->width) return;
    kt_fill(&app->frame, (struct kt_rect){p->x + p->width - width - 4, p->y, width + 2, 1}, KT_BASE);
    kt_text(&app->frame, (struct kt_rect){p->x + p->width - width - 3, p->y, width, 1}, note, KT_MUTED);
}

/* Notes over the list: a stale fleet observation, the filter. */
static int list_notes(struct app *app, const struct pane *p, int y) {
    char text[TEXT + 96];
    if (app->remote_stale && y < p->bottom) {
        snprintf(text, sizeof(text), "Remote observation stale (%lds old): %s; r retries", remote_age(app), app->remote_error);
        kt_text_aligned(&app->frame, (struct kt_rect){p->x + 1, y++, p->width - 1, 1}, text, KT_ALIGN_LEFT, KT_WARNING);
    }
    if (app->hosts && app->hosts->filter[0] && y < p->bottom) {
        snprintf(text, sizeof(text), "Filter: %s%sesc clears", app->hosts->filter, dot(app));
        kt_text_aligned(&app->frame, (struct kt_rect){p->x + 1, y++, p->width - 1, 1}, text, KT_ALIGN_LEFT, KT_MUTED);
    }
    return y;
}

static void draw_list(struct app *app, const struct pane *p, const struct host_row *rows, size_t count) {
    struct native_hosts *h = app->hosts;
    size_t shown[MAX_HEADS + 2], n = display_rows(rows, count, shown), i, top;
    char total[24];
    int y, name_width = name_column(app, p, rows, count);
    page_strip(app, p, false);
    snprintf(total, sizeof(total), "%zu", count - 1);
    rule_note(app, p, total);
    y = list_notes(app, p, p->body);
    top = list_top(h, shown, n, app->host_selected, p->bottom - y);
    for (i = top; i < n && y < p->bottom; i++, y++) {
        struct row_text t;
        if (shown[i] == ROW_OBSERVED) { draw_heading(app, p, y, "Observed", kind_count(rows, count, HOST_ROW_HOST)); continue; }
        if (shown[i] == ROW_SETTING_UP) { draw_heading(app, p, y, "Setting up", kind_count(rows, count, HOST_ROW_SETUP)); continue; }
        row_text(app, &rows[shown[i]], &t);
        draw_row(app, p, y, &t, shown[i] == app->host_selected, name_width);
        record_hit(app, p, y, shown[i]);
    }
}

/* ---- the detail ------------------------------------------------------------------ */

static void content(struct app *app, const struct pane *p) {
    app->content_x = p->x; app->content_width = p->width;
    app->line = p->body; app->limit = p->bottom;
}

static void detail_rule(struct app *app, const struct pane *p, const char *title, const char *note) {
    pane_rule_styled(&app->frame, (struct kt_rect){p->x, p->y, p->width + 1, 1}, title, p->focused);
    rule_note(app, p, note);
}

static void blank(struct app *app) { if (app->line < app->limit) app->line++; }

static void reading(struct app *app, const char *label, const char *value, bool read) {
    char text[96];
    snprintf(text, sizeof(text), "%s reading%s", glyph(app, "\xe2\x97\x8c", "."), glyph(app, "\xe2\x80\xa6", "..."));
    labelled(app, label, value[0] ? value : read ? "unknown" : text, value[0] ? KT_BASE : KT_MUTED);
}

static void machine_heads(struct app *app) {
    size_t heads, running, gone;
    char text[160];
    hosts_machine_heads(app, &heads, &running, &gone);
    if (!heads) copy_text(text, sizeof(text), "none yet");
    else snprintf(text, sizeof(text), "%zu head%s%s%zu running", heads, heads == 1 ? "" : "s", dot(app), running);
    if (gone) text_append(text, sizeof(text), "%s%zu terminal%s gone", dot(app), gone, gone == 1 ? "" : "s");
    labelled(app, "Heads", text, KT_BASE);
}

static void machine_agents(struct app *app, const struct hosts_machine *m) {
    size_t i;
    style(app, TONE_STRONG); linef(app, "Agents"); style(app, TONE_BASE);
    if (!m->agents_read) { reading(app, "", "", m->read); return; }
    for (i = 0; i < m->agent_count && app->line < app->limit; i++) {
        char state[64];
        snprintf(state, sizeof(state), "%s %s", m->agents[i].available ? glyph(app, "\xe2\x97\x8f", "*") : glyph(app, "\xe2\x97\x8b", "-"),
                 m->agents[i].available ? "on PATH" : "not found on PATH");
        column(app, 1, 10, KT_BASE, m->agents[i].name);
        column(app, 11, app->content_width - 11, m->agents[i].available ? KT_SUCCESS : KT_MUTED, state);
        app->line++;
    }
    if (!m->agent_count) linef(app, " no agent profiles");
}

/* The words every Hosts start has said since remote setup existed (U10). */
static void remote_explanation(struct app *app, size_t count) {
    paragraph(app, "Hydra can run agents on other machines over SSH, for example a build box with more cores or a "
              "machine that already has the right toolchain, when a task needs more than this machine offers. Adding a "
              "host checks its SSH host key and requirements, installs a matching Hydra there and helps you install "
              "and sign in to agents. Nothing changes on the remote until you approve it.", KT_BASE);
    blank(app);
    paragraph(app, count > 2 ? "Enter continues a host's setup or upgrades its Hydra; e edits and x removes an unfinished one. A set-up host's work appears in Work beside local heads; L filters by location."
                             : "Press Enter or A to add a host. Its work then appears in Work beside local heads; L filters by location.", KT_MUTED);
}

static void machine_detail(struct app *app, const struct pane *p, size_t count) {
    const struct hosts_machine *m = &app->hosts->machine;
    detail_rule(app, p, "This machine", "");
    content(app, p);
    machine_heads(app);
    reading(app, "tmux", m->tmux, m->read);
    reading(app, "Hydra", m->hydra, m->read);
    blank(app);
    machine_agents(app, m);
    blank(app);
    remote_explanation(app, count);
}

static void host_task(struct app *app, const struct host_observation *host) {
    size_t i;
    char text[TEXT * 2];
    for (i = 0; host->tasks && i < app->model.task_count; i++) {
        const struct task_observation *task = &app->model.tasks[i];
        if (strcmp(task->host, host->name)) continue;
        blank(app);
        snprintf(text, sizeof(text), "Task: %s / %s", task->task_id, task->state);
        style(app, TONE_BASE); linef(app, "%s", text);
        snprintf(text, sizeof(text), "Wait: %s / %s", task->waiting_reason, task->next_action);
        style(app, TONE_WARNING); linef(app, "%s", text); style(app, TONE_BASE);
        return;
    }
}

static void host_detail(struct app *app, const struct pane *p, const struct host_observation *host) {
    bool failed = !host_answering(host);
    char state[TEXT + 128];
    host_state_text(app, host, state, sizeof(state));
    detail_rule(app, p, host->name, failed ? "disconnected" : "connected");
    content(app, p);
    linef(app, "List response: %s", host->state);
    style(app, failed || app->remote_stale ? TONE_WARNING : TONE_BASE); linef(app, "Connection: %s%s", failed ? "disconnected; " : "", state);
    style(app, strcmp(host->freshness, "fresh") ? TONE_WARNING : TONE_BASE);
    linef(app, "Observation: %s / age %s%s", host->freshness, host->age[0] ? host->age : "-", host->retained ? " / rows retained" : "");
    style(app, TONE_WARNING); if (host->error[0] && strcmp(host->error, "-")) linef(app, "%s", host->error);
    style(app, TONE_BASE);
    blank(app);
    linef(app, "Last confirmed: %s", host->last_confirmed);
    linef(app, "%u task observations", host->tasks);
    linef(app, "Process liveness: unobserved");
    linef(app, "CPU / memory / load: unavailable");
    style(app, failed ? TONE_WARNING : TONE_MUTED);
    linef(app, "%s", failed ? "Disconnected: rows are the last observation; r retries." : "Enter shows this host's work; r refreshes it.");
    style(app, TONE_BASE);
    host_task(app, host);
}

/* A finished setup whose host runs another Hydra: Enter shows the upgrade plan. */
static void upgrade_detail(struct app *app, const char *state) {
    style(app, TONE_WARNING); linef(app, "%s", state); style(app, TONE_BASE);
    blank(app);
    paragraph(app, "Enter shows the upgrade plan: a new pinned install beside the old one, which stays; after a handshake "
              "the host switches to it. You approve it first.", KT_BASE);
}

static void setup_detail(struct app *app, const struct pane *p, const struct host_row *row) {
    const struct setup_record *r = &app->setup->records[row->index];
    char name[160], state[200];
    setup_row_text(app, row, name, sizeof(name), state, sizeof(state));
    detail_rule(app, p, name, r->destination);
    content(app, p);
    if (r->upgrade) { upgrade_detail(app, state); return; }
    linef(app, "%s", state);
    blank(app);
    paragraph(app, "This host is not in the fleet yet. Enter shows its steps and continues setup; you approve every change first.", KT_BASE);
    blank(app);
    style(app, TONE_MUTED); linef(app, "e edits it, x removes its record."); style(app, TONE_BASE);
}

static void add_detail(struct app *app, const struct pane *p, size_t count) {
    detail_rule(app, p, "Add a host", "");
    content(app, p);
    paragraph(app, "Hydra checks the SSH host key and requirements, installs Hydra and helps with agents; you approve every "
              "change first.", KT_BASE);
    blank(app);
    remote_explanation(app, count);
}

static void flow_detail(struct app *app, const struct pane *p) {
    const struct native_setup *s = app->setup;
    detail_rule(app, p, s->name, s->current->destination[0] ? s->current->destination : s->destination);
    content(app, p);
    native_setup_render(app);
}

static void draw_detail(struct app *app, const struct pane *p, const struct host_row *rows, size_t count) {
    const struct host_row *row = &rows[app->host_selected < count ? app->host_selected : 0];
    if (flow_shown(app)) { flow_detail(app, p); return; }
    switch (row->kind) {
    case HOST_ROW_MACHINE: machine_detail(app, p, count); break;
    case HOST_ROW_HOST: host_detail(app, p, &app->model.hosts[row->index]); break;
    case HOST_ROW_SETUP: setup_detail(app, p, row); break;
    default: add_detail(app, p, count); break;
    }
}

static void hosts_follow_setup(struct app *app);

static void onboarding_page(struct app *app) {
    struct pane p = {1, 2, app->cols - 3, app->rows - 2, 3, true};
    page_strip(app, &p, true);
    content(app, &p);
    app->content_x = 2; app->content_width = app->cols - 4;
    native_onboarding_render(app);
}

void hosts_render(struct app *app) {
    struct host_row rows[MAX_HEADS];
    struct hosts_layout l;
    size_t count;
    if (!hosts_state(app) || app->rows < 6) return;
    if (native_onboarding_active(app)) { onboarding_page(app); return; }
    count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    if (app->host_selected >= count) app->host_selected = 0;
    hosts_follow_setup(app);
    hosts_layout(app, &l, rows, count);
    if (l.list_shown) draw_list(app, &l.list, rows, count);
    if (l.detail_shown) draw_detail(app, &l.detail, rows, count);
}

void hosts_overlay(struct app *app) {
    if (hosts_modal_open(app) && !native_onboarding_active(app)) native_setup_modal(app);
}

void hosts_status(struct app *app, char *out, size_t size) {
    struct host_row rows[MAX_HEADS];
    size_t count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    size_t hosts = kind_count(rows, count, HOST_ROW_HOST), setups = kind_count(rows, count, HOST_ROW_SETUP);
    if (app->notice[0] || native_onboarding_active(app)) return;
    if (hosts) text_append(out, size, "%s%zu remote host%s", dot(app), hosts, hosts == 1 ? "" : "s");
    if (setups) text_append(out, size, "%s%zu setup%s unfinished", dot(app), setups, setups == 1 ? "" : "s");
}

/* ---- keys ------------------------------------------------------------------------- */

const char *hosts_open_label(const struct app *app) {
    struct host_row row;
    if (!hosts_selected_row(app, &row)) return "open";
    return row.kind == HOST_ROW_SETUP ? "continue" : "open";
}

/* The setup the flow works on takes its row once the list has it. */
static void hosts_follow_setup(struct app *app) {
    struct host_row rows[MAX_HEADS];
    size_t count, i;
    if (!flow_shown(app)) return;
    count = hosts_rows(app, rows, sizeof(rows) / sizeof(rows[0]));
    for (i = 0; i < count; i++)
        if (rows[i].kind == HOST_ROW_SETUP && !strcmp(app->setup->records[rows[i].index].name, app->setup->name)) {
            app->host_selected = i;
            return;
        }
}

void hosts_move(struct app *app, int direction) {
    size_t count = hosts_row_count(app);
    if (flow_shown(app)) native_setup_close(app);
    if (app->host_selected >= count) app->host_selected = 0;
    else if (direction > 0 && app->host_selected + 1 < count) app->host_selected++;
    else if (direction < 0 && app->host_selected) app->host_selected--;
}

/* Follows work in Work, filtered to where it runs; Esc there comes back. */
static void follow(struct app *app, const char *location) {
    struct native_hosts *h = hosts_state(app);
    if (!h) return;
    copy_text(h->location, sizeof(h->location), app->location);
    h->jumped = true;
    location_set(app, location);
    screen_jump(app, 7);
    work_focus_list(app);
    work_selection_changed(app);
}

bool hosts_jump_return(struct app *app) {
    struct native_hosts *h = app->hosts;
    if (!h || !h->jumped || app->jump_back != 7) return false;
    h->jumped = false;
    location_set(app, h->location);
    app->jump_back = 0;
    enter_view(app, 6);
    return true;
}

void hosts_open_row(struct app *app) {
    struct host_row row;
    if (!hosts_selected_row(app, &row)) return;
    if (row.kind == HOST_ROW_ADD) native_setup_open_form(app);
    else if (row.kind == HOST_ROW_MACHINE) follow(app, "local");
    else if (row.kind == HOST_ROW_HOST) follow(app, app->model.hosts[row.index].name);
    else if (flow_shown(app) && !strcmp(app->setup->name, app->setup->records[row.index].name)) hosts_focus_detail(app, true);
    else native_setup_resume(app, app->setup->records[row.index].name);
}

/* r on a host row: a fresh observation of the fleet, named for that host. */
void hosts_retry_row(struct app *app) {
    struct host_row row;
    if (hosts_selected_row(app, &row) && row.kind == HOST_ROW_HOST) remote_retry(app, app->model.hosts[row.index].name);
    else remote_retry(app, NULL);
}

static void filter_prompt(struct app *app) {
    char query[64];
    struct native_hosts *h = hosts_state(app);
    if (!h || prompt_text(app, "Filter hosts: ", query, sizeof(query)) != 0) return;
    copy_text(h->filter, sizeof(h->filter), query);
    app->host_selected = 0;
}

/* Tab: the list or the open setup. Below 70 columns the list's Tab opens
 * the detail in its place, as Enter does. */
static void pane_toggle(struct app *app) {
    struct native_hosts *h = hosts_state(app);
    if (!h) return;
    if (h->focus_detail || !flow_shown(app)) { h->focus_detail = false; h->detail_open = !h->detail_open && app->cols < STACKED; return; }
    h->focus_detail = true;
}

static void edit_or_remove(struct app *app, enum key_action action) {
    size_t record;
    if (detail_focused(app)) { native_setup_perform(app, action); return; }
    if (!hosts_setup_selected(app, &record)) return;
    if (action == KEY_HOSTS_EDIT) native_setup_edit(app, record);
    else native_setup_remove(app, record);
}

bool hosts_applies(const struct app *app, enum key_action action, bool *applies) {
    size_t record;
    if (action < KEY_HOSTS_UP || action > KEY_HOSTS_LAST) return false;
    if (action >= KEY_SETUP_UP) { *applies = native_setup_applies(app, action); return true; }
    switch (action) {
    case KEY_HOSTS_EDIT: case KEY_HOSTS_REMOVE:
        *applies = detail_focused(app) ? native_setup_applies(app, action) : hosts_setup_selected(app, &record);
        break;
    case KEY_HOSTS_RETRY: *applies = app->model.host_count || app->remote_stale; break;
    case KEY_HOSTS_PANE: case KEY_HOSTS_PANE_BACK: *applies = flow_shown(app) || app->cols < STACKED; break;
    default: *applies = true; break;
    }
    return true;
}

bool hosts_perform(struct app *app, enum key_action action) {
    if (action < KEY_HOSTS_UP || action > KEY_HOSTS_LAST) return false;
    if (action >= KEY_SETUP_UP) { native_setup_perform(app, action); return true; }
    switch (action) {
    case KEY_HOSTS_UP: case KEY_HOSTS_DOWN: hosts_move(app, action == KEY_HOSTS_UP ? -1 : 1); break;
    case KEY_HOSTS_OPEN: hosts_open_row(app); break;
    case KEY_HOSTS_ADD: native_setup_open_form(app); break;
    case KEY_HOSTS_EDIT: case KEY_HOSTS_REMOVE: edit_or_remove(app, action); break;
    case KEY_HOSTS_RETRY: hosts_retry_row(app); break;
    case KEY_HOSTS_FILTER: filter_prompt(app); break;
    case KEY_HOSTS_PANE: case KEY_HOSTS_PANE_BACK: pane_toggle(app); break;
    default: native_onboarding_open(app); break;   /* O and the page keys */
    }
    return true;
}

/* Esc: the open setup steps back, then the detail gives the list its place,
 * then the filter clears. */
bool hosts_back(struct app *app) {
    struct native_hosts *h = app->hosts;
    if (detail_focused(app)) { native_setup_perform(app, KEY_BACK); return true; }
    if (!h) return false;
    if (h->detail_open) { h->detail_open = false; return true; }
    if (h->filter[0]) { h->filter[0] = '\0'; app->host_selected = 0; return true; }
    return false;
}

/* The Onboarding page lets its list's [ and ] go back to Hosts, and the
 * digits change screens; every other key is the onboarding view's. */
bool hosts_onboarding_key(struct app *app, const struct kt_event *e) {
    const struct native_onboarding *o = app->onboarding;
    if (!o || o->screen != ONB_SCREEN_LIST || e->mods) return false;
    if (e->key == '[' || e->key == ']') { native_onboarding_close(app); return true; }
    if (e->key >= '1' && e->key <= '4') { native_onboarding_close(app); return key_dispatch(app, e); }
    return false;
}

void hosts_click(struct app *app, size_t row, unsigned button) {
    if (button == 64U || button == 65U) { hosts_move(app, button == 64U ? -1 : 1); return; }
    if (button != 0U) return;
    if (row != app->host_selected && flow_shown(app)) native_setup_close(app);
    app->host_selected = row;
    if (app->hosts) app->hosts->focus_detail = false;
}
