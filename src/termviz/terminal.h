#ifndef TV_TERMINAL_H
#define TV_TERMINAL_H
#include "termviz.h"

struct tv_screen {
    struct tv_canvas canvas;
    int x, y, saved_x, saved_y, top, bottom;
    bool wrap_pending;
    struct tv_cell saved_pen;
};
/* Caller owns all storage, including primary/alternate screens and history.
 * Arrays must not overlap. Each screen holds max_columns * max_rows cells;
 * history holds max_columns * history_rows cells. No allocation or OS access.
 * Resize preserves the top-left intersection, clears cropped cells, and resets
 * margins; history stores physical rows and is not reflowed. See TERMINAL.md. */
struct tv_terminal_model {
    struct tv_screen primary, alternate;
    struct tv_cell *history;
    size_t history_rows, history_count, history_start;
    uint64_t history_serial, unsupported;
    int max_columns, max_rows;
    struct tv_cell pen;
    bool alternate_active, autowrap, origin, insert, cursor_visible, application_cursor, bracketed_paste;
    unsigned mouse_mode;
    bool mouse_sgr;
    unsigned parser_state, parameter_count, parameters[16];
    bool parameter_present[16], private_mode, invalid_sequence;
    unsigned char utf8[4];
    size_t utf8_length;
    char reply[256];
    size_t reply_length;
};
bool tv_term_init(struct tv_terminal_model *term, struct tv_cell *primary, struct tv_cell *alternate,
                  int max_columns, int max_rows, struct tv_cell *history, size_t history_rows,
                  int columns, int rows);
void tv_term_reset(struct tv_terminal_model *term);
bool tv_term_resize(struct tv_terminal_model *term, int columns, int rows);
void tv_term_feed(struct tv_terminal_model *term, const void *bytes, size_t length);
/* Resolve a truncated UTF-8 scalar at EOF; discard an incomplete escape. */
void tv_term_finish(struct tv_terminal_model *term);
/* Paint to a borrowed clipped surface. Scroll is rows back from the live bottom.
 * Cursor appears only at the live bottom and when requested by the focused pane. */
void tv_term_draw(const struct tv_terminal_model *term, struct tv_canvas *surface,
                  size_t scroll, bool cursor);
/* Optional protocol replies (device status/attributes). Caller drains and sends
 * these only to this terminal's child; no response is written to the outer TTY. */
size_t tv_term_take_reply(struct tv_terminal_model *term, char *out, size_t capacity);
#endif
