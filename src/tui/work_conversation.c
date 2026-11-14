#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Work's conversation: the selected head's agent. Not attached, it shows the
 * head's latest output read-only (one capture of that head per refresh); a
 * headless head shows its step's output and a run row the output of the head
 * running its current step. Attached, it is the agent's own screen, live,
 * and with focus every key goes to the agent but Hydra's chords. */

/* The head whose output the conversation shows: the selected head, or the
 * local head running (or last running) the selected run's current step. */
const struct head *work_output_head(struct app *app) {
    const struct workflow_model *m = app->workflows;
    const struct head *found = NULL;
    size_t i;
    if (!app->run_row) return selected_head(app);
    if (!m || app->workflow_run >= m->run_count) return NULL;
    for (i = 0; i < m->node_count; i++) {
        const struct head *h;
        if (m->nodes[i].run != app->workflow_run || !m->nodes[i].head[0]) continue;
        h = head_for_branch(app, m->nodes[i].head);
        if (!h || head_remote(h)) continue;
        found = h;
        if (!strcmp(m->nodes[i].state, "running")) break;
    }
    return found;
}

/* The selected head's attached client, connected or not. */
struct native_terminal *work_terminal(struct app *app) {
    const struct head *h = app->run_row ? NULL : selected_head(app);
    return h ? native_terminal_any_for_head(app, h) : NULL;
}

/* Whether a refresh reads the conversation's preview: Work is shown and its
 * conversation is not the live agent. */
bool work_preview_wanted(struct app *app) {
    const struct head *h;
    if (app->view != 7 || app->help || app->result_open) return false;
    h = work_output_head(app);
    return h && !head_remote(h) && !work_terminal(app);
}

/* ---- the header line ------------------------------------------------------ */

static void header_local(struct app *app, const struct head *h, char *out, size_t size) {
    const char *sep = dot(app);
    char agent[TEXT + 96];
    head_agent_text(app, h, agent, sizeof(agent));
    if (head_headless(h)) {
        const char *role = head_run_role(app, h);
        snprintf(out, size, "%s%s%s%sheadless, no terminal", role ? role : "headless head", sep, agent, sep);
        return;
    }
    bool waiting = (h->gates > h->approved || work_head_plan_ready(app, h)) && !strcmp(display_status(h), "LIVE");
    snprintf(out, size, "%s%s%s", waiting ? "needs you" : status_label(h), sep, agent);
    if (!h->head_id[0]) { text_append(out, size, "%sno head record", sep); return; }
    if (h->gates) text_append(out, size, "%s%u of %u approvals", sep, h->approved, h->gates);
    if (h->diff) text_append(out, size, "%s%u file%s changed", sep, h->diff, h->diff == 1 ? "" : "s");
    if (work_head_plan_ready(app, h)) text_append(out, size, "%splan ready", sep);
    if (h->pr[0] && strcmp(h->pr, "-")) text_append(out, size, "%sPR %.40s", sep, h->pr);
}

/* State, agent, approvals and plan or PR, after the head's glyph. */
static struct kt_style header_text(struct app *app, const struct head *h, char *out, size_t size, const char **mark) {
    struct kt_style tone;
    *mark = work_head_glyph(app, h, &tone);
    if (head_remote(h))
        snprintf(out, size, "%s%son %.100s%sdesired %.40s", remote_head_status(app, h), dot(app), h->remote_host, dot(app),
                 h->desired[0] ? h->desired : "unknown");
    else header_local(app, h, out, size);
    return tone;
}

static void draw_header(struct app *app, struct kt_rect r, const struct head *h) {
    char text[TEXT * 2], fitted[TEXT * 2];
    const char *mark;
    struct kt_style tone = header_text(app, h, text, sizeof(text), &mark);
    fit_text(app, text, r.width > 3 ? (size_t)r.width - 2U : 1U, fitted, sizeof(fitted));
    kt_text(&app->frame, (struct kt_rect){r.x, r.y, 1, 1}, mark, tone);
    kt_text(&app->frame, (struct kt_rect){r.x + 2, r.y, r.width - 2, 1}, fitted, KT_MUTED);
}

/* ---- what the rule says --------------------------------------------------- */

/* The note at the right of the rule: whether this is output to read or the
 * live agent, and the key that changes it. */
