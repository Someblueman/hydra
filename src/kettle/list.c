#include "component_internal.h"
#include <string.h>

/* A selectable, scrolling list over the caller's items. A row is an item,
 * or while a filter is applied a match; the selection and the top of the
 * view are rows. Items are read through the callback whenever they are
 * needed, so a list of any length costs nothing to keep. */

#define NAV (KT_LIST_SCOPE_BROWSE | KT_LIST_SCOPE_FILTERING)
#define MOVE "Move"

static const struct kt_binding list_keys[KT_LIST_ACTIONS] = {
    KT_UI_ROW(KT_LIST_UP, "list.up", "select", NAV, MOVE, 3, 2, KT_UI_CHORD(KT_KEY_UP, 0), KT_UI_CHORD('k', 0)),
    KT_UI_ROW(KT_LIST_DOWN, "list.down", "select", NAV, MOVE, 3, 2, KT_UI_CHORD(KT_KEY_DOWN, 0), KT_UI_CHORD('j', 0)),
    KT_UI_ROW(KT_LIST_PAGE_UP, "list.page_up", "page", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_PAGE_UP, 0),
              KT_UI_CHORD('b', KT_MOD_CTRL)),
    KT_UI_ROW(KT_LIST_PAGE_DOWN, "list.page_down", "page", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_PAGE_DOWN, 0),
              KT_UI_CHORD('f', KT_MOD_CTRL)),
    KT_UI_ROW(KT_LIST_TOP, "list.top", "first", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_HOME, 0), KT_UI_CHORD('g', 0)),
    KT_UI_ROW(KT_LIST_BOTTOM, "list.bottom", "last", NAV, MOVE, -1, 2, KT_UI_CHORD(KT_KEY_END, 0),
              KT_UI_CHORD('G', 0)),
    KT_UI_ROW(KT_LIST_CHOOSE, "list.choose", "open", KT_LIST_SCOPE_BROWSE, MOVE, 2, 1,
              KT_UI_CHORD(KT_KEY_ENTER, 0), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_LIST_FILTER, "list.filter", "filter", KT_LIST_SCOPE_FILTERABLE, MOVE, 1, 1, KT_UI_CHORD('/', 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_LIST_FILTER_ACCEPT, "list.filter_accept", "keep filter", KT_LIST_SCOPE_FILTERING, MOVE, 2, 1,
              KT_UI_CHORD(KT_KEY_ENTER, 0), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_LIST_FILTER_CLEAR, "list.filter_clear", "clear filter",
              KT_LIST_SCOPE_FILTERING | KT_LIST_SCOPE_FILTERED, MOVE, 2, 1, KT_UI_CHORD(KT_KEY_ESCAPE, 0),
              KT_UI_NO_CHORD),
};

/* ---- rows --------------------------------------------------------------- */

static bool filter_applied(const struct kt_list *l) { return l->matches && l->filter.length > 0; }

size_t kt_list_rows(const struct kt_list *l) { return filter_applied(l) ? l->match_count : l->count; }

size_t kt_list_row_item(const struct kt_list *l, size_t row) {
    if (row >= kt_list_rows(l)) return KT_NONE;
    return filter_applied(l) ? l->matches[row].index : row;
}

size_t kt_list_index(const struct kt_list *l) { return kt_list_row_item(l, l->selected); }

static void row_item(const struct kt_list *l, size_t row, struct kt_list_item *item) {
    memset(item, 0, sizeof(*item));
    if (row < kt_list_rows(l) && l->item) l->item(l->context, kt_list_row_item(l, row), item);
    /* Headers are left out of matches, so a filtered row is never one. */
    if (filter_applied(l)) item->header = false;
}

static bool is_header(const struct kt_list *l, size_t row) {
    struct kt_list_item item;
    row_item(l, row, &item);
    return item.header;
}

static int row_height(const void *context, size_t row) {
    const struct kt_list *l = context;
    return is_header(l, row) ? 1 : (l->item_height == 2 ? 2 : 1);
}

static struct kt_ui_rows rows_of(const struct kt_list *l) {
    struct kt_ui_rows rows;
    rows.count = kt_list_rows(l);
    rows.height = row_height;
    rows.context = l;
    return rows;
}

