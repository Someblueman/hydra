#include "component_internal.h"
#include <string.h>

/* Rows of columns over the caller's data. Rows are one line each; the
 * selection, scrolling and marks are the list's. */

#define NAV KT_TABLE_SCOPE_BROWSE
#define MOVE "Move"

static const struct kt_binding table_keys[KT_TABLE_ACTIONS] = {
    KT_UI_ROW(KT_TABLE_UP, "table.up", "select", NAV, MOVE, 3, 2, KT_UI_CHORD(KT_KEY_UP, 0), KT_UI_CHORD('k', 0)),
    KT_UI_ROW(KT_TABLE_DOWN, "table.down", "select", NAV, MOVE, 3, 2, KT_UI_CHORD(KT_KEY_DOWN, 0),
              KT_UI_CHORD('j', 0)),
    KT_UI_ROW(KT_TABLE_PAGE_UP, "table.page_up", "page", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_PAGE_UP, 0),
              KT_UI_CHORD('b', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TABLE_PAGE_DOWN, "table.page_down", "page", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_PAGE_DOWN, 0),
              KT_UI_CHORD('f', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TABLE_TOP, "table.top", "first", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_HOME, 0), KT_UI_CHORD('g', 0)),
    KT_UI_ROW(KT_TABLE_BOTTOM, "table.bottom", "last", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_END, 0),
              KT_UI_CHORD('G', 0)),
    KT_UI_ROW(KT_TABLE_CHOOSE, "table.choose", "open", NAV, MOVE, 2, 1, KT_UI_CHORD(KT_KEY_ENTER, 0), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TABLE_SORT, "table.sort", "sort", KT_TABLE_SCOPE_SORTABLE, MOVE, 0, 1, KT_UI_CHORD('s', 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TABLE_REVERSE, "table.reverse", "reverse", KT_TABLE_SCOPE_SORTABLE, MOVE, -1, 1,
              KT_UI_CHORD('S', 0), KT_UI_NO_CHORD),
};

static size_t columns_of(const struct kt_table *t) {
    return t->column_count < KT_TABLE_COLUMNS ? t->column_count : KT_TABLE_COLUMNS;
}

static struct kt_ui_rows rows_of(const struct kt_table *t) {
    struct kt_ui_rows rows;
    rows.count = t->count;
    rows.height = NULL;
    rows.context = NULL;
    return rows;
}

/* The area less the header, the status line and the scroll bar's column. */
static struct kt_rect body_of(const struct kt_table *t, struct kt_rect area) {
    struct kt_ui_rows rows = rows_of(t);
    if (t->header) (void)kt_rect_cut(area, KT_SIDE_TOP, 1, &area);
    if (t->status_line && area.height > 1) (void)kt_rect_cut(area, KT_SIDE_BOTTOM, 1, &area);
    if (t->scrollbar && kt_ui_bottom_top(&rows, area.height) > 0) (void)kt_rect_cut(area, KT_SIDE_RIGHT, 1, &area);
    return area;
}

void kt_table_columns(const struct kt_table *t, struct kt_rect area, struct kt_rect *parts) {
    struct kt_constraint constraints[KT_TABLE_COLUMNS];
    size_t i, n = columns_of(t);
    for (i = 0; i < n; i++) constraints[i] = t->columns[i].width;
    /* Two columns for the selection bar, one spare at the right. */
    kt_rect_columns(kt_rect_inset(body_of(t, area), 0, 1, 0, 2), t->gap, constraints, n, parts);
    for (i = 0; i < n; i++) { parts[i].y = area.y; parts[i].height = 1; }
}

static void reveal(struct kt_table *t) {
    struct kt_ui_rows rows = rows_of(t);
    int height = body_of(t, t->area).height;
    size_t last = kt_ui_bottom_top(&rows, height);
    if (t->top > last) t->top = last;
    if (t->selected != KT_NONE) t->top = kt_ui_scroll_to(&rows, t->top, t->selected, height);
}

static enum kt_table_result select_row(struct kt_table *t, long row) {
    size_t before = t->selected;
    if (!t->count) return KT_TABLE_HANDLED;
    if (row < 0) row = 0;
    if ((size_t)row >= t->count) row = (long)t->count - 1;
    t->selected = (size_t)row;
    reveal(t);
    return t->selected != before ? KT_TABLE_MOVED : KT_TABLE_HANDLED;
}

static long page(const struct kt_table *t) {
    int height = body_of(t, t->area).height;
    return height > 1 ? height - 1 : 1;
}

static enum kt_table_result sort(struct kt_table *t, size_t column, bool reverse) {
    size_t n = columns_of(t);
    if (!n) return KT_TABLE_HANDLED;
    if (reverse) t->sort_descending = !t->sort_descending;
    else if (column == t->sort_column) t->sort_descending = !t->sort_descending;
    else { t->sort_column = column < n ? column : 0; t->sort_descending = false; }
    return KT_TABLE_SORTED;
}

