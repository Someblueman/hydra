#include "kettle/program.h"
#include <stdlib.h>
#include <string.h>

/* A kt_program application for test_program_pty.c: it shows what it was told
 * (size, keys, paste, focus) inside a border that reaches every edge of the
 * terminal, and offers each way a program can leave or lend the terminal. */

struct fixture {
    int columns, rows, keys, resizes, resumes, exec_status;
    bool cursor, focused;
    char pasted[32];
};

static void on_key(struct fixture *f, char key, struct kt_cmds *cmds) {
    static char *child[] = {"sh", "-c", "stty -a | grep -q ' icanon' && echo CHILD_COOKED; exit 7", NULL};
    f->keys++;
    switch (key) {
    case 'q': kt_cmd_quit(cmds, 0); break;
    case 'x': exit(3);
    case 'a': abort();
    case 'r': kt_cmd_repaint(cmds); break;
    case 'z': kt_cmd_suspend(cmds); break;
    case 'e': kt_cmd_exec(cmds, 0, child); break;
    case 'c': f->cursor = !f->cursor; break;
    default: break;
    }
}

static bool update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct fixture *f = model;
    switch (msg->type) {
    case KT_MSG_RESIZE: f->columns = msg->u.resize.columns; f->rows = msg->u.resize.rows; f->resizes++; return true;
    case KT_MSG_KEY: on_key(f, msg->u.event.length == 1 ? msg->u.event.bytes[0] : '\0', cmds); return true;
    case KT_MSG_PASTE: snprintf(f->pasted, sizeof(f->pasted), "%.*s", (int)msg->u.paste.length, msg->u.paste.bytes); return true;
    case KT_MSG_FOCUS: f->focused = true; return true;
    case KT_MSG_BLUR: f->focused = false; return true;
    case KT_MSG_EXEC: f->exec_status = msg->u.exec.status; return true;
    case KT_MSG_RESUME: f->resumes++; return true;
    default: return false;
    }
}

static void line(struct kt_canvas *canvas, int row, const char *text) {
    kt_text(canvas, (struct kt_rect){2, row, canvas->width - 4, 1}, text, KT_BASE);
}

static void view(const void *model, struct kt_frame *frame) {
    const struct fixture *f = model;
    struct kt_canvas *canvas = kt_frame_canvas(frame);
    char text[96];
    kt_panel(canvas, (struct kt_rect){0, 0, kt_frame_columns(frame), kt_frame_rows(frame)}, "FIXTURE");
    snprintf(text, sizeof(text), "size %dx%d", f->columns, f->rows);
    line(canvas, 1, text);
    snprintf(text, sizeof(text), "keys %d resizes %d resumes %d", f->keys, f->resizes, f->resumes);
    line(canvas, 2, text);
    snprintf(text, sizeof(text), "paste [%s] focus %s exec %d", f->pasted, f->focused ? "yes" : "no", f->exec_status);
    line(canvas, 3, text);
    if (f->cursor) kt_frame_cursor(frame, 2, 4);
}

static void style(void *model, FILE *out, enum kt_style style) {
    (void)model;
    fputs(style == KT_BASE ? "\033[0m" : "\033[0;36m", out);
}

int main(void) {
    static const struct kt_app app = {NULL, update, view, style};
    struct fixture fixture;
    struct kt_options options = {0};
    struct kt_program *program;
    int status;
    memset(&fixture, 0, sizeof(fixture));
    options.alt_screen = options.bracketed_paste = options.focus_events = true;
    options.mouse = KT_MOUSE_DRAG;
    program = kt_program_new(&app, &fixture, &options);
    if (!program) return 1;
    status = kt_program_run(program);
    kt_program_free(program);
    return status < 0 ? 1 : status;
}
