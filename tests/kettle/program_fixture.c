#include "kettle/program.h"
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* A kt_program application for test_program_pty.c: it shows what it was told
 * (size, keys, paste, focus) inside a border that reaches every edge of the
 * terminal, and offers each way a program can leave or lend the terminal. */

struct fixture {
    int columns, rows, keys, resizes, resumes, exec_status, answers, lends;
    bool cursor, focused, asking;
    char pasted[32];
};

static struct kt_program *program;

/* A question that update does not return from until it is answered: the loop
 * is run from here, view draws the question, and y answers it. */
static void ask(struct fixture *f) {
    struct kt_msg msg;
    f->asking = true;
    while (f->asking && kt_program_render(program) && kt_program_next(program, &msg, -1)) {
        if (msg.type == KT_MSG_KEY && msg.u.event.length == 1 && msg.u.event.bytes[0] == 'y') f->asking = false;
        else if (msg.type != KT_MSG_KEY) (void)kt_program_dispatch(program, &msg);
    }
    f->asking = false;
    f->answers++;
}

static void say(const char *text) {
    if (write(STDOUT_FILENO, text, strlen(text)) < 0) return;
}

/* Lends the terminal within one update: reports whether it is cooked, waits
 * for a line as a cooked terminal delivers it, and takes the terminal back. */
static void lend(struct fixture *f) {
    struct termios now;
    char byte = '\0';
    if (!kt_program_release(program)) return;
    say(tcgetattr(STDIN_FILENO, &now) == 0 && (now.c_lflag & ICANON) ? "LENT_COOKED\n" : "LENT_RAW\n");
    while (byte != '\n' && read(STDIN_FILENO, &byte, 1) == 1) { }
    f->lends += kt_program_reclaim(program) ? 1 : 0;
}

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
    case 'm': ask(f); break;
    case 'l': lend(f); break;
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
    snprintf(text, sizeof(text), "answers %d lends %d", f->answers, f->lends);
    line(canvas, 4, text);
    if (f->asking) line(canvas, 5, "really? y answers");
    if (f->cursor) kt_frame_cursor(frame, 2, 4);
}

int main(void) {
    static const struct kt_app app = {NULL, update, view};
    struct fixture fixture;
    struct kt_options options = {0};
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