static enum kt_table_result perform(struct kt_table *t, int action) {
    long at = t->selected == KT_NONE ? -1 : (long)t->selected;
    size_t next = t->sort_column == KT_NONE ? 0 : t->sort_column + 1;
    switch (action) {
    case KT_TABLE_UP: return select_row(t, at - 1);
    case KT_TABLE_DOWN: return select_row(t, at + 1);
    case KT_TABLE_PAGE_UP: return select_row(t, at - page(t));
    case KT_TABLE_PAGE_DOWN: return select_row(t, at + page(t));
    case KT_TABLE_TOP: return select_row(t, 0);
    case KT_TABLE_BOTTOM: return select_row(t, (long)t->count - 1);
    case KT_TABLE_CHOOSE: return t->selected != KT_NONE ? KT_TABLE_CHOSEN : KT_TABLE_HANDLED;
    case KT_TABLE_SORT: return sort(t, next < columns_of(t) ? next : 0, false);
    case KT_TABLE_REVERSE: return sort(t, t->sort_column, t->sort_column != KT_NONE);
    default: return KT_TABLE_HANDLED;
    }
}

/* A click on a heading sorts by that column. */
static enum kt_table_result heading_click(struct kt_table *t, int x) {
    struct kt_rect parts[KT_TABLE_COLUMNS];
    size_t i, n = columns_of(t);
    if (!t->sortable) return KT_TABLE_HANDLED;
    kt_table_columns(t, t->area, parts);
    for (i = 0; i < n; i++)
        if (x >= parts[i].x && x < parts[i].x + parts[i].width) return sort(t, i, false);
    return KT_TABLE_HANDLED;
}

static enum kt_table_result mouse(struct kt_table *t, const struct kt_msg *msg) {
    struct kt_rect body = body_of(t, t->area);
    struct kt_ui_rows rows = rows_of(t);
    size_t row;
    int x, y, wheel = kt_ui_wheel(msg);
    if (wheel && kt_ui_inside(t->area, msg->u.event.x, msg->u.event.y)) {
        t->top = kt_ui_scroll_by(&rows, t->top, wheel * 3L, body.height);
        return KT_TABLE_HANDLED;
    }
    if (t->header && kt_ui_click(msg, (struct kt_rect){t->area.x, t->area.y, t->area.width, 1}, &x, &y))
        return heading_click(t, msg->u.event.x);
    if (!kt_ui_click(msg, body, &x, &y)) return kt_ui_click(msg, t->area, &x, &y) ? KT_TABLE_HANDLED : KT_TABLE_IGNORED;
    row = kt_ui_row_at(&rows, t->top, y, body.height);
    if (row == KT_NONE) return KT_TABLE_HANDLED;
    if (row == t->selected) return KT_TABLE_CHOSEN;
    t->selected = row;
    return KT_TABLE_MOVED;
}

enum kt_table_result kt_table_update(struct kt_table *t, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action;
    (void)cmds;
    if (msg->type == KT_MSG_MOUSE) return mouse(t, msg);
    action = kt_ui_action(t->keys, KT_TABLE_ACTIONS, &t->chord, kt_table_scope(t), msg, &consumed);
    if (action >= 0) return perform(t, action);
    return consumed ? KT_TABLE_HANDLED : KT_TABLE_IGNORED;
}

uint32_t kt_table_scope(const struct kt_table *t) {
    return KT_TABLE_SCOPE_BROWSE | (t->sortable ? KT_TABLE_SCOPE_SORTABLE : 0);
}

struct kt_keymap kt_table_keymap(struct kt_table *t) {
    struct kt_keymap map;
    map.bindings = t->keys;
    map.count = KT_TABLE_ACTIONS;
    return map;
}

void kt_table_init(struct kt_table *t, const struct kt_table_column *columns, size_t column_count,
                   kt_table_cell_fn cell, void *context, size_t count) {
    struct kt_table_style *s = &t->style;
    memset(t, 0, sizeof(*t));
    t->columns = columns;
    t->column_count = column_count;
    t->cell = cell;
    t->context = context;
    t->count = count;
    t->selected = count ? 0 : KT_NONE;
    t->sort_column = KT_NONE;
    t->focused = t->header = t->scrollbar = true;
    t->gap = 2;
    t->empty = "Nothing here";
    s->header = KT_INK(KT_COLOR_MUTED, 0);
    s->sort = KT_INK(KT_COLOR_ACCENT, 0);
    s->cell = KT_INK(KT_COLOR_TEXT, 0);
    s->cell_selected = KT_INK(KT_COLOR_ON_SELECTION, KT_BOLD);
    s->selection = KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, 0);
    s->bar = KT_INK(KT_COLOR_ACCENT, 0);
    s->bar_unfocused = KT_INK(KT_COLOR_FAINT, 0);
    s->status = KT_INK(KT_COLOR_MUTED, 0);
    s->empty = KT_INK(KT_COLOR_MUTED, 0);
    s->scrollbar = KT_INK(KT_COLOR_FAINT, 0);
    s->thumb = KT_INK(KT_COLOR_MUTED, 0);
    memcpy(t->keys, table_keys, sizeof(table_keys));
}

