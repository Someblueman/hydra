#ifndef KT_TERMINAL_H
#define KT_TERMINAL_H
#include "kettle.h"

struct kt_screen {
    struct kt_canvas canvas;
    int x, y, saved_x, saved_y, top, bottom;
    bool wrap_pending;
    struct kt_cell saved_pen;
    bool saved_graphics[2];
    unsigned saved_charset;
};
/* Caller owns all storage, including primary/alternate screens and history.
 * Arrays must not overlap. Each screen holds max_columns * max_rows cells;
 * history holds max_columns * history_rows cells. No allocation or OS access.
 * Resize preserves the top-left intersection, clears cropped cells, and resets
 * margins; history stores physical rows and is not reflowed. See TERMINAL.md. */
struct kt_terminal_model {
    struct kt_screen primary, alternate;
    struct kt_cell *history;
    size_t history_rows, history_count, history_start;
    uint64_t history_serial, unsupported;
    int max_columns, max_rows;
    struct kt_cell pen;
    bool alternate_active, autowrap, origin, insert, cursor_visible, application_cursor, bracketed_paste;
    unsigned mouse_mode;
    bool mouse_sgr;
    unsigned parser_state, parameter_count, parameters[16];
    bool graphics[2];
    unsigned active_charset, charset_target;
    bool parameter_present[16], private_mode, invalid_sequence;
    unsigned char utf8[4];
    size_t utf8_length;
    char reply[256];
    size_t reply_length;
};
bool kt_term_init(struct kt_terminal_model *term, struct kt_cell *primary, struct kt_cell *alternate,
                  int max_columns, int max_rows, struct kt_cell *history, size_t history_rows,
                  int columns, int rows);
void kt_term_reset(struct kt_terminal_model *term);
bool kt_term_resize(struct kt_terminal_model *term, int columns, int rows);
void kt_term_feed(struct kt_terminal_model *term, const void *bytes, size_t length);
/* Resolve a truncated UTF-8 scalar at EOF; discard an incomplete escape. */
void kt_term_finish(struct kt_terminal_model *term);
/* Paint to a borrowed clipped surface. Scroll is rows back from the live bottom.
 * Cursor appears only at the live bottom and when requested by the focused pane. */
void kt_term_draw(const struct kt_terminal_model *term, struct kt_canvas *surface,
                  size_t scroll, bool cursor);
/* Optional protocol replies (device status/attributes). Caller drains and sends
 * these only to this terminal's child; no response is written to the outer TTY. */
size_t kt_term_take_reply(struct kt_terminal_model *term, char *out, size_t capacity);
#endif