/* The nearest row from `from` in direction `step` that can be selected. */
static size_t selectable(const struct kt_list *l, size_t from, int step) {
    size_t count = kt_list_rows(l), row = from;
    while (row < count) {
        if (!is_header(l, row)) return row;
        if (step < 0 && row == 0) return KT_NONE;
        row = step < 0 ? row - 1 : row + 1;
    }
    return KT_NONE;
}

/* ---- geometry ----------------------------------------------------------- */

static bool shows_filter(const struct kt_list *l) { return l->filtering || filter_applied(l); }

/* The rows area: the area less the filter bar, the status line and the
 * scroll bar's column. */
static struct kt_rect body_of(const struct kt_list *l, struct kt_rect area) {
    struct kt_ui_rows rows = rows_of(l);
    if (shows_filter(l)) (void)kt_rect_cut(area, KT_SIDE_TOP, 1, &area);
    if (l->status_line && area.height > 1) (void)kt_rect_cut(area, KT_SIDE_BOTTOM, 1, &area);
    if (l->scrollbar && kt_ui_bottom_top(&rows, area.height) > 0) (void)kt_rect_cut(area, KT_SIDE_RIGHT, 1, &area);
    return area;
}

static int body_height(const struct kt_list *l) { return body_of(l, l->area).height; }

/* Keeps the selection in view, with its section's header when there is room. */
static void reveal(struct kt_list *l) {
    struct kt_ui_rows rows = rows_of(l);
    int height = body_height(l);
    size_t last = kt_ui_bottom_top(&rows, height);
    if (l->top > last) l->top = last;
    if (l->selected == KT_NONE) return;
    l->top = kt_ui_scroll_to(&rows, l->top, l->selected, height);
    if (l->top == l->selected && l->top > 0 && is_header(l, l->top - 1) && height > row_height(l, l->selected))
        l->top--;
}

static enum kt_list_result select_row(struct kt_list *l, size_t row) {
    size_t before = kt_list_index(l);
    if (row != KT_NONE) l->selected = row;
    reveal(l);
    return kt_list_index(l) != before ? KT_LIST_MOVED : KT_LIST_HANDLED;
}

static size_t first_row(const struct kt_list *l) { return selectable(l, 0, 1); }

static size_t last_row(const struct kt_list *l) {
    size_t count = kt_list_rows(l);
    return count ? selectable(l, count - 1, -1) : KT_NONE;
}

/* `steps` selectable rows up or down from the selection, stopping at the ends. */
static size_t step_rows(const struct kt_list *l, long steps) {
    size_t row = l->selected;
    int step = steps < 0 ? -1 : 1;
    if (row == KT_NONE) return first_row(l);
    for (; steps != 0; steps -= step) {
        size_t next;
        if (step < 0 && row == 0) break;
        next = selectable(l, step < 0 ? row - 1 : row + 1, step);
        if (next == KT_NONE) break;
        row = next;
    }
    return row;
}

static long page(const struct kt_list *l) {
    struct kt_ui_rows rows = rows_of(l);
    size_t shown = kt_ui_rows_shown(&rows, l->top, body_height(l));
    return shown > 1 ? (long)shown - 1 : 1;
}

/* ---- filtering ---------------------------------------------------------- */

static void add_match(struct kt_list *l, size_t index, const char *pattern) {
    struct kt_list_item item;
    int score;
    memset(&item, 0, sizeof(item));
    l->item(l->context, index, &item);
    if (item.header || !item.title) return;
    score = kt_fuzzy(pattern, item.title, NULL, 0);
    if (score < 0) return;
    l->matches[l->match_count].index = index;
    l->matches[l->match_count++].score = score;
}

/* Matches the filter again; the selection stays on its item if it still
 * shows, and otherwise goes to the first row. */
static void refilter(struct kt_list *l, size_t keep) {
    const char *pattern = kt_textinput_value(&l->filter);
    size_t i;
    l->match_count = 0;
    if (filter_applied(l) && l->item)
        for (i = 0; i < l->count && l->match_count < l->match_capacity; i++) add_match(l, i, pattern);
    if (l->ranked) kt_fuzzy_rank(l->matches, l->match_count);
    l->top = 0;
    l->selected = KT_NONE;
    if (keep == KT_NONE || !kt_list_select(l, keep)) l->selected = first_row(l);
    reveal(l);
}

static enum kt_list_result filtered(struct kt_list *l, size_t before) {
    refilter(l, before);
    return kt_list_index(l) != before ? KT_LIST_MOVED : KT_LIST_HANDLED;
}