void kt_table_set_count(struct kt_table *t, size_t count) {
    t->count = count;
    if (!count) t->selected = KT_NONE;
    else if (t->selected == KT_NONE) t->selected = 0;
    else if (t->selected >= count) t->selected = count - 1;
    reveal(t);
}

void kt_table_place(struct kt_table *t, struct kt_rect area) {
    t->area = area;
    reveal(t);
}

/* ---- view --------------------------------------------------------------- */

static void draw_heading(const struct kt_table *t, struct kt_canvas *canvas, struct kt_rect part, size_t column) {
    const struct kt_table_column *c = &t->columns[column];
    const char *mark = t->sort_descending ? "\xe2\x96\xbc" : "\xe2\x96\xb2";
    int used;
    if (column != t->sort_column) {
        (void)kt_text_aligned(canvas, part, c->title, c->align, t->style.header);
        return;
    }
    /* The indicator follows the title and is never cut off by it. */
    used = kt_text_aligned(canvas, kt_rect_inset(part, 0, 2, 0, 0), c->title, KT_ALIGN_LEFT, t->style.header);
    (void)kt_text_aligned(canvas, (struct kt_rect){part.x + used + 1, part.y, 1, 1}, mark, KT_ALIGN_LEFT, t->style.sort);
}

static void draw_row(const struct kt_table *t, struct kt_canvas *canvas, const struct kt_rect *parts, struct kt_rect line,
                     size_t row) {
    bool selected = row == t->selected;
    struct kt_style base = selected && t->focused ? t->style.cell_selected : t->style.cell;
    size_t column, n = columns_of(t);
    if (selected) {
        base.attrs |= KT_BOLD;
        kt_ui_selection(canvas, line, t->focused, t->style.selection, t->style.bar, t->style.bar_unfocused);
    }
    for (column = 0; column < n; column++) {
        struct kt_table_cell cell = {NULL, KT_DEFAULT, 0};
        struct kt_style style = base;
        struct kt_rect part = parts[column];
        t->cell(t->context, row, column, &cell);
        /* A lit row's cells all take the selection's ink (kt_ui_lit). */
        if (cell.color != KT_DEFAULT && !(selected && t->focused)) style.fg = cell.color;
        style.attrs |= cell.attrs;
        part.y = line.y;
        if (cell.text) (void)kt_text_aligned(canvas, part, cell.text, t->columns[column].align, style);
    }
}

static void draw_status(const struct kt_table *t, struct kt_canvas *canvas, struct kt_rect line, int height) {
    char text[96];
    size_t last = t->top + (size_t)height < t->count ? t->top + (size_t)height : t->count;
    if (!t->count) return;
    snprintf(text, sizeof(text), "%zu\xe2\x80\x93%zu of %zu", t->top + 1, last, t->count);
    (void)kt_text_aligned(canvas, line, text, KT_ALIGN_LEFT, t->style.status);
}

void kt_table_view(const struct kt_table *t, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_rect parts[KT_TABLE_COLUMNS], body = body_of(t, area);
    size_t column, row;
    if (area.width < 1 || area.height < 1 || !t->cell) return;
    kt_table_columns(t, area, parts);
    for (column = 0; t->header && column < columns_of(t); column++) draw_heading(t, canvas, parts[column], column);
    if (t->status_line && area.height > 1)
        draw_status(t, canvas, (struct kt_rect){area.x, area.y + area.height - 1, area.width, 1}, body.height);
    if (!t->count) {
        (void)kt_text_aligned(canvas, (struct kt_rect){body.x + 2, body.y, body.width - 2, 1}, t->empty, KT_ALIGN_LEFT,
                              t->style.empty);
        return;
    }
    for (row = t->top; row < t->count && (int)(row - t->top) < body.height; row++)
        draw_row(t, canvas, parts, (struct kt_rect){body.x, body.y + (int)(row - t->top), body.width, 1}, row);
    if (body.width < area.width) {
        struct kt_ui_rows rows = rows_of(t);
        kt_ui_scrollbar(canvas, (struct kt_rect){body.x + body.width, body.y, 1, body.height}, t->count, t->top,
                        kt_ui_rows_shown(&rows, t->top, body.height), t->style.scrollbar, t->style.thumb);
    }
}