/* The note of an attached client: whether keys reach it, or why not. */
static void terminal_note(struct app *app, const struct native_terminal *t, bool focused, char *out, size_t size) {
    char leave[64];
    if (t->client.finished || t->client.eof) { snprintf(out, size, "disconnected%sa reconnects", dot(app)); return; }
    /* tmux sized the window to a smaller client: its dotted fill lies
     * outside that window, and typing here takes the size back. */
    if (t->foreign_size) { snprintf(out, size, "sized by another client%styping resizes", dot(app)); return; }
    if (!focused) { snprintf(out, size, "live%stab here to type", dot(app)); return; }
    if (!key_text(app, KEY_TERMINAL_LEAVE, leave, sizeof(leave))) copy_text(leave, sizeof(leave), "leave");
    snprintf(out, size, "typing to %.40s%s%s leave", native_agent_name(selected_head(app)), dot(app), leave);
}

void work_conversation_note(struct app *app, bool focused, char *out, size_t size) {
    struct native_terminal *t = work_terminal(app);
    const struct head *h = work_output_head(app);
    if (t) { terminal_note(app, t, focused, out, size); return; }
    if (selected_task(app)) { snprintf(out, size, "remote task%sno terminal here", dot(app)); return; }
    if (h && head_remote(h)) { snprintf(out, size, "remote%senter attaches, no history here", dot(app)); return; }
    if (h && app->preview_back) { snprintf(out, size, "%zu rows back%spgdn returns", app->preview_back, dot(app)); return; }
    if (!h || head_headless(h) || app->run_row) { snprintf(out, size, "latest output%sread-only", dot(app)); return; }
    snprintf(out, size, "latest output%senter to talk%s", dot(app), app->snapshot_stale ? " (stale)" : "");
}

/* ---- the body ------------------------------------------------------------- */

/* The newest rows of the head's output that fit, scrolled back by PgUp. */
static void draw_preview(struct app *app, struct kt_rect r, const struct head *h) {
    struct transcript_layout layout;
    const char *text = app->preview_text[0] && !strcmp(app->preview_head, h->branch) ? app->preview_text :
        head_headless(h) ? "Reading the step output..." : preview_loading(app) ? "Reading the terminal output..." : "No terminal output yet.";
    int x = app->content_x, width = app->content_width, line = app->line, limit = app->limit;
    app->content_x = r.x; app->content_width = r.width; app->line = r.y; app->limit = r.y + r.height - 1;
    memset(&layout, 0, sizeof(layout)); layout.follow = true; layout.back = app->preview_back;
    transcript_view(app, text, strlen(text), &layout);
    if (!layout.hints) {
        /* Without provider key hints the note row goes back to the output. */
        app->line = r.y; app->limit = r.y + r.height;
        memset(&layout, 0, sizeof(layout)); layout.follow = true; layout.back = app->preview_back;
        transcript_view(app, text, strlen(text), &layout);
    } else kt_text(&app->frame, (struct kt_rect){r.x, r.y + r.height - 1, r.width, 1},
                   "Key hints above belong to the agent's live input, not this view; enter opens it.", KT_MUTED);
    app->preview_back = layout.back;
    app->content_x = x; app->content_width = width; app->line = line; app->limit = limit;
}

static void draw_lines(struct app *app, struct kt_rect r, const char *text, struct kt_style tone) {
    statistics_wrap(&app->frame, r.x, r.y, r.width, r.height, tone, text);
}

/* A remote head without a client here: what its host last said. */
static void draw_remote(struct app *app, struct kt_rect r, const struct head *h) {
    char text[TEXT * 2], refusal[TEXT], observation[TEXT];
    int used;
    remote_observation_text(app, h, observation, sizeof(observation));
    snprintf(text, sizeof(text), "observation %s", observation);
    used = statistics_wrap(&app->frame, r.x, r.y, r.width, r.height, remote_head_tone(app, h), text) + 1;
    r.y += used; r.height -= used;
    if (r.height < 1) return;
    if (remote_action_refusal(app, h, refusal, sizeof(refusal))) { draw_lines(app, r, refusal, KT_WARNING); return; }
    snprintf(text, sizeof(text), "This head runs on %.100s. Enter or a attaches its terminal here through hydra fleet attach; "
             "the agent keeps running when you leave. The remote client has no history to scroll here.", h->remote_host);
    draw_lines(app, r, text, KT_BASE);
}

