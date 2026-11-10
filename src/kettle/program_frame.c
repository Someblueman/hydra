#define _POSIX_C_SOURCE 200809L
#include "program_internal.h"
#include <string.h>

/* Frames and terminal state. A frame is assembled in memory and leaves as one
 * write: mode changes, then changed cells, then the cursor. Terminal state is
 * declarative: `applied` records what the terminal has, each frame says what
 * it wants, and only the difference is written. */

static const char sync_begin[] = "\033[?2026h", sync_end[] = "\033[?2026l";
static const char *const mouse_on[] = {"", "\033[?1000h", "\033[?1002h", "\033[?1003h"};
static const char *const mouse_off[] = {"", "\033[?1000l", "\033[?1002l", "\033[?1003l"};

/* ---- view interface ----------------------------------------------------- */

struct kt_canvas *kt_frame_canvas(struct kt_frame *frame) { return &frame->canvas; }
int kt_frame_columns(const struct kt_frame *frame) { return frame->canvas.width; }
int kt_frame_rows(const struct kt_frame *frame) { return frame->canvas.height; }
enum kt_profile kt_frame_profile(const struct kt_frame *frame) { return frame->paint.profile; }
const struct kt_theme *kt_frame_theme(const struct kt_frame *frame) { return frame->paint.theme; }

void kt_frame_cursor(struct kt_frame *frame, int x, int y) {
    frame->cursor = x >= 0 && y >= 0 && x < frame->canvas.width && y < frame->canvas.height;
    frame->cursor_x = x; frame->cursor_y = y;
}

void kt_frame_mouse(struct kt_frame *frame, enum kt_mouse_mode mode) {
    frame->mouse = mode >= KT_MOUSE_OFF && mode <= KT_MOUSE_ALL ? mode : KT_MOUSE_OFF;
}

/* Titles are data: control bytes cannot end the escape sequence early, and a
 * long title is cut between characters. */
void kt_frame_title(struct kt_frame *frame, const char *title) {
    size_t length = title ? strlen(title) : 0, at = 0, used = 0;
    frame->titled = title != NULL;
    while (at < length) {
        uint32_t cp = '?';
        char bytes[4];
        size_t step = kt_utf8_decode(title + at, length - at, &cp), size;
        if (!step) break;
        if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) cp = ' ';
        size = kt_utf8_encode(cp, bytes);
        if (used + size >= KT_TITLE_MAX) break;
        memcpy(frame->title + used, bytes, size);
        used += size; at += step;
    }
    frame->title[used] = '\0';
}

/* ---- buffers ------------------------------------------------------------ */

static int clipped(int value, int maximum) {
    if (value < 1) return 1;
    return value > maximum ? maximum : value;
}

/* The canvas and the presenter's copy of the screen are exactly as large as
 * the terminal (within max_columns x max_rows). A new size repaints fully. */
bool kt_frame_resize(struct kt_program *p, int columns, int rows) {
    size_t count;
    struct kt_cell *cells, *previous;
    p->terminal_columns = columns; p->terminal_rows = rows;
    columns = clipped(columns, p->options.max_columns);
    rows = clipped(rows, p->options.max_rows);
    count = (size_t)columns * (size_t)rows;
    if (count != p->cell_capacity) {
        cells = kt_program_allocate(p, p->cells, count * sizeof(*cells));
        if (!cells) return false;
        p->cells = cells;
        previous = kt_program_allocate(p, p->previous, count * sizeof(*previous));
        if (!previous) return false;
        p->previous = previous; p->cell_capacity = count;
    }
    if (!kt_init(&p->canvas, p->cells, count, columns, rows, !p->options.ascii) ||
        !kt_present_init(&p->presenter, p->previous, count)) return false;
    p->presenter.overwrite = true;
    return true;
}

/* ---- terminal modes ----------------------------------------------------- */

