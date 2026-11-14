#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Facts for a head's Details page that come from what the control centre
 * already holds rather than from its snapshot rows: the objective the head
 * was started with (the task its launch wrote beside the head's record),
 * the run step that targets it and what that step waits on (the workflow
 * snapshot), and whether its agent has left the terminal (the line Hydra's
 * own launcher prints into the terminal, lib/tmux.sh). */

/* ---- the objective -------------------------------------------------------- */

/* The value after `prefix` at the start of a line of text, to its end. */
static bool line_value(const char *text, const char *prefix, char *out, size_t size) {
    const char *at = text;
    size_t length = strlen(prefix);
    while (at && *at) {
        if (!strncmp(at, prefix, length)) {
            at += length;
            snprintf(out, size, "%.*s", (int)strcspn(at, "\n"), at);
            return out[0] != '\0';
        }
        at = strchr(at, '\n');
        if (at) at++;
    }
    return false;
}

/* What a task file says the objective is: the Objective line of a task the
 * control centre started, the objective of its planning handoff, or else
 * the task's first paragraph as it was given. */
static void objective_of(const char *task, char *out, size_t size) {
    const char *paragraph_end;
    size_t i;
    if (line_value(task, "Objective: ", out, size)) return;
    if (line_value(task, "Discuss and plan this objective with the user: ", out, size)) {
        /* A planning task started without one asks the user for it. */
        if (!strncmp(out, "Ask the user what they want", 27)) out[0] = '\0';
        return;
    }
    paragraph_end = strstr(task, "\n\n");
    snprintf(out, size, "%.*s", (int)(paragraph_end ? (size_t)(paragraph_end - task) : strlen(task)), task);
    for (i = 0; out[i]; i++) if ((unsigned char)out[i] < 32 || out[i] == 127) out[i] = ' ';
}

const char *head_objective(struct app *app, const struct head *head) {
    char path[SOURCE_TEXT + 8], task[4096];
    size_t n;
    FILE *file;
    if (!head || head_remote(head) || !head->head_id[0] || !head->source[0]) return "";
    if (!strcmp(app->objective_head, head->head_id)) return app->objective;
    copy_text(app->objective_head, sizeof(app->objective_head), head->head_id);
    app->objective[0] = '\0';
    snprintf(path, sizeof(path), "%s/task", head->source);
    if (!(file = fopen(path, "r"))) return app->objective;
    n = fread(task, 1, sizeof(task) - 1U, file);
    fclose(file);
    task[n] = '\0';
    objective_of(task, app->objective, sizeof(app->objective));
    return app->objective;
}

/* ---- the run step on this head -------------------------------------------- */

static bool step_done(const char *state) { return !strcmp(state, "succeeded") || !strcmp(state, "skipped"); }

static const struct workflow_node *run_node(const struct workflow_model *m, size_t run, const char *id, size_t length) {
    size_t i;
    for (i = 0; i < m->node_count; i++)
        if (m->nodes[i].run == run && strlen(m->nodes[i].id) == length && !strncmp(m->nodes[i].id, id, length)) return &m->nodes[i];
    return NULL;
}

/* "; waits for approval (waiting-approval on worker-a)" for each step it
 * needs that has not finished. */
static void step_blockers(const struct app *app, const struct workflow_node *step, char *out, size_t size) {
    const char *at = step->needs;
    bool first = true;
    while (*at) {
        size_t length = strcspn(at, ", ");
        const struct workflow_node *need = length ? run_node(app->workflows, step->run, at, length) : NULL;
        if (need && !step_done(need->state)) {
            text_append(out, size, "%s%s (%s%s%s)", first ? "; waits for " : ", ", need->id, need->state,
                        need->head[0] ? " on " : "", need->head);
            first = false;
        }
        at += length;
        at += strspn(at, ", ");
    }
}

/* The step of a run that targets the head, newest run first, one whose step
 * has not finished before one that has. */
static const struct workflow_node *targeting_step(const struct app *app, const struct head *head) {
    const struct workflow_model *m = app->workflows;
    const struct workflow_node *best = NULL;
    size_t i;
    if (!m || head_remote(head)) return NULL;
    for (i = 0; i < m->node_count; i++) {
        const struct workflow_node *n = &m->nodes[i];
        if (!strcmp(n->kind, "spawn") || strcmp(n->head, head->branch)) continue;
        if (!best || (step_done(best->state) && !step_done(n->state)) ||
            (step_done(best->state) == step_done(n->state) && strcmp(m->runs[n->run].created, m->runs[best->run].created) > 0))
            best = n;
    }
    return best;
}

bool head_step_link(const struct app *app, const struct head *head, char *out, size_t size) {
    const struct workflow_node *step = targeting_step(app, head);
    const struct workflow_run *run;
    if (!step) return false;
    run = &app->workflows->runs[step->run];
    snprintf(out, size, "step %s of %s %s, %s", step->id, run_label_kind(run), run->name, step->state);
    if (!step_done(step->state)) step_blockers(app, step, out, size);
    return true;
}

/* ---- whether the agent left the terminal ---------------------------------- */

/* "Hydra: codex exited with status 1; this pane is now a shell for head
 * needs-login." Returns the status, or -1 when the text has no such line
 * for this head. */
static int exit_status_in(const char *text, const char *branch) {
    static const char said[] = " exited with status ";
    char shell[TEXT + 48];
    const char *at = text, *found = NULL;
    snprintf(shell, sizeof(shell), "; this pane is now a shell for head %s.", branch);
    while ((at = strstr(at, "Hydra: ")) != NULL) {
        const char *end = strchr(at, '\n'), *exited = strstr(at, said), *tail = strstr(at, shell);
        if (exited && tail && exited < tail && (!end || tail < end)) found = exited + sizeof(said) - 1U;
        at += 7;
    }
    return found ? atoi(found) : -1;
}

/* The client's screen as one line of text: each row without its trailing
 * blanks, joined to the next directly when it fills the width (the
 * terminal wrapped it there) and by one space otherwise. */
static bool blank_cell(const struct kt_cell *cell) { return cell->glyph == ' ' || !cell->glyph; }

/* One row's text (ASCII; anything else is '?'); returns the bytes added. */
static size_t row_text(const struct kt_cell *row, int end, char *out, size_t room) {
    size_t used = 0;
    int x;
    for (x = 0; x < end && used < room; x++)
        if (row[x].glyph) out[used++] = row[x].glyph >= 32U && row[x].glyph < 127U ? (char)row[x].glyph : '?';
    return used;
}

static void screen_text(const struct native_terminal *t, char *out, size_t size) {
    const struct kt_screen *s = t->screen->alternate_active ? &t->screen->alternate : &t->screen->primary;
    size_t used = 0;
    int y;
    for (y = 0; y < s->canvas.height && used + 2U < size; y++) {
        const struct kt_cell *row = s->canvas.cells + (size_t)y * (size_t)s->canvas.stride;
        int end = s->canvas.width;
        while (end > 0 && blank_cell(&row[end - 1])) end--;
        used += row_text(row, end, out + used, size - used - 2U);
        if (end < s->canvas.width && used) out[used++] = ' ';
    }
    out[used] = '\0';
}

int head_agent_exit(struct app *app, const struct head *head) {
    const struct native_terminal *t;
    static char text[16384];
    if (!head || head_remote(head) || head_headless(head)) return -1;
    t = native_terminal_any_for_head(app, head);
    if (t && t->screen) {
        screen_text(t, text, sizeof(text));
        return exit_status_in(text, head->branch);
    }
    return !strcmp(app->preview_head, head->branch) ? exit_status_in(app->preview_text, head->branch) : -1;
}