void kt_list_set_filter(struct kt_list *l, const char *text) {
    kt_textinput_set(&l->filter, text);
    refilter(l, kt_list_index(l));
}

static enum kt_list_result clear_filter(struct kt_list *l) {
    size_t before = kt_list_index(l);
    l->filtering = l->filter.focused = false;
    kt_textinput_set(&l->filter, "");
    return filtered(l, before);
}

/* ---- input -------------------------------------------------------------- */

static enum kt_list_result perform(struct kt_list *l, int action) {
    switch (action) {
    case KT_LIST_UP: return select_row(l, step_rows(l, -1));
    case KT_LIST_DOWN: return select_row(l, step_rows(l, 1));
    case KT_LIST_PAGE_UP: return select_row(l, step_rows(l, -page(l)));
    case KT_LIST_PAGE_DOWN: return select_row(l, step_rows(l, page(l)));
    case KT_LIST_TOP: return select_row(l, first_row(l));
    case KT_LIST_BOTTOM: return select_row(l, last_row(l));
    case KT_LIST_CHOOSE: return kt_list_index(l) != KT_NONE ? KT_LIST_CHOSEN : KT_LIST_HANDLED;
    case KT_LIST_FILTER: l->filtering = l->filter.focused = true; reveal(l); return KT_LIST_HANDLED;
    case KT_LIST_FILTER_ACCEPT: l->filtering = l->filter.focused = false; reveal(l); return KT_LIST_HANDLED;
    case KT_LIST_FILTER_CLEAR: return clear_filter(l);
    default: return KT_LIST_HANDLED;
    }
}

/* While a filter is typed, characters and pastes are its text; other keys
 * try the list's bindings first and then the text input's. */
static enum kt_list_result filter_input(struct kt_list *l, const struct kt_msg *msg, bool *done) {
    size_t before = kt_list_index(l);
    enum kt_textinput_result r;
    *done = true;
    if (msg->type == KT_MSG_KEY && !kt_event_text(&msg->u.event)) { *done = false; return KT_LIST_IGNORED; }
    if (msg->type != KT_MSG_KEY && msg->type != KT_MSG_PASTE) { *done = false; return KT_LIST_IGNORED; }
    r = kt_textinput_update(&l->filter, msg, NULL);
    return r == KT_TEXTINPUT_CHANGED ? filtered(l, before) : KT_LIST_HANDLED;
}

static enum kt_list_result editing_keys(struct kt_list *l, const struct kt_msg *msg) {
    size_t before = kt_list_index(l);
    enum kt_textinput_result r = kt_textinput_update(&l->filter, msg, NULL);
    if (r == KT_TEXTINPUT_CHANGED) return filtered(l, before);
    return r == KT_TEXTINPUT_IGNORED ? KT_LIST_IGNORED : KT_LIST_HANDLED;
}

static enum kt_list_result mouse(struct kt_list *l, const struct kt_msg *msg) {
    struct kt_rect body = body_of(l, l->area);
    struct kt_ui_rows rows = rows_of(l);
    size_t row;
    int x, y, wheel = kt_ui_wheel(msg);
    if (wheel && kt_ui_inside(l->area, msg->u.event.x, msg->u.event.y)) {
        l->top = kt_ui_scroll_by(&rows, l->top, wheel * 3L, body.height);
        return KT_LIST_HANDLED;
    }
    if (!kt_ui_click(msg, body, &x, &y)) return kt_ui_click(msg, l->area, &x, &y) ? KT_LIST_HANDLED : KT_LIST_IGNORED;
    row = kt_ui_row_at(&rows, l->top, y, body.height);
    if (row == KT_NONE || is_header(l, row)) return KT_LIST_HANDLED;
    if (row == l->selected) return KT_LIST_CHOSEN;
    l->selected = row;
    return KT_LIST_MOVED;
}

enum kt_list_result kt_list_update(struct kt_list *l, const struct kt_msg *msg, struct kt_cmds *cmds) {
    enum kt_list_result result;
    bool done = false, consumed;
    int action;
    (void)cmds;
    if (msg->type == KT_MSG_MOUSE) return mouse(l, msg);
    if (l->filtering) {
        result = filter_input(l, msg, &done);
        if (done) return result;
    }
    action = kt_ui_action(l->keys, KT_LIST_ACTIONS, &l->chord, kt_list_scope(l), msg, &consumed);
    if (action >= 0) return perform(l, action);
    if (consumed) return KT_LIST_HANDLED;
    return l->filtering ? editing_keys(l, msg) : KT_LIST_IGNORED;
}

