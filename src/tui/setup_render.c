#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "setup_flow.h"
/* Remote setup in the Hosts detail pane: the steps, and the current step's
 * screen under a rule of its own. Every value shown comes from the CLI
 * envelope; the fixed sentences explain the step and its guarantees in
 * plain language. Its form and approvals are modals (setup_modal.c). */

/* A scrolled, word-wrapped body between fixed title and footer rows. */
struct pen { struct app *app; size_t row, skip; int bottom; bool more; };

static void pen_emit(struct pen *p, int indent, struct kt_style tone, const char *text, size_t length) {
    char line[1024];
    struct app *app = p->app;
    if (p->row++ < p->skip) return;
    if (app->line >= p->bottom) { p->more = true; return; }
    snprintf(line, sizeof(line), "%.*s", (int)(length < sizeof(line) ? length : sizeof(line) - 1), text);
    column(app, indent, app->content_width - indent, tone, line);
    app->line++;
}

/* Bytes of text that fit in width columns, preferring to break at a space. */
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

/* A label column and a value wrapped with a hanging indent under itself;
 * an empty label wraps plain text at x. */
struct hanging { int x, width; const char *label; struct kt_style label_tone; };

static void pen_hanging(struct pen *p, const struct hanging *h, const char *value, struct kt_style tone) {
    int width = p->app->content_width - h->x - h->width;
    const char *rest = value;
    if (width < 8) width = 8;
    do {
        size_t take = wrap_length(rest, strlen(rest), width);
        if (p->row >= p->skip && p->app->line < p->bottom && rest == value && h->label[0])
            column(p->app, h->x, h->width, h->label_tone, h->label);
        pen_emit(p, h->x + h->width, tone, rest, take);
        /* wrap_length never exceeds strlen(rest), so rest stays within the
         * string and at worst reaches its terminator. */
        rest += take;
        while (*rest == ' ') rest++; // NOLINT(clang-analyzer-security.ArrayBound)
    } while (*rest);
}

static void pen_text(struct pen *p, int indent, struct kt_style tone, const char *text) {
    struct hanging h = {indent, 0, "", KT_BASE};
    pen_hanging(p, &h, text, tone);
}

static void pen_pair(struct pen *p, const char *label, const char *value, struct kt_style tone) {
    struct hanging h = {2, p->app->content_width < 70 ? 14 : 20, label, KT_MUTED};
    pen_hanging(p, &h, value, tone);
}

/* A step's screen opens under a rule titled with what it is. */
static void title(struct app *app, struct kt_style tone, const char *text) {
    if (app->line >= app->limit) return;
    kt_rule_styled(&app->frame, (struct kt_rect){app->content_x, app->line, app->content_width - 1, 1}, text, KT_ALIGN_LEFT,
                   tone, KT_BORDER);
    app->line++;
}

static void pen_begin(struct pen *p, struct app *app, int footer) {
    p->app = app; p->row = 0; p->skip = app->setup->scroll; p->more = false;
    p->bottom = app->limit - footer;
}

/* The pane's last rows: the scroll marker, then the screen's notice or
 * what its keys do. */
static void footer(struct app *app, struct pen *p, const char *prompt, struct kt_style tone) {
    struct native_setup *s = app->setup;
    s->more = p->more;
    app->line = app->limit - 2;
    if (p->more || p->skip) {
        column(app, 0, app->content_width, KT_MUTED, p->more ? (app->ascii ? "v more below: Down or PgDn scrolls" : "\xe2\x86\x93 more below: Down or PgDn scrolls")
                                                               : "Up or PgUp scrolls back");
    }
    app->line++;
    column(app, 0, app->content_width, s->notice[0] ? KT_WARNING : tone, s->notice[0] ? s->notice : prompt);
    app->line++;
}

/* ---- Steps ---- */

static struct kt_style step_tone(const char *status) {
    if (!strcmp(status, "done") || !strcmp(status, "running")) return KT_SUCCESS;
    if (!strcmp(status, "pending") || !strcmp(status, "skipped")) return KT_MUTED;
    return KT_WARNING;
}

/* A running step turns with the elapsed seconds. */
static const char *spinner(const struct app *app, const struct native_setup *s) {
    static const char *const unicode[] = {"\xe2\x97\x90", "\xe2\x97\x93", "\xe2\x97\x91", "\xe2\x97\x92"};
    static const char *const ascii[] = {"|", "/", "-", "\\"};
    long phase = (long)(time(NULL) - s->job_started) % 4;
    if (phase < 0) phase = 0;
    return app->ascii ? ascii[phase] : unicode[phase];
}

