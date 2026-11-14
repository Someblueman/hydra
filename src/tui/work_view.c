#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Drawing Work: each pane starts with a titled rule (accent while it has
 * focus) and has no box; a pane with pages shows them as a strip in its
 * rule. The status line says what the selection is doing, or that keys go
 * to the agent; the footer is generated from the focused scope chain. */

/* A pane's titled rule: the title (accent while the pane has focus), the
 * line, and a muted note at its right end. */
/* The note at the end of a rule whose title takes used columns; left out
 * when the two would meet. */
static void rule_note(struct app *app, struct kt_rect r, int used, const char *note) {
    int length;
    if (!note || !note[0]) return;
    length = kt_text_width(note);
    if (used + length + 4 > r.width - 1) return;
    kt_fill(&app->frame, (struct kt_rect){r.x + r.width - length - 2, r.y, length + 1, 1}, KT_BASE);
    kt_text(&app->frame, (struct kt_rect){r.x + r.width - length - 1, r.y, length, 1}, note, KT_MUTED);
}

void work_rule(struct app *app, struct kt_rect r, const char *title, bool focused, const char *note) {
    if (r.width < 4 || r.height < 1) return;
    pane_rule_styled(&app->frame, (struct kt_rect){r.x, r.y, r.width, 1}, title, focused);
    rule_note(app, r, title && title[0] ? kt_text_width(title) + 1 : 0, note);
}

static const char *const page_names[WORK_PAGES] = {"Conversation", "Details", "Plan", "Run"};

/* Whether a page has a place in the strip: it applies, and when a focused
 * pane's strip and note do not both fit, only the shown page stays (the note
 * says where keys go; [ and ] still reach the others). */
static bool strip_page(struct app *app, int page, int shown, bool only_shown) {
    return work_page_available(app, page) && (!only_shown || page == shown);
}

/* A rule whose title is the strip of the pages that apply: the shown page
 * bold (accent with focus), the others muted. */
/* The strip's text for a rule width; false when only the shown page (or,
 * narrower still, no page: *shown becomes -1) leaves room for the note. */
static bool strip_text(struct app *app, int first, int *shown, const char *note, int width, char *strip, size_t size) {
    int page;
    strip[0] = '\0';
    for (page = first; page < WORK_PAGES; page++)
        if (strip_page(app, page, *shown, false)) text_append(strip, size, "%s%s", strip[0] ? "  " : "", page_names[page]);
    if (!note || !note[0] || kt_text_width(strip) + kt_text_width(note) + 5 <= width - 1) return true;
    copy_text(strip, size, page_names[*shown]);
    if (kt_text_width(strip) + kt_text_width(note) + 5 > width - 1) { strip[0] = '\0'; *shown = -1; }
    return false;
}

static void pages_rule(struct app *app, struct kt_rect r, bool shared, int shown, bool focused, const char *note) {
    char strip[64];
    int x = r.x, page, first = shared ? WORK_CONVERSATION : WORK_DETAILS;
    bool only_shown = !strip_text(app, first, &shown, focused ? note : NULL, r.width, strip, sizeof(strip));
    work_rule(app, r, strip, focused, NULL);
    for (page = first; page < WORK_PAGES; page++) {
        int length = (int)strlen(page_names[page]);
        if (!strip_page(app, page, shown, only_shown)) continue;
        if (x + length < r.x + r.width - 1)
            kt_text(&app->frame, (struct kt_rect){x, r.y, length, 1}, page_names[page], page == shown ? (focused ? KT_FOCUS : KT_STRONG) : KT_MUTED);
        work_page_hit(app, page, x, r.y, length);
        x += length + 2;
    }
    rule_note(app, r, kt_text_width(strip) + 1, note);
}

static struct kt_rect pane_body(struct kt_rect r) {
    return (struct kt_rect){r.x, r.y + 1, r.width - 1, r.height - 1};
}

static const char *conversation_title(struct app *app) {
    const struct head *h = work_output_head(app);
    const struct task_observation *task = selected_task(app);
    if (task) return task->task_id;
    return h ? h->branch : "Conversation";
}

/* The conversation pane, or on a narrow screen the pane that shows the
 * conversation and the side pane's pages. */