static void toggle(FILE *out, bool *applied, bool wanted, const char *on, const char *off) {
    if (*applied == wanted) return;
    fputs(wanted ? on : off, out);
    *applied = wanted;
}

/* Mouse reports always use the SGR encoding the input decoder understands. */
static void mouse_emit(FILE *out, enum kt_mouse_mode from, enum kt_mouse_mode to) {
    if (from == to) return;
    fputs(mouse_off[from], out);
    if (to == KT_MOUSE_OFF) fputs("\033[?1006l", out);
    else {
        fputs(mouse_on[to], out);
        if (from == KT_MOUSE_OFF) fputs("\033[?1006h", out);
    }
}

/* The terminal's own title is pushed on its title stack before the first
 * change and popped when frames stop asking for one. */
static void title_emit(struct kt_program *p, const struct kt_frame *frame) {
    struct kt_modes *on = &p->applied;
    if (!frame->titled) {
        toggle(p->out, &on->titled, false, "", "\033[23;2t");
        return;
    }
    if (on->titled && !strcmp(on->title, frame->title)) return;
    toggle(p->out, &on->titled, true, "\033[22;2t", "");
    fprintf(p->out, "\033]2;%s\033\\", frame->title);
    memcpy(on->title, frame->title, sizeof(on->title));
}

static void modes_forget(struct kt_program *p) {
    memset(&p->applied, 0, sizeof(p->applied));
    p->reassert = false;
    kt_present_invalidate(&p->presenter);
}

void kt_frame_reassert(struct kt_program *p) {
    p->reassert = true;
    kt_present_invalidate(&p->presenter);
}

/* Taking over the screen clears it once; after that frames only overwrite. */
static void modes_emit(struct kt_program *p, const struct kt_frame *frame) {
    struct kt_modes *on = &p->applied;
    if (p->reassert) {
        /* The title saved when it was first changed is still on the terminal's stack. */
        bool titled = on->titled;
        modes_forget(p);
        on->titled = titled;
    }
    if (!on->screen) {
        if (p->options.alt_screen) fputs("\033[?1049h", p->out);
        fputs("\033[H\033[2J", p->out);
        on->screen = true;
        kt_present_invalidate(&p->presenter);
    }
    mouse_emit(p->out, on->mouse, frame->mouse);
    on->mouse = frame->mouse;
    toggle(p->out, &on->paste, p->options.bracketed_paste, "\033[?2004h", "\033[?2004l");
    toggle(p->out, &on->focus, p->options.focus_events, "\033[?1004h", "\033[?1004l");
    title_emit(p, frame);
}

/* The terminal cursor is left wherever the last cell was written, so it is
 * put back whenever anything was. */
static void cursor_emit(struct kt_program *p, const struct kt_frame *frame, bool painted) {
    struct kt_modes *on = &p->applied;
    bool moved = painted || on->cursor_hidden || on->cursor_x != frame->cursor_x ||
                 on->cursor_y != frame->cursor_y;
    if (!frame->cursor) {
        toggle(p->out, &on->cursor_hidden, true, "\033[?25l", "\033[?25h");
        return;
    }
    if (moved) fprintf(p->out, "\033[%d;%dH", frame->cursor_y + 1, frame->cursor_x + 1);
    toggle(p->out, &on->cursor_hidden, false, "\033[?25l", "\033[?25h");
    on->cursor_x = frame->cursor_x; on->cursor_y = frame->cursor_y;
}

static size_t append(char *out, size_t used, const char *text) {
    size_t length = strlen(text);
    if (used + length >= KT_LEAVE_MAX) return used;
    memcpy(out + used, text, length + 1);
    return used + length;
}