static void bar(struct app *app, bool selected, bool lit) {
    if (selected) column(app, 0, 1, lit ? KT_SELECTED : KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SURFACE, 0), app->ascii ? ">" : "\xe2\x96\x8c");
}

static void step_row(struct app *app, struct pen *p, const struct setup_step *step, bool selected, bool running) {
    const char *status = running ? "running" : step->status;
    char line[400], label[96];
    int width = app->content_width;
    bool shown = p->row >= p->skip && app->line < p->bottom;
    setup_step_label(step->id, label, sizeof(label));
    snprintf(line, sizeof(line), "%s %-24s %s", running ? spinner(app, app->setup) : setup_status_mark(status, app->ascii), label,
             setup_status_label(status));
    if (shown && selected) { column(app, 0, width, KT_SELECTED, ""); bar(app, true, true); }
    if (width >= 90) {
        char detail[300];
        snprintf(detail, sizeof(detail), "%-52.52s %s", line, step->detail);
        pen_emit(p, 1, selected ? KT_SELECTED : step_tone(status), detail, strlen(detail));
        return;
    }
    pen_emit(p, 1, selected ? KT_SELECTED : step_tone(status), line, strlen(line));
    if (step->detail[0]) pen_text(p, 5, KT_MUTED, step->detail);
}

/* The action row: what is running, or the selectable Continue setup. */
static void steps_action(struct app *app, const struct native_setup *s, bool running) {
    char work[200], label[96];
    if (!running) {
        if (!s->selected) { column(app, 0, app->content_width, KT_SELECTED, ""); bar(app, true, true); }
        column(app, 2, app->content_width - 2, s->selected ? KT_STRONG : KT_SELECTED, "Continue setup");
        app->line++;
        return;
    }
    setup_running_label(s->job_step, label, sizeof(label));
    clock_mark();
    snprintf(work, sizeof(work), "%s %s%s %lds", spinner(app, s), label, app->ascii ? "..." : "\xe2\x80\xa6", (long)(time(NULL) - s->job_started));
    column(app, 0, app->content_width, KT_SUCCESS, work);
    app->line++;
}

static void intro(struct app *app, const struct native_setup *s) {
    char text[300];
    snprintf(text, sizeof(text), "Setting up %.120s. Each step shows its plan before anything changes.", s->name);
    paragraph(app, text, KT_BASE);
    if (app->line < app->limit) app->line++;
}

static void render_steps(struct app *app, struct native_setup *s, bool running) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    size_t i;
    bool selectable = !running && s->screen == SETUP_SCREEN_STEPS;
    intro(app, s);
    steps_action(app, s, running);
    if (app->line < app->limit) app->line++;
    pen_begin(&p, app, 2);
    for (i = 0; i < e->step_count; i++)
        step_row(app, &p, &e->steps[i], selectable && s->selected == i + 1, running && !strcmp(e->steps[i].id, s->job_step));
    if (!e->step_count) pen_text(&p, 2, KT_MUTED, running ? "Contacting the machine..." : "No steps recorded yet.");
    footer(app, &p, !running ? "Continue setup runs the next step; changes ask for approval."
                    : s->job.cancelled ? "Stopping the step; Hydra then rereads the recorded setup status."
                    : "c cancels (a rerun continues safely); b keeps it running in the background.", KT_MUTED);
}

/* Above a step's own screen, when the pane has room: the steps, one row
 * each, the open one marked. */
static void steps_overview(struct app *app, const struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    const struct setup_step *open = setup_open_step(e);
    size_t i;
    if (!e->step_count || app->limit - app->line < (int)e->step_count + 20) return;
    intro(app, s);
    for (i = 0; i < e->step_count; i++) {
        char line[200], label[96];
        setup_step_label(e->steps[i].id, label, sizeof(label));
        snprintf(line, sizeof(line), "%s %-24s %s", setup_status_mark(e->steps[i].status, app->ascii), label, setup_status_label(e->steps[i].status));
        bar(app, &e->steps[i] == open, false);
        column(app, 1, app->content_width - 1, step_tone(e->steps[i].status), line);
        app->line++;
    }
    app->line++;
}

/* ---- Problems ---- */

/* The CLI's own ssh-keygen -R command from its recovery text, or one built
 * from the reported host and file. Shown for the user to run; never run. */