static void draw_main(struct app *app, struct kt_rect r, bool focused) {
    struct native_workspace *w = app->workspace;
    char note[TEXT];
    int page = w->merged || w->compact ? work_page_shown(app, false) : WORK_CONVERSATION;
    work_conversation_note(app, focused, note, sizeof(note));
    if (w->merged || w->compact) {
        pages_rule(app, r, true, page, focused, page == WORK_CONVERSATION ? note : NULL);
        if (page != WORK_CONVERSATION) { work_page_draw(app, page, pane_body(r), focused); return; }
    } else work_rule(app, r, conversation_title(app), focused, note);
    work_conversation_draw(app, pane_body(r), focused);
}

static void draw_side(struct app *app, struct kt_rect r, bool focused) {
    int page = work_page_shown(app, true);
    pages_rule(app, r, false, page, focused, NULL);
    work_page_draw(app, page, pane_body(r), focused);
}

static void draw_pane(struct app *app, int pane) {
    struct native_workspace *w = app->workspace;
    struct kt_rect r = w->layout.panes[pane].bounds;
    bool focused = w->layout.focus == pane;
    if (r.width < 4 || r.height < 2) return;
    if (pane == WORK_PANE_LIST) work_list_draw(app, r, focused);
    else if (pane == WORK_PANE_CONVERSATION) draw_main(app, r, focused);
    else if (pane == WORK_PANE_SIDE) draw_side(app, r, focused);
    else if (w->agents.first && pane >= w->agents.first && pane <= w->agents.first + 1) {
        size_t slot = w->agents.slots[pane - w->agents.first];
        work_agent_draw(app, r, app->terminals && slot < NATIVE_TERMINALS ? &app->terminals->slots[slot] : NULL, focused);
    }
}

/* Nothing in the project yet: what a head is and the three ways forward. */
static bool work_empty(const struct app *app) {
    return !app->model.head_count && !app->model.task_count && !(app->workflows && app->workflows->run_count) && !app->plan &&
        !app->search[0] && !app->location[0] && (app->snapshot_at || app->snapshot_stale);
}

static void draw_empty(struct app *app, struct kt_rect r) {
    static const char *const lines[] = {
        "A head is one task: its own branch, worktree, terminal and agent.",
        "Hydra creates them; you talk to each agent here and approve what it does."};
    const char *keys = app->ascii ? "n start a task / 3 add a remote host / : all commands" :
        "n start a task \xe2\x80\xa2 3 add a remote host \xe2\x80\xa2 : all commands";
    int y = r.y + r.height / 2 - 4, i;
    if (y < r.y) y = r.y;
    kt_text_aligned(&app->frame, (struct kt_rect){r.x, y, r.width, 1}, app->ascii ? "<>" : "\xe2\x97\x87", KT_ALIGN_CENTER, KT_TITLE);
    kt_text_aligned(&app->frame, (struct kt_rect){r.x, y + 2, r.width, 1}, "No agent work in this project yet", KT_ALIGN_CENTER, KT_STRONG);
    for (i = 0; i < 2; i++) kt_text_aligned(&app->frame, (struct kt_rect){r.x, y + 4 + i, r.width, 1}, lines[i], KT_ALIGN_CENTER, KT_BASE);
    kt_text_aligned(&app->frame, (struct kt_rect){r.x, y + 7, r.width, 1}, keys, KT_ALIGN_CENTER, KT_MUTED);
}

/* ---- the status line ------------------------------------------------------ */

/* Without a notice the status line says what the selection is doing. */
static void selection_context(struct app *app, char *out, size_t size) {
    const struct head *h = selected_head(app);
    const struct task_observation *task = selected_task(app);
    char text[TEXT * 2];
    if (app->run_row && app->workflows && app->workflow_run < app->workflows->run_count) {
        run_summary_text(app, app->workflow_run, text, sizeof(text));
        text_append(out, size, "%s%s", dot(app), text);
    } else if (task) text_append(out, size, "%stask %.100s on %.60s%s%s", dot(app), task->task_id, task->host, dot(app), task_status(app, task));
    else if (h) text_append(out, size, "%s%.120s%s%s", dot(app), h->branch, dot(app),
                            work_head_plan_ready(app, h) ? "a plan is ready for your review" : head_list_status(app, h));
}

/* While typing the status line keeps its freshness chip, then says where
 * keys go (or answers the last action). */
