#include "terminal_internal.h"
#include <string.h>

struct tv_screen *tv_term_screen(struct tv_terminal_model *t) {
    return t->alternate_active ? &t->alternate : &t->primary;
}

struct tv_cell tv_term_blank(const struct tv_terminal_model *t) {
    struct tv_cell cell = t->pen;
    cell.glyph = ' '; cell.width = 1; cell.combining_count = 0;
    memset(cell.combining, 0, sizeof(cell.combining));
    return cell;
}

void tv_term_erase(struct tv_terminal_model *t, int y, int first, int last) {
    struct tv_canvas *c = &tv_term_screen(t)->canvas;
    struct tv_cell empty = tv_term_blank(t), *row;
    int x;
    if (y < 0 || y >= c->height) return;
    if (first < 0) first = 0;
    if (last >= c->width) last = c->width - 1;
    if (last < first) return;
    row = c->cells + (size_t)y * (size_t)c->stride;
    if (!row[first].width && first > 0) row[first - 1] = empty;
    if (row[last].width == 2 && last + 1 < c->width) row[last + 1] = empty;
    for (x = first; x <= last; x++) row[x] = empty;
}

void tv_term_scroll(struct tv_terminal_model *t, int top, int bottom, int amount) {
    struct tv_screen *s = tv_term_screen(t);
    struct tv_canvas *c = &s->canvas;
    int count = amount < 0 ? -amount : amount, i;
    size_t stride = (size_t)c->stride;
    if (top < 0 || bottom >= c->height || top > bottom) return;
    if (count > bottom - top + 1) count = bottom - top + 1;
    if (!count) return;
    if (amount > 0) {
        if (!t->alternate_active && top == 0 && bottom == c->height - 1 && t->history_rows) {
            for (i = 0; i < count; i++) {
                size_t slot = (t->history_start + t->history_count) % t->history_rows;
                memcpy(t->history + slot * stride, c->cells + (size_t)i * stride, stride * sizeof(*c->cells));
                if (t->history_count == t->history_rows) t->history_start = (t->history_start + 1) % t->history_rows;
                else t->history_count++;
                if (t->history_serial != UINT64_MAX) t->history_serial++;
            }
        }
        memmove(c->cells + (size_t)top * stride, c->cells + (size_t)(top + count) * stride,
                (size_t)(bottom - top + 1 - count) * stride * sizeof(*c->cells));
        for (i = bottom - count + 1; i <= bottom; i++) tv_term_erase(t, i, 0, c->width - 1);
    } else {
        memmove(c->cells + (size_t)(top + count) * stride, c->cells + (size_t)top * stride,
                (size_t)(bottom - top + 1 - count) * stride * sizeof(*c->cells));
        for (i = top; i < top + count; i++) tv_term_erase(t, i, 0, c->width - 1);
    }
}

void tv_term_index(struct tv_terminal_model *t, bool reverse) {
    struct tv_screen *s = tv_term_screen(t);
    s->wrap_pending = false;
    if (reverse) {
        if (s->y == s->top) tv_term_scroll(t, s->top, s->bottom, -1);
        else if (s->y > 0) s->y--;
    } else {
        if (s->y == s->bottom) tv_term_scroll(t, s->top, s->bottom, 1);
        else if (s->y < s->canvas.height - 1) s->y++;
    }
}

void tv_term_glyph(struct tv_terminal_model *t, uint32_t cp) {
    struct tv_screen *s = tv_term_screen(t);
    struct tv_canvas *c = &s->canvas;
    struct tv_cell *cell;
    int width = tv_codepoint_width(cp), x;
    if (width < 0) { cp = '?'; width = 1; }
    if (!width) {
        x = s->wrap_pending ? s->x : s->x - 1;
        if (x < 0) return;
        cell = &c->cells[(size_t)s->y * (size_t)c->stride + (size_t)x];
        if (!cell->width && x > 0) cell--;
        if (cell->combining_count < TV_COMBINING_MAX) cell->combining[cell->combining_count++] = cp;
        return;
    }
    if (s->wrap_pending || (width == 2 && s->x == c->width - 1)) {
        if (t->autowrap) { tv_term_index(t, false); s->x = 0; }
        s->wrap_pending = false;
    }
    if (width == 2 && c->width == 1) { cp = '?'; width = 1; }
    if (width == 2 && s->x == c->width - 1) { cp = ' '; width = 1; }
    cell = &c->cells[(size_t)s->y * (size_t)c->stride + (size_t)s->x];
    if (t->insert) {
        if (!cell->width && s->x > 0) cell[-1] = tv_term_blank(t);
        memmove(cell + width, cell, (size_t)(c->width - s->x - width) * sizeof(*cell));
        if (c->cells[(size_t)s->y * (size_t)c->stride + (size_t)c->width - 1].width == 2)
            c->cells[(size_t)s->y * (size_t)c->stride + (size_t)c->width - 1] = tv_term_blank(t);
    }
    tv_put(c, s->x, s->y, cp, TV_BASE);
    cell->attributes = t->pen.attributes;
    cell->foreground = t->pen.foreground; cell->background = t->pen.background;
    if (width == 2) { cell[1].attributes = cell->attributes; cell[1].foreground = cell->foreground; cell[1].background = cell->background; }
    s->x += width;
    if (s->x >= c->width) { s->x = c->width - 1; s->wrap_pending = t->autowrap; }
}