static void removal_command(const struct native_setup *s, char *out, size_t size) {
    const struct setup_envelope *e = s->current;
    const char *host = e->key_host[0] ? e->key_host : strrchr(e->destination, '@') ? strrchr(e->destination, '@') + 1 : e->destination;
    const char *start = strstr(e->recovery, "ssh-keygen -R"), *end;
    if (start) {
        end = strstr(start, ", then");
        snprintf(out, size, "%.*s", (int)(end ? (size_t)(end - start) : strlen(start)), start);
        return;
    }
    snprintf(out, size, "ssh-keygen -R %.200s%s%.300s", host, e->key_file[0] ? " -f " : "", e->key_file);
}

static void render_key_changed(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct setup_explanation x;
    struct pen p;
    char command[800];
    setup_explain("host_key_changed", &x);
    title(app, KT_WARNING, "The host key changed: Hydra will not connect");
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_BASE, x.body);
    pen_text(&p, 0, KT_BASE, "");
    if (e->presented[0]) pen_pair(&p, "Presented key", e->presented, KT_STRONG);
    if (e->key_file[0]) pen_pair(&p, "Old key in", e->key_file, KT_BASE);
    pen_text(&p, 0, KT_BASE, "");
    pen_text(&p, 0, KT_STRONG, "What you can do");
    pen_text(&p, 2, KT_BASE, "1. Confirm the new key with whoever runs the machine, over a channel other than this connection.");
    pen_text(&p, 2, KT_BASE, "2. Only if the change is expected, remove the old entry yourself:");
    removal_command(s, command, sizeof(command));
    pen_text(&p, 6, KT_STRONG, command);
    pen_text(&p, 2, KT_BASE, "3. Press Enter to check again; Hydra then shows the new key for your review.");
    if (e->message[0]) { pen_text(&p, 0, KT_BASE, ""); snprintf(command, sizeof(command), "Hydra says: %s", e->message); pen_text(&p, 0, KT_MUTED, command); }
    footer(app, &p, "Hydra never accepts a changed key; there is no option to do so here.", KT_MUTED);
}

static void requirement_row(struct app *app, struct pen *p, const struct setup_requirement *r) {
    const char *status = r->blocking && strcmp(r->status, "ok") ? "BLOCKS" : r->status;
    struct kt_style tone = !strcmp(r->status, "ok") ? KT_SUCCESS : KT_WARNING;
    char label[96], fix[300];
    bool narrow = app->content_width < 70;
    struct hanging h = {2, narrow ? 8 : 25, label, tone};
    snprintf(label, sizeof(label), narrow ? "%.7s" : "%-8s %.15s", status, r->name);
    if (narrow) { pen_text(p, 2, tone, r->name); h.label = status; }
    pen_hanging(p, &h, r->detail[0] ? r->detail : "-", narrow ? KT_MUTED : KT_BASE);
    if (r->command[0]) { snprintf(fix, sizeof(fix), "fix: %s", r->command); h.label = ""; pen_hanging(p, &h, fix, KT_STRONG); }
}

/* A guided run stopped here because heads cannot run on the host: say so,
 * list the rows that are not ok first, and let Enter continue anyway. */
static void render_paused(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[300];
    size_t i;
    snprintf(text, sizeof(text), "Heads cannot run on %.120s yet", s->name);
    title(app, KT_WARNING, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_BASE, "This machine has no tmux 3.0 or newer, so Hydra cannot run heads there. Setup can still "
                             "finish; install tmux yourself (Hydra never uses sudo) before starting work on it.");
    pen_text(&p, 0, KT_BASE, "");
    pen_text(&p, 2, KT_MUTED, app->content_width < 70 ? "STATUS  REQUIREMENT" : "STATUS   REQUIREMENT     DETAIL");
    for (i = 0; i < e->requirement_count; i++) if (strcmp(e->requirements[i].status, "ok")) requirement_row(app, &p, &e->requirements[i]);
    for (i = 0; i < e->requirement_count; i++) if (!strcmp(e->requirements[i].status, "ok")) requirement_row(app, &p, &e->requirements[i]);
    footer(app, &p, "Enter continues setup anyway   Esc back", KT_STRONG);
}