static void typing_status(struct app *app, char *out, size_t size, bool headless) {
    const struct head *h = selected_head(app);
    char leave[64] = "Ctrl-]", fresh[96];
    (void)key_text(app, KEY_TERMINAL_LEAVE, leave, sizeof(leave));
    (void)snapshot_freshness(app, app->snapshot_at, false, headless, fresh, sizeof(fresh));
    if (app->notice[0]) snprintf(out, size, "%s%s%s", fresh, dot(app), app->notice);
    else snprintf(out, size, "%s%sEvery key goes to %.40s in %.120s except %s and Hydra's chords", fresh, dot(app),
                  h ? native_agent_name(h) : "the agent", h ? h->branch : "its head", leave);
    if (app->workspace && app->workspace->zoom) {
        char zoom[64] = "Ctrl-\\ z";
        (void)key_text(app, KEY_WORKSPACE_ZOOM, zoom, sizeof(zoom));
        text_append(out, size, "%s%s restores the panes", dot(app), zoom);
    }
}

static void work_status(struct app *app, char *out, size_t size, struct kt_style *tone, bool headless) {
    bool plain = !app->notice[0] && !app->marked_count && !app->search[0] && !app->snapshot_stale && !app->location[0];
    /* While typing, a stale snapshot still says so (as every screen does). */
    if (native_terminal_focused(app) && !app->snapshot_stale) { typing_status(app, out, size, headless); *tone = KT_FOCUS; return; }
    screen_status(app, out, size, tone, headless);
    if (plain) selection_context(app, out, size);
    if (app->workspace && app->workspace->zoom && !app->notice[0]) text_append(out, size, "%sz restores the panes", dot(app));
}

/* ---- the frame ------------------------------------------------------------ */

/* What the frame shows is decided before it is drawn: a run chosen only
 * because nothing else was listed gives way to the first head; with nothing
 * selectable but runs, the first run is the selection; and --view overview,
 * asked before the runs were read, opens on the run it names once they are. */
static void frame_selection(struct app *app, struct native_workspace *w) {
    if (w->run_auto && app->run_row && app->model.head_count) { app->run_row = false; w->run_auto = false; }
    retarget_selection(app);
    if (!app->run_row && !selected_head(app) && !selected_task(app)) {
        outline_move(app, 1, false);
        w->run_auto = app->run_row;
    }
    if (w->chosen == WORK_RUN && !w->chosen_for[0] && !work_page_available(app, WORK_RUN) && overview_run(app) != SIZE_MAX) {
        app->run_row = true; app->task_row = false; app->workflow_run = overview_run(app);
    }
}

/* The status line and footer; while typing, the footer is the chords and a
 * short screen (tight) gives the status line to the agent. */
static void frame_footer(struct app *app, bool headless, bool typing, bool tight) {
    char status[768];
    struct kt_style tone;
    struct kt_canvas *c = &app->frame;
    work_status(app, status, sizeof(status), &tone, headless);
    if (!typing) { chrome_footer(app, c, status, tone); return; }
    if (!tight) chrome_status(app, c, status, tone);
    key_terminal_footer(app, c, (struct kt_rect){1, c->height - 1, c->width - 2, 1});
}

bool work_render(struct app *app, unsigned frame, bool headless) {
    struct native_workspace *w;
    struct kt_canvas *c;
    struct kt_rect area;
    int top, i;
    bool typing, tight;
    if (!native_workspace_init(app) || !frame_begin(app, headless)) return false;
    w = app->workspace; c = &app->frame;
    frame_selection(app, w);
    work_frame_mode(app);
    top = app->rows >= 30 ? 2 : 1;
    /* Typing on a short screen gives the status line to the agent: its
     * rule says where keys go, the footer how to leave. */
    typing = native_terminal_focused(app);
    tight = typing && c->height < 12;
    chrome_header(app, c, top > 1);
    area = (struct kt_rect){1, top, c->width - 2, c->height - top - (tight ? 1 : 2)};
    work_page_hits_clear(app);
    if (work_empty(app)) draw_empty(app, area);
    else {
        work_arrange(app, area);
        for (i = 1; i < w->layout.count; i++) if (w->layout.panes[i].split == KT_LEAF) draw_pane(app, i);
    }
    app->limit = app->rows;
    frame_footer(app, headless, typing, tight);
    frame_end(app, frame, headless);
    return true;
}
