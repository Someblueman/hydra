/* themes: one view, any theme.
 *
 * The view names what things are (muted text, the selection, a raised panel);
 * a theme says what that looks like, and the runtime turns it into whatever
 * the terminal can show. Nothing here writes a colour code or asks what kind
 * of terminal this is.
 *
 *   themes            interactive: up/down select, t next theme, q quit
 *   themes --print    the same view once per built-in theme, as plain lines */
#include "program.h"
#include "layout.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct task { const char *name, *state, *agent; kt_color tone; };
static const struct task tasks[] = {
    {"feature-live", "running", "codex", KT_COLOR_SUCCESS},
    {"feature-stale", "terminal gone", "claude", KT_COLOR_WARNING},
    {"fix-\xe7\x95\x8c\xe9\x9d\xa2-rendering-of-wide-text-in-narrow-panes", "stopped", "shell", KT_COLOR_MUTED},
    {"docs", "failed", "codex", KT_COLOR_DANGER}
};
#define TASKS (sizeof(tasks) / sizeof(tasks[0]))

static const struct kt_theme *const themes[] = {&kt_theme_dark, &kt_theme_light, &kt_theme_terminal};
#define THEMES (sizeof(themes) / sizeof(themes[0]))

struct model { size_t selected, theme; };

static bool update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct model *m = model;
    if (msg->type != KT_MSG_KEY) return false;
    switch (msg->u.event.key) {
    case KT_KEY_UP: case 'k': if (m->selected) m->selected--; return true;
    case KT_KEY_DOWN: case 'j': if (m->selected + 1 < TASKS) m->selected++; return true;
    /* The model only remembers which theme; the runtime repaints the frame. */
    case 't': m->theme = (m->theme + 1) % THEMES; kt_cmd_theme(cmds, themes[m->theme]); return true;
    case 'q': kt_cmd_quit(cmds, 0); return true;
    default: return false;
    }
}

/* One task: a filled row when selected, with a bar at its left edge, and
 * three columns that share the width by constraint. */
static void task_row(struct kt_canvas *canvas, struct kt_rect row, const struct task *task, bool selected) {
    static const struct kt_constraint columns[] = {KT_MIN(12), KT_FIXED(14), KT_FIXED(7)};
    struct kt_rect cell[3];
    if (selected) {
        kt_fill(canvas, row, KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, 0));
        kt_box(canvas, row, &kt_border_bar, KT_INK(KT_COLOR_ACCENT, 0));
    }
    kt_rect_columns(kt_rect_inset(row, 0, 1, 0, 2), 2, columns, 3, cell);
    /* KT_KEEP: the text goes over the row's surface, selected or not. */
    kt_text_aligned(canvas, cell[0], task->name, KT_ALIGN_LEFT, KT_STYLE(KT_KEEP, KT_KEEP, selected ? KT_BOLD : 0));
    kt_text_aligned(canvas, cell[1], task->state, KT_ALIGN_LEFT, KT_INK(task->tone, 0));
    kt_text_aligned(canvas, cell[2], task->agent, KT_ALIGN_RIGHT, KT_INK(KT_COLOR_MUTED, 0));
}

static void draw(struct kt_canvas *canvas, const struct model *m, const char *theme) {
    struct kt_rect all = {0, 0, canvas->width, canvas->height}, header, status, panel, line;
    const struct kt_span title[] = {{"themes", KT_INK(KT_COLOR_ACCENT, KT_BOLD)}, {"  ", KT_INK(KT_COLOR_TEXT, 0)},
                                    {theme, KT_INK(KT_COLOR_MUTED, 0)}};
    const struct kt_span hints[] = {{" ready ", KT_STYLE(KT_COLOR_SUCCESS, KT_COLOR_SUNKEN, 0)},
        {"  up/down", KT_INK(KT_COLOR_TEXT, KT_BOLD)}, {" select  ", KT_INK(KT_COLOR_MUTED, 0)},
        {"t", KT_INK(KT_COLOR_TEXT, KT_BOLD)}, {" theme  ", KT_INK(KT_COLOR_MUTED, 0)},
        {"q", KT_INK(KT_COLOR_TEXT, KT_BOLD)}, {" quit", KT_INK(KT_COLOR_MUTED, 0)}};
    size_t i;
    /* Take the header and the status line off the top and bottom; the panel
     * gets what is left, as a raised surface instead of a box. */
    header = kt_rect_cut(all, KT_SIDE_TOP, 1, &all);
    status = kt_rect_cut(all, KT_SIDE_BOTTOM, 1, &all);
    panel = kt_rect_pad(all, 1, 1);
    kt_spans(canvas, kt_rect_pad(header, 1, 0), title, 3);
    kt_fill(canvas, panel, KT_STYLE(KT_COLOR_TEXT, KT_COLOR_RAISED, 0));
    panel = kt_rect_pad(panel, 1, 0);
    /* A rule with its title set in replaces a border around the list. */
    line = kt_rect_cut(panel, KT_SIDE_TOP, 1, &panel);
    kt_rule_styled(canvas, line, "Tasks", KT_ALIGN_LEFT, KT_INK(KT_COLOR_TEXT, KT_BOLD), KT_INK(KT_COLOR_FAINT, 0));
    for (i = 0; i < TASKS && panel.height > 0; i++) {
        line = kt_rect_cut(panel, KT_SIDE_TOP, 1, &panel);
        task_row(canvas, line, &tasks[i], i == m->selected);
    }
    kt_spans(canvas, kt_rect_pad(status, 1, 0), hints, sizeof(hints) / sizeof(hints[0]));
}

static void view(const void *model, struct kt_frame *frame) {
    draw(kt_frame_canvas(frame), model, kt_frame_theme(frame)->name);
}

static const struct kt_app themes_app = {NULL, update, view};

/* The same drawing without the runtime: a canvas in caller storage, written
 * a row at a time against each theme, for whatever standard output can show. */
static const char *variable(void *context, const char *name) { (void)context; return getenv(name); }

static int print(void) {
    static struct kt_cell cells[64 * 9];
    struct model model = {1, 0};
    struct kt_canvas canvas;
    struct kt_paint paint;
    size_t i;
    int row;
    paint.profile = kt_profile_detect(variable, NULL, isatty(STDOUT_FILENO) != 0);
    for (i = 0; i < THEMES; i++) {
        paint.theme = themes[i];
        if (!kt_init(&canvas, cells, sizeof(cells) / sizeof(cells[0]), 64, 9, true)) return 1;
        draw(&canvas, &model, themes[i]->name);
        for (row = 0; row < canvas.height; row++)
            if (!kt_write_row(&canvas, row, stdout, &paint) || putchar('\n') == EOF) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct model model = {0, 0};
    struct kt_options options = {0};
    struct kt_program *program;
    int status;
    if (argc > 1 && !strcmp(argv[1], "--print")) return print();
    options.alt_screen = true;
    options.theme = themes[model.theme];
    program = kt_program_new(&themes_app, &model, &options);
    if (!program) return 1;
    status = kt_program_run(program);
    kt_program_free(program);
    if (status < 0) fputs("themes needs an interactive terminal; try --print\n", stderr);
    return status < 0 ? 1 : status;
}