static void render_preflight(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[300];
    size_t i;
    bool blocked = setup_preflight_blocked(e);
    if (s->paused) { render_paused(app, s); return; }
    snprintf(text, sizeof(text), "%s on %.120s", blocked ? "Missing requirements" : "Requirements", s->name);
    title(app, blocked ? KT_WARNING : KT_STRONG, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_BASE, blocked ? "Setup cannot continue until the items marked BLOCKS are installed. Hydra never uses sudo: "
                                       "run the suggested commands on the machine yourself, then check again."
                                     : "Nothing blocks setup. Warnings are worth fixing but do not stop it.");
    pen_text(&p, 0, KT_BASE, "");
    pen_text(&p, 2, KT_MUTED, app->content_width < 70 ? "STATUS  REQUIREMENT" : "STATUS   REQUIREMENT     DETAIL");
    for (i = 0; i < e->requirement_count; i++) requirement_row(app, &p, &e->requirements[i]);
    footer(app, &p, blocked ? "Enter checks again after you install them   Esc back" : "Enter continues setup   Esc back", KT_STRONG);
}

static const char *agent_status(const char *status) {
    static const char *const names[][2] = {
        {"on_path", "on PATH"}, {"found_off_path", "found outside PATH"}, {"recorded", "recorded location"},
        {"missing", "not installed"}, {"ambiguous", "several copies found"},
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(status, names[i][0])) return names[i][1];
    return status;
}

/* Fixed action rows: Continue setup, then one row per installer choice. */
static void choice_text(const struct app *app, const struct setup_choice *c, bool selected, char *out, size_t size) {
    const char *mark = selected ? (app->ascii ? ">" : "\xe2\x96\x8c") : " ";
    if (!c) snprintf(out, size, "%s Continue setup", mark);
    else if (c->install) snprintf(out, size, "%s Install %.60s (you review the installer first)", mark, c->agent);
    else snprintf(out, size, "%s Sign in to %.60s", mark, c->agent);
}

static void agent_actions(struct app *app, const struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    char line[200];
    size_t i;
    for (i = 0; i <= e->choice_count && app->line < app->limit - 4; i++) {
        struct kt_style tone = s->selected == i ? KT_SELECTED : i ? KT_BASE : KT_STRONG;
        choice_text(app, i ? &e->choices[i - 1] : NULL, s->selected == i, line, sizeof(line));
        if (s->selected == i) column(app, 0, app->content_width, KT_SELECTED, "");
        column(app, 0, app->content_width, tone, line);
        app->line++;
    }
}

static void render_agents(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[600];
    size_t i;
    snprintf(text, sizeof(text), "Agents on %.120s", s->name);
    title(app, KT_STRONG, text);
    paragraph(app, "Choose an agent to install or sign in to, or continue setup without one. Installers ask for your approval; "
              "sign-in runs in this terminal.", KT_BASE);
    agent_actions(app, s);
    linef(app, "");
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_MUTED, "Found on the remote PATH and in common per-user install directories:");
    for (i = 0; i < e->agent_count; i++) {
        const struct setup_agent *a = &e->agents[i];
        snprintf(text, sizeof(text), "%-12s %-20s %s%s%s", a->name, agent_status(a->status), a->path, a->version[0] ? "  " : "", a->version);
        pen_text(&p, 2, !strcmp(a->status, "missing") || !strcmp(a->status, "ambiguous") ? KT_WARNING : KT_BASE, text);
    }
    if (!e->agent_count) pen_text(&p, 2, KT_MUTED, "No agent programs were reported.");
    footer(app, &p, "Up/Down choose   Enter run   PgDn more agents   Esc back", KT_STRONG);
}

static void render_handoff(struct app *app, struct native_setup *s) {
    struct pen p;
    char text[1200], label[96];
    size_t i;
    bool sign_in = !strncmp(s->handoff_step, "sign_in:", 8);
    setup_step_label(s->handoff_step, label, sizeof(label));
    snprintf(text, sizeof(text), "%.90s on %.120s", label, s->name);
    title(app, KT_STRONG, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_BASE, sign_in ? "The provider's sign-in asks questions or shows a link to open in your browser. Hydra hands this "
                                       "terminal to it. Credentials stay on the remote; Hydra copies nothing."
                                     : "The installer runs in this terminal so you can answer it.");
    pen_text(&p, 0, KT_BASE, "When it succeeds you return here at once; after a failure Hydra waits for Enter so you can read "
                             "the output.");
    pen_text(&p, 0, KT_BASE, "");
    text[0] = '\0';
    for (i = 0; i < s->handoff_argc; i++) text_append(text, sizeof(text), "%s%s", i ? " " : "", s->handoff[i]);
    pen_pair(&p, "Command", text, KT_STRONG);
    footer(app, &p, "Enter hands over the terminal   Esc later", KT_STRONG);
}