/* What returns the terminal to how it was found, given what is applied. */
static size_t leave_sequence(const struct kt_program *p, char *out) {
    const struct kt_modes *on = &p->applied;
    char park[32];
    size_t used = 0;
    out[0] = '\0';
    if (!on->screen) return 0;
    used = append(out, used, "\033[0m");
    used = append(out, used, mouse_off[on->mouse]);
    if (on->mouse != KT_MOUSE_OFF) used = append(out, used, "\033[?1006l");
    if (on->paste) used = append(out, used, "\033[?2004l");
    if (on->focus) used = append(out, used, "\033[?1004l");
    if (on->titled) used = append(out, used, "\033[23;2t");
    if (on->cursor_hidden) used = append(out, used, "\033[?25h");
    /* Without the alternate screen the last frame stays; the shell prompt
     * continues below it. */
    snprintf(park, sizeof(park), "\033[%d;1H\r\n", p->terminal_rows > 0 ? p->terminal_rows : 1);
    return append(out, used, p->options.alt_screen ? "\033[?1049l" : park);
}

/* Exit paths that bypass the loop need the same bytes; keep the backend's
 * copy current. */
static void leave_publish(struct kt_program *p) {
    char sequence[KT_LEAVE_MAX];
    size_t length = leave_sequence(p, sequence);
    if (length == p->leave_length && !memcmp(sequence, p->leave, length)) return;
    memcpy(p->leave, sequence, length + 1);
    p->leave_length = length;
    p->backend->restore(p->context, p->leave, length);
}

void kt_frame_leave(struct kt_program *p) {
    leave_publish(p);
    p->backend->leave(p->context);
    modes_forget(p);
    leave_publish(p);
}

/* ---- frame -------------------------------------------------------------- */

/* A terminal larger than the canvas keeps a margin the cells never reach. */
static void margin_erase(struct kt_program *p) {
    int row;
    if (p->terminal_columns > p->canvas.width)
        for (row = 0; row < p->canvas.height; row++)
            fprintf(p->out, "\033[%d;%dH\033[0m\033[K", row + 1, p->canvas.width + 1);
    if (p->terminal_rows > p->canvas.height)
        fprintf(p->out, "\033[%d;1H\033[0m\033[J", p->canvas.height + 1);
}

static bool cells_emit(struct kt_program *p) {
    bool full = !p->presenter.valid;
    if (!kt_present(&p->presenter, &p->canvas, p->out, &p->paint)) return false;
    if (full) margin_erase(p);
    return true;
}

static void frame_begin(struct kt_program *p, struct kt_frame *frame) {
    frame->cursor = frame->titled = false;
    frame->cursor_x = frame->cursor_y = 0;
    frame->title[0] = '\0';
    kt_clear(&p->canvas, KT_BASE);
    frame->canvas = p->canvas;
    frame->paint = p->paint;
    frame->mouse = p->options.mouse >= KT_MOUSE_OFF && p->options.mouse <= KT_MOUSE_ALL
                       ? p->options.mouse : KT_MOUSE_OFF;
}

bool kt_frame_render(struct kt_program *p) {
    struct kt_frame frame;
    long start, end;
    frame_begin(p, &frame);
    p->app->view(p->model, &frame);
    p->dirty = false;
    rewind(p->out);
    if (!p->options.no_synchronized_output) fputs(sync_begin, p->out);
    start = ftell(p->out);
    modes_emit(p, &frame);
    if (!cells_emit(p)) { kt_program_fail(p); return false; }
    cursor_emit(p, &frame, ftell(p->out) != start);
    end = ftell(p->out);
    /* The frame cap paces what the terminal is sent. A frame that changed
     * nothing sent nothing, so the change that follows it need not wait. */
    if (end == start) return true;
    p->frame_at = p->backend->now(p->context) + p->frame_interval;
    if (!p->options.no_synchronized_output) { fputs(sync_end, p->out); end += (long)sizeof(sync_end) - 1; }
    if (fflush(p->out) != 0 || ferror(p->out) || !p->backend->write(p->context, p->out_bytes, (size_t)end)) {
        kt_program_fail(p);
        return false;
    }
    leave_publish(p);
    return true;
}
