/* counter: a complete kettle application in one file.
 *
 *   model   plain data, owned by main
 *   update  the only place the model changes: one message in, commands out
 *   view    draws the model; it runs after update, at most once per frame
 *
 * The runtime owns the terminal, the event loop and every effect. This file
 * never reads input, sleeps, forks or writes an escape sequence. */
#include "program.h"
#include <string.h>

enum { TICK = 1, DATE };   /* tags route a command's message back to its cause */

struct model {
    int count;         /* changed by keys */
    unsigned seconds;  /* changed by a repeating timer */
    char date[64];     /* output of a child process */
    bool asking;
};

/* Effects are requests: the child runs beside the loop, and its output
 * arrives later as a KT_MSG_PROCESS carrying the DATE tag. */
static void ask_date(struct model *m, struct kt_cmds *cmds) {
    static char *argv[] = {"date", NULL};
    struct kt_spawn spawn = {0};
    spawn.argv = argv;
    spawn.budget_ms = 2000;
    m->asking = kt_cmd_spawn(cmds, DATE, &spawn) != 0;
}

static void init(void *model, struct kt_cmds *cmds) {
    (void)kt_cmd_timer(cmds, TICK, 1000, 1000);
    ask_date(model, cmds);
}

static bool on_key(struct model *m, const struct kt_event *key, struct kt_cmds *cmds) {
    switch (key->key) {
    case KT_KEY_UP: case '+': m->count++; return true;
    case KT_KEY_DOWN: case '-': m->count--; return true;
    case 'd': ask_date(m, cmds); return true;
    case 'q': kt_cmd_quit(cmds, 0); return true;
    default: return false;
    }
}

static void on_date(struct model *m, const struct kt_process_result *result) {
    size_t line = strcspn(result->output, "\n");
    m->asking = false;
    if (result->failed || result->status != 0) snprintf(m->date, sizeof(m->date), "unavailable");
    else snprintf(m->date, sizeof(m->date), "%.*s", (int)line, result->output);
}

static bool update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct model *m = model;
    switch (msg->type) {
    case KT_MSG_KEY: return on_key(m, &msg->u.event, cmds);
    case KT_MSG_TIMER: m->seconds++; return true;
    case KT_MSG_PROCESS: on_date(m, &msg->u.process); return true;
    default: return false;   /* unhandled signals quit; everything else is ignored */
    }
}

static void view(const void *model, struct kt_frame *frame) {
    const struct model *m = model;
    struct kt_canvas *canvas = kt_frame_canvas(frame);
    int width = kt_frame_columns(frame), rows = kt_frame_rows(frame);
    char line[96];
    kt_panel(canvas, (struct kt_rect){0, 0, width, rows}, "counter");
    snprintf(line, sizeof(line), "Count    %d", m->count);
    kt_text(canvas, (struct kt_rect){2, 2, width - 4, 1}, line, KT_STRONG);
    snprintf(line, sizeof(line), "Running  %u s", m->seconds);
    kt_text(canvas, (struct kt_rect){2, 3, width - 4, 1}, line, KT_BASE);
    snprintf(line, sizeof(line), "Date     %s", m->asking ? "asking..." : m->date);
    kt_text(canvas, (struct kt_rect){2, 4, width - 4, 1}, line, KT_BASE);
    kt_text(canvas, (struct kt_rect){2, rows - 2, width - 4, 1},
            "up/down or +/- count   d ask date again   q quit", KT_MUTED);
}

/* Cells carry meaning (KT_STRONG, KT_MUTED); this gives it a look. */
static void style(void *model, FILE *out, enum kt_style style) {
    static const char *const sgr[] = {"\033[0m", "\033[0;36m", "\033[0;1;36m", "\033[0;7m",
        "\033[0;33m", "\033[0;1m", "\033[0;32m", "\033[0;2m", "\033[0;1;36m"};
    (void)model;
    fputs(sgr[style <= KT_FOCUS ? style : KT_BASE], out);
}

static const struct kt_app counter = {init, update, view, style};

int main(void) {
    struct model model = {0};
    struct kt_options options = {0};
    struct kt_program *program;
    int status;
    options.alt_screen = true;
    program = kt_program_new(&counter, &model, &options);
    if (!program) return 1;
    status = kt_program_run(program);   /* returns with the terminal restored */
    kt_program_free(program);
    if (status < 0) fputs("counter needs an interactive terminal\n", stderr);
    return status < 0 ? 1 : status;
}