uint32_t kt_list_scope(const struct kt_list *l) {
    uint32_t scope;
    if (l->filtering) return KT_LIST_SCOPE_FILTERING;
    scope = KT_LIST_SCOPE_BROWSE;
    if (l->matches) scope |= KT_LIST_SCOPE_FILTERABLE;
    if (filter_applied(l)) scope |= KT_LIST_SCOPE_FILTERED;
    return scope;
}

struct kt_keymap kt_list_keymap(struct kt_list *l) {
    struct kt_keymap map;
    map.bindings = l->keys;
    map.count = KT_LIST_ACTIONS;
    return map;
}

/* ---- set up ------------------------------------------------------------- */

void kt_list_init(struct kt_list *l, kt_list_item_fn item, void *context, size_t count) {
    struct kt_list_style *s = &l->style;
    memset(l, 0, sizeof(*l));
    l->item = item;
    l->context = context;
    l->count = count;
    l->item_height = 1;
    l->focused = l->ranked = l->scrollbar = true;
    l->empty = "Nothing here";
    kt_textinput_init(&l->filter, NULL, 0);
    l->filter.prompt = "/ ";
    l->filter.style.field = KT_STYLE(KT_KEEP, KT_KEEP, 0);
    l->filter.placeholder = "filter";
    s->title = KT_INK(KT_COLOR_TEXT, 0);
    s->title_selected = KT_INK(KT_COLOR_ON_SELECTION, KT_BOLD);
    s->detail = KT_INK(KT_COLOR_MUTED, 0);
    s->header = KT_INK(KT_COLOR_MUTED, KT_BOLD);
    s->header_rule = KT_INK(KT_COLOR_FAINT, 0);
    s->selection = KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, 0);
    s->bar = KT_INK(KT_COLOR_ACCENT, 0);
    s->bar_unfocused = KT_INK(KT_COLOR_FAINT, 0);
    s->match = KT_INK(KT_COLOR_ACCENT, KT_UNDERLINE);
    s->status = KT_INK(KT_COLOR_MUTED, 0);
    s->empty = KT_INK(KT_COLOR_MUTED, 0);
    s->scrollbar = KT_INK(KT_COLOR_FAINT, 0);
    s->thumb = KT_INK(KT_COLOR_MUTED, 0);
    memcpy(l->keys, list_keys, sizeof(list_keys));
    l->selected = first_row(l);
}

void kt_list_filter_storage(struct kt_list *l, struct kt_match *matches, size_t capacity) {
    l->matches = capacity ? matches : NULL;
    l->match_capacity = capacity;
    refilter(l, kt_list_index(l));
}

void kt_list_set_count(struct kt_list *l, size_t count) {
    size_t keep = kt_list_index(l);
    l->count = count;
    if (keep != KT_NONE && keep >= count) keep = count ? count - 1 : KT_NONE;
    refilter(l, keep);
}

bool kt_list_select(struct kt_list *l, size_t index) {
    size_t row, rows = kt_list_rows(l);
    for (row = 0; row < rows; row++) {
        if (kt_list_row_item(l, row) != index || is_header(l, row)) continue;
        l->selected = row;
        reveal(l);
        return true;
    }
    return false;
}

void kt_list_place(struct kt_list *l, struct kt_rect area) {
    l->area = area;
    kt_textinput_place(&l->filter, (struct kt_rect){area.x + 1, area.y, area.width - 1, 1});
    reveal(l);
}

/* ---- view --------------------------------------------------------------- */

static void draw_header(const struct kt_list *l, struct kt_canvas *canvas, struct kt_rect row, const char *title) {
    kt_rule_styled(canvas, kt_rect_inset(row, 0, 0, 0, 2), title, KT_ALIGN_LEFT, l->style.header, l->style.header_rule);
}

/* The icon, when there is one, and the room after it. */
static struct kt_rect draw_icon(struct kt_canvas *canvas, struct kt_rect line, const struct kt_list_item *item,
                                struct kt_style title) {
    struct kt_style style = title;
    int width;
    if (!item->icon || !item->icon[0]) return line;
    if (item->icon_color != KT_DEFAULT) style.fg = item->icon_color;
    width = kt_text_aligned(canvas, line, item->icon, KT_ALIGN_LEFT, style);
    return kt_rect_inset(line, 0, 0, 0, width + 1);
}