void tv_term_save(struct tv_terminal_model *t) {
    struct tv_screen *s = tv_term_screen(t);
    s->saved_x = s->x; s->saved_y = s->y; s->saved_pen = t->pen;
}

void tv_term_restore(struct tv_terminal_model *t) {
    struct tv_screen *s = tv_term_screen(t);
    s->x = s->saved_x < s->canvas.width ? s->saved_x : s->canvas.width - 1;
    s->y = s->saved_y < s->canvas.height ? s->saved_y : s->canvas.height - 1;
    s->wrap_pending = false; t->pen = s->saved_pen;
}

void tv_term_reset(struct tv_terminal_model *t) {
    int i;
    memset(&t->pen, 0, sizeof(t->pen));
    t->pen.foreground = t->pen.background = TV_COLOR_DEFAULT;
    t->pen.attributes = TV_EXPLICIT; t->pen.width = 1; t->pen.glyph = ' ';
    t->alternate_active = t->origin = t->insert = t->application_cursor = t->bracketed_paste = t->mouse_sgr = false;
    t->autowrap = t->cursor_visible = true; t->mouse_mode = 0;
    t->history_count = t->history_start = 0;
    t->parser_state = 0; t->utf8_length = t->reply_length = 0;
    for (i = 0; i < 2; i++) {
        struct tv_screen *s = i ? &t->alternate : &t->primary;
        size_t j, count = (size_t)t->max_columns * (size_t)t->max_rows;
        s->x = s->y = s->saved_x = s->saved_y = s->top = 0;
        s->bottom = s->canvas.height - 1; s->wrap_pending = false; s->saved_pen = t->pen;
        for (j = 0; j < count; j++) s->canvas.cells[j] = t->pen;
    }
}

bool tv_term_init(struct tv_terminal_model *t, struct tv_cell *primary, struct tv_cell *alternate,
                  int max_columns, int max_rows, struct tv_cell *history, size_t history_rows,
                  int columns, int rows) {
    if (!t || !primary || !alternate || primary == alternate || max_columns < 1 || max_columns > 512 ||
        max_rows < 1 || max_rows > 256 || columns < 1 || columns > max_columns || rows < 1 || rows > max_rows ||
        (history_rows && !history) || history_rows > 10000) return false;
    memset(t, 0, sizeof(*t));
    t->max_columns = max_columns; t->max_rows = max_rows;
    t->history = history; t->history_rows = history_rows;
    t->primary.canvas = (struct tv_canvas){primary, columns, rows, max_columns, true};
    t->alternate.canvas = (struct tv_canvas){alternate, columns, rows, max_columns, true};
    tv_term_reset(t);
    return true;
}

bool tv_term_resize(struct tv_terminal_model *t, int columns, int rows) {
    int i, x, y;
    struct tv_cell empty = tv_term_blank(t);
    if (columns < 1 || columns > t->max_columns || rows < 1 || rows > t->max_rows) return false;
    if (columns == t->primary.canvas.width && rows == t->primary.canvas.height) return true;
    for (i = 0; i < 2; i++) {
        struct tv_screen *s = i ? &t->alternate : &t->primary;
        for (y = 0; y < t->max_rows; y++) for (x = 0; x < t->max_columns; x++) {
            struct tv_cell *cell = s->canvas.cells + (size_t)y * (size_t)t->max_columns + (size_t)x;
            if (x >= columns || x >= s->canvas.width || y >= rows || y >= s->canvas.height ||
                (x == columns - 1 && cell->width == 2)) *cell = empty;
        }
        s->canvas.width = columns; s->canvas.height = rows;
        if (s->x >= columns) s->x = columns - 1;
        if (s->y >= rows) s->y = rows - 1;
        s->top = 0; s->bottom = rows - 1; s->wrap_pending = false;
    }
    return true;
}

void tv_term_draw(const struct tv_terminal_model *t, struct tv_canvas *out, size_t scroll, bool cursor) {
    const struct tv_screen *s = t->alternate_active ? &t->alternate : &t->primary;
    size_t history = t->alternate_active ? 0 : t->history_count;
    int y, x;
    if (scroll > history) scroll = history;
    tv_clear(out, TV_BASE);
    for (y = 0; y < out->height && y < s->canvas.height; y++) {
        size_t logical = history - scroll + (size_t)y;
        const struct tv_cell *row = logical < history ?
            t->history + ((t->history_start + logical) % t->history_rows) * (size_t)t->max_columns :
            s->canvas.cells + (logical - history) * (size_t)t->max_columns;
        for (x = 0; x < out->width && x < s->canvas.width; x++) {
            struct tv_cell cell = row[x];
            if ((!cell.width && !x) || (cell.width == 2 && x + 1 == out->width)) {
                cell.glyph = ' '; cell.width = 1; cell.combining_count = 0;
            }
            if (cursor && !scroll && t->cursor_visible && y == s->y &&
                (x == s->x || (s->x > 0 && x == s->x - 1 && !row[s->x].width))) cell.attributes ^= TV_REVERSE;
            out->cells[(size_t)y * (size_t)out->stride + (size_t)x] = cell;
        }
    }
}