static void render_problem(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct setup_explanation x;
    struct pen p;
    char text[800];
    setup_explain(e->code, &x);
    title(app, KT_WARNING, x.title);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_BASE, x.body);
    pen_text(&p, 0, KT_BASE, "");
    if (e->message[0]) { snprintf(text, sizeof(text), "Hydra says: %s", e->message); pen_text(&p, 0, KT_BASE, text); }
    if (e->recovery[0]) { snprintf(text, sizeof(text), "Suggested: %s", e->recovery); pen_text(&p, 0, KT_BASE, text); }
    snprintf(text, sizeof(text), "code %s%sexit %d", e->code, dot(app), e->exit_status);
    pen_text(&p, 0, KT_MUTED, text);
    snprintf(text, sizeof(text), "%s   Esc back", x.action ? x.action : "Nothing to retry here");
    footer(app, &p, text, x.action ? KT_STRONG : KT_MUTED);
}

static void render_done(struct app *app, struct native_setup *s) {
    const struct setup_envelope *e = s->current;
    struct pen p;
    char text[600];
    size_t i;
    snprintf(text, sizeof(text), "%.120s is ready", s->name);
    title(app, KT_SUCCESS, text);
    pen_begin(&p, app, 2);
    snprintf(text, sizeof(text), "Hydra added %.120s (%.200s) as a remote host.", s->name, e->destination[0] ? e->destination : s->destination);
    pen_text(&p, 0, KT_BASE, text);
    pen_text(&p, 0, KT_BASE, "");
    for (i = 0; i < e->step_count; i++) step_row(app, &p, &e->steps[i], false, false);
    pen_text(&p, 0, KT_BASE, "");
    pen_text(&p, 0, KT_BASE, "Follow and start work on it with hydra fleet tui.");
    footer(app, &p, "Enter or Esc closes", KT_STRONG);
}

/* ---- A name that already has an unfinished setup ---- */

static void render_duplicate(struct app *app, struct native_setup *s) {
    const struct setup_record *r = NULL;
    struct pen p;
    char text[1400];
    size_t i;
    for (i = 0; i < s->record_count; i++) if (!strcmp(s->records[i].name, s->duplicate)) r = &s->records[i];
    snprintf(text, sizeof(text), "%.120s already has an unfinished setup", s->duplicate);
    title(app, KT_WARNING, text);
    pen_begin(&p, app, 2);
    pen_text(&p, 0, KT_BASE, "Hydra keeps one setup per name, so this form would fail. Continue that setup, change it, or go back "
             "and choose another name.");
    pen_text(&p, 0, KT_BASE, "");
    pen_pair(&p, "Destination", r ? r->destination : "", KT_BASE);
    pen_pair(&p, "SSH config", r && r->config[0] ? r->config : "your usual SSH config", KT_BASE);
    pen_pair(&p, "Progress", r ? r->summary : "", KT_BASE);
    pen_text(&p, 0, KT_BASE, "");
    pen_text(&p, 0, KT_BASE, r && r->remote_changed
             ? "It already changed the remote, so it cannot be edited: x removes its record (the plan lists what stays there)."
             : "e edits it (nothing changed on the remote yet); x removes its record.");
    footer(app, &p, "Enter continues that setup   e edit   x remove   Esc back to the form", KT_STRONG);
}

/* The flow in the Hosts detail pane (app->content_* and line..limit). A
 * modal's screen shows the steps under it. */
void native_setup_render(struct app *app) {
    struct native_setup *s = app->setup;
    if (s->screen != SETUP_SCREEN_STEPS && s->screen != SETUP_SCREEN_RUNNING && s->screen != SETUP_SCREEN_TRUST_KEY &&
        s->screen != SETUP_SCREEN_PLAN && s->screen != SETUP_SCREEN_FORM)
        steps_overview(app, s);
    switch (s->screen) {
        case SETUP_SCREEN_RUNNING: render_steps(app, s, true); break;
        case SETUP_SCREEN_KEY_CHANGED: render_key_changed(app, s); break;
        case SETUP_SCREEN_PREFLIGHT: render_preflight(app, s); break;
        case SETUP_SCREEN_AGENTS: render_agents(app, s); break;
        case SETUP_SCREEN_HANDOFF: render_handoff(app, s); break;
        case SETUP_SCREEN_ERROR: case SETUP_SCREEN_UNKNOWN: render_problem(app, s); break;
        case SETUP_SCREEN_DONE: render_done(app, s); break;
        case SETUP_SCREEN_DUPLICATE: render_duplicate(app, s); break;
        default: render_steps(app, s, false); break;
    }
}