static void draw_item(const struct kt_list *l, struct kt_canvas *canvas, struct kt_rect row,
                      const struct kt_list_item *item, bool selected) {
    struct kt_style title = selected && l->focused ? l->style.title_selected : l->style.title;
    struct kt_rect line = kt_rect_inset((struct kt_rect){row.x, row.y, row.width, 1}, 0, 1, 0, 2);
    const char *pattern = filter_applied(l) ? kt_textinput_value(&l->filter) : NULL;
    if (selected) {
        title.attrs |= KT_BOLD;
        kt_ui_selection(canvas, row, l->focused, l->style.selection, l->style.bar, l->style.bar_unfocused);
    }
    line = draw_icon(canvas, line, item, title);
    if (item->detail && l->item_height == 2) {
        (void)kt_text_aligned(canvas, (struct kt_rect){line.x, row.y + 1, line.width, 1}, item->detail, KT_ALIGN_LEFT,
                              l->style.detail);
    } else if (item->detail) {
        int detail = kt_text_aligned(canvas, line, item->detail, KT_ALIGN_RIGHT, l->style.detail);
        line.width -= detail + 2;
    }
    (void)kt_ui_marked(canvas, line, item->title, pattern, title, l->style.match);
}

static void draw_rows(const struct kt_list *l, struct kt_canvas *canvas, struct kt_rect body) {
    size_t row = l->top, rows = kt_list_rows(l);
    int y = body.y;
    while (row < rows && y < body.y + body.height) {
        struct kt_list_item item;
        int height = row_height(l, row);
        struct kt_rect line = {body.x, y, body.width, height};
        if (line.y + line.height > body.y + body.height) line.height = body.y + body.height - line.y;
        row_item(l, row, &item);
        if (item.header) draw_header(l, canvas, line, item.title);
        else draw_item(l, canvas, line, &item, row == l->selected);
        y += height;
        row++;
    }
}

static void draw_status(const struct kt_list *l, struct kt_canvas *canvas, struct kt_rect line, struct kt_rect body) {
    struct kt_ui_rows rows = rows_of(l);
    size_t shown = kt_ui_rows_shown(&rows, l->top, body.height);
    char text[96];
    if (!rows.count) return;
    snprintf(text, sizeof(text), "%zu\xe2\x80\x93%zu of %zu", l->top + 1, l->top + shown, rows.count);
    (void)kt_text_aligned(canvas, line, text, KT_ALIGN_LEFT, l->style.status);
}

static void draw_filter(const struct kt_list *l, struct kt_canvas *canvas, struct kt_rect line) {
    char count[48];
    int used = 0;
    if (filter_applied(l)) {
        snprintf(count, sizeof(count), "%zu of %zu", l->match_count, l->count);
        used = kt_text_aligned(canvas, line, count, KT_ALIGN_RIGHT, l->style.status);
    }
    kt_textinput_view(&l->filter, canvas, kt_rect_inset(line, 0, used ? used + 2 : 0, 0, 1));
}

void kt_list_view(const struct kt_list *l, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_rect body = body_of(l, area), rest = area, line;
    struct kt_ui_rows rows = rows_of(l);
    if (area.width < 1 || area.height < 1) return;
    if (shows_filter(l)) draw_filter(l, canvas, kt_rect_cut(rest, KT_SIDE_TOP, 1, &rest));
    if (l->status_line && rest.height > 1) {
        line = kt_rect_cut(rest, KT_SIDE_BOTTOM, 1, &rest);
        draw_status(l, canvas, line, body);
    }
    if (!rows.count) {
        const char *empty = filter_applied(l) ? "No matches" : l->empty;
        (void)kt_text_aligned(canvas, kt_rect_inset((struct kt_rect){body.x, body.y, body.width, 1}, 0, 0, 0, 2),
                              empty, KT_ALIGN_LEFT, l->style.empty);
        return;
    }
    draw_rows(l, canvas, body);
    if (body.width < rest.width)
        kt_ui_scrollbar(canvas, (struct kt_rect){body.x + body.width, body.y, 1, body.height}, rows.count, l->top,
                        kt_ui_rows_shown(&rows, l->top, body.height), l->style.scrollbar, l->style.thumb);
}