/* A remote task: what its receiver last said, then what can be done here. */
static void draw_task(struct app *app, struct kt_rect r, const struct task_observation *task) {
    char text[1024];
    int y = r.y, bottom = r.y + r.height;
    snprintf(text, sizeof(text), "task %.120s on %.60s", task->task_id, task->host);
    y += statistics_wrap(&app->frame, r.x, y, r.width, bottom - y, KT_STRONG, text);
    snprintf(text, sizeof(text), "%s%sowner %s", task_status(app, task), dot(app), task->owner);
    y += statistics_wrap(&app->frame, r.x, y, r.width, bottom - y, task_tone(app, task), text);
    snprintf(text, sizeof(text), "waiting %s%snext %s", task->waiting_reason, dot(app), task->next_action);
    y += statistics_wrap(&app->frame, r.x, y, r.width, bottom - y, KT_BASE, text) + 1;
    if (y < bottom)
        draw_lines(app, (struct kt_rect){r.x, y, r.width, bottom - y}, "A remote task has no terminal here; its controls "
                   "are Y approve, N reject, R resume and X cancel, each confirmed against a fresh observation.", KT_MUTED);
}

static void draw_run(struct app *app, struct kt_rect r) {
    char summary[TEXT + 160], next[1536];
    run_summary_text(app, app->workflow_run, summary, sizeof(summary));
    run_next_text(app, app->workflow_run, next, sizeof(next));
    kt_text(&app->frame, (struct kt_rect){r.x, r.y, r.width, 1}, summary, KT_STRONG);
    if (r.height > 2) draw_lines(app, (struct kt_rect){r.x, r.y + 2, r.width, r.height - 2}, next, KT_BASE);
}

static void draw_terminal(struct app *app, struct kt_rect r, struct native_terminal *t, bool focused) {
    struct kt_canvas content;
    if (kt_canvas_view(&content, &app->frame, r)) native_terminal_draw(app, t, &content, focused);
}

/* Attached: what the agent's terminal says of itself ("shell · session
 * running", "codex · exit recorded"). */
static void draw_attached_header(struct app *app, struct kt_rect r, const struct head *h, struct native_terminal *t) {
    char attention[TEXT + 64];
    struct kt_style tone;
    native_terminal_attention(app, t, attention, sizeof(attention));
    kt_text(&app->frame, (struct kt_rect){r.x, r.y, 1, 1}, work_head_glyph(app, h, &tone), tone);
    kt_text(&app->frame, (struct kt_rect){r.x + 2, r.y, r.width - 2, 1}, attention, KT_MUTED);
}

/* Rows above the body: the header and a blank row; a short pane gives the
 * blank row to the output, and a very short one the header too while an
 * agent is attached (its rule still says where keys go). */
static int header_rows(struct kt_rect r, const struct native_terminal *t) {
    if (t && r.height < 8) return 0;
    return r.height < 10 ? 1 : 2;
}

/* The conversation in r (its rule is drawn by the caller). */
void work_conversation_draw(struct app *app, struct kt_rect r, bool focused) {
    const struct task_observation *task = selected_task(app);
    struct native_terminal *t = work_terminal(app);
    const struct head *h = work_output_head(app);
    int gap = header_rows(r, t);
    struct kt_rect body = {r.x, r.y + gap, r.width, r.height - gap};
    if (r.height < 1) return;
    if (task) { draw_task(app, r, task); return; }
    if (app->run_row && !h) { draw_run(app, r); return; }
    if (!h) { draw_lines(app, r, "Select a head to see its agent here.", KT_MUTED); return; }
    if (gap && t) draw_attached_header(app, r, h, t);
    else if (gap) draw_header(app, r, h);
    if (body.height < 1) return;
    if (t) draw_terminal(app, body, t, focused);
    else if (head_remote(h)) draw_remote(app, body, h);
    else draw_preview(app, body, h);
}

/* One of two agents side by side: the terminal in that slot. */
void work_agent_draw(struct app *app, struct kt_rect r, struct native_terminal *t, bool focused) {
    char title[TEXT + 64], attention[TEXT + 64];
    if (!t || !t->screen) return;
    native_terminal_attention(app, t, attention, sizeof(attention));
    const char *note = focused && !t->client.finished && !t->client.eof ? "attached \xc2\xb7 typing" : "attached \xc2\xb7 live";
    snprintf(title, sizeof(title), "%.100s%s%.60s", t->label, dot(app), attention);
    /* A narrow pane keeps the note (where keys go) over the agent's state. */
    if (kt_text_width(title) + kt_text_width(note) + 5 > r.width - 1) snprintf(title, sizeof(title), "%.100s", t->label);
    work_rule(app, r, title, focused, note);
    draw_terminal(app, (struct kt_rect){r.x, r.y + 1, r.width - 1, r.height - 1}, t, focused);
}
