#include "component_internal.h"
#include <string.h>

/* A row of tabs. Each is its label and badge with a column of padding on
 * either side; the layout is worked out from the area whenever it is
 * needed, so the view and the mouse always agree. */

#define ALL 1U
#define JUMP(n) KT_UI_ROW(KT_TABS_JUMP + (n) - 1, "tabs." #n, "jump to tab", ALL, "Move", -1, 1, \
                          KT_UI_CHORD('0' + (n), 0), KT_UI_NO_CHORD)
#define MARKER 2   /* an arrow and a space at a side that hides tabs */

static const struct kt_binding tabs_keys[KT_TABS_ACTIONS] = {
    KT_UI_ROW(KT_TABS_NEXT, "tabs.next", "next tab", ALL, "Move", 1, 2, KT_UI_CHORD(KT_KEY_TAB, 0),
              KT_UI_CHORD(']', 0)),
    KT_UI_ROW(KT_TABS_PREVIOUS, "tabs.previous", "previous tab", ALL, "Move", -1, 2,
              KT_UI_CHORD(KT_KEY_TAB, KT_MOD_SHIFT), KT_UI_CHORD('[', 0)),
    JUMP(1), JUMP(2), JUMP(3), JUMP(4), JUMP(5), JUMP(6), JUMP(7), JUMP(8), JUMP(9),
};

/* ---- layout ------------------------------------------------------------- */

struct strip { int pad, gap; size_t first, last; bool before, after; };

static int tab_width(const struct kt_tabs *t, size_t i, int pad) {
    const struct kt_tab *tab = &t->tabs[i];
    int width = kt_text_width(tab->label) + 2 * pad;
    if (tab->badge && tab->badge[0]) width += 1 + kt_text_width(tab->badge);
    return width;
}

/* The active tab keeps its padding, so its fill never touches the text. */
static int width_of(const struct kt_tabs *t, const struct strip *s, size_t i) {
    return tab_width(t, i, i == t->active ? 1 : s->pad);
}

static int run_width(const struct kt_tabs *t, const struct strip *s, size_t first, size_t last) {
    int width = 0;
    size_t i;
    for (i = first; i < last; i++) width += width_of(t, s, i) + (i > first ? s->gap : 0);
    return width + (first > 0 ? MARKER : 0) + (last < t->count ? MARKER : 0);
}

/* Grows a window around the active tab, a tab at a time on alternate sides. */
static void window(const struct kt_tabs *t, struct strip *s, int width) {
    bool right = true;
    s->first = t->active;
    s->last = t->active + 1;
    for (;;) {
        bool can_right = s->last < t->count && run_width(t, s, s->first, s->last + 1) <= width;
        bool can_left = s->first > 0 && run_width(t, s, s->first - 1, s->last) <= width;
        if (!can_right && !can_left) break;
        if ((right && can_right) || !can_left) s->last++;
        else s->first--;
        right = !right;
    }
}

static struct strip layout(const struct kt_tabs *t, int width) {
    struct strip s = {1, 0, 0, 0, false, false};
    s.last = t->count;
    if (!t->count || run_width(t, &s, 0, t->count) <= width) return s;
    s.pad = 0;
    s.gap = 1;
    if (run_width(t, &s, 0, t->count) > width) window(t, &s, width);
    s.before = s.first > 0;
    s.after = s.last < t->count;
    return s;
}

struct kt_rect kt_tabs_rect(const struct kt_tabs *t, struct kt_rect area, size_t index) {
    struct strip s = layout(t, area.width);
    struct kt_rect r = {area.x, area.y, 0, area.height > 0 ? 1 : 0};
    size_t i;
    if (index < s.first || index >= s.last) return r;
    r.x += s.before ? MARKER : 0;
    for (i = s.first; i < index; i++) r.x += width_of(t, &s, i) + s.gap;
    r.width = width_of(t, &s, index);
    return r;
}

/* ---- input -------------------------------------------------------------- */

static enum kt_tabs_result go(struct kt_tabs *t, size_t index) {
    if (index >= t->count || index == t->active) return KT_TABS_HANDLED;
    t->active = index;
    return KT_TABS_CHANGED;
}

static enum kt_tabs_result click(struct kt_tabs *t, const struct kt_msg *msg) {
    size_t i;
    int x, y;
    if (!kt_ui_click(msg, t->area, &x, &y)) return KT_TABS_IGNORED;
    for (i = 0; i < t->count; i++)
        if (kt_ui_inside(kt_tabs_rect(t, t->area, i), msg->u.event.x, msg->u.event.y)) return go(t, i);
    return KT_TABS_HANDLED;
}

enum kt_tabs_result kt_tabs_update(struct kt_tabs *t, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action;
    (void)cmds;
    if (msg->type == KT_MSG_MOUSE) return click(t, msg);
    action = kt_ui_action(t->keys, KT_TABS_ACTIONS, &t->chord, ALL, msg, &consumed);
    if (action < 0) return consumed ? KT_TABS_HANDLED : KT_TABS_IGNORED;
    if (!t->count) return KT_TABS_HANDLED;
    if (action == KT_TABS_NEXT) return go(t, (t->active + 1) % t->count);
    if (action == KT_TABS_PREVIOUS) return go(t, (t->active + t->count - 1) % t->count);
    return go(t, (size_t)(action - KT_TABS_JUMP));
}

uint32_t kt_tabs_scope(const struct kt_tabs *t) { (void)t; return ALL; }

struct kt_keymap kt_tabs_keymap(struct kt_tabs *t) {
    struct kt_keymap map;
    map.bindings = t->keys;
    map.count = KT_TABS_ACTIONS;
    return map;
}

void kt_tabs_init(struct kt_tabs *t, const struct kt_tab *items, size_t count) {
    memset(t, 0, sizeof(*t));
    t->tabs = items;
    t->count = count;
    t->style.active = KT_STYLE(KT_COLOR_ON_ACCENT, KT_COLOR_ACCENT, KT_BOLD);
    t->style.inactive = KT_INK(KT_COLOR_MUTED, 0);
    t->style.badge = KT_INK(KT_COLOR_WARNING, 0);
    t->style.badge_active = KT_INK(KT_COLOR_ON_ACCENT, 0);
    t->style.overflow = KT_INK(KT_COLOR_FAINT, 0);
    memcpy(t->keys, tabs_keys, sizeof(tabs_keys));
}

void kt_tabs_place(struct kt_tabs *t, struct kt_rect area) { t->area = area; }

/* ---- view --------------------------------------------------------------- */

static void draw_tab(const struct kt_tabs *t, struct kt_canvas *canvas, struct kt_rect r, size_t i, int pad) {
    const struct kt_tab *tab = &t->tabs[i];
    bool active = i == t->active;
    struct kt_style badge = active ? t->style.badge_active : t->style.badge;
    struct kt_rect text = kt_rect_inset(r, 0, pad, 0, pad);
    int used;
    if (active) kt_fill(canvas, r, t->style.active);
    used = kt_text_aligned(canvas, text, tab->label, KT_ALIGN_LEFT, active ? KT_STYLE(KT_KEEP, KT_KEEP, KT_BOLD)
                                                                           : t->style.inactive);
    if (!tab->badge || !tab->badge[0]) return;
    if (!active && tab->badge_color != KT_DEFAULT) badge.fg = tab->badge_color;
    (void)kt_text_aligned(canvas, kt_rect_inset(text, 0, 0, 0, used + 1), tab->badge, KT_ALIGN_LEFT, badge);
}

void kt_tabs_view(const struct kt_tabs *t, struct kt_canvas *canvas, struct kt_rect area) {
    struct strip s = layout(t, area.width);
    size_t i;
    if (area.width < 1 || area.height < 1) return;
    if (s.before) (void)kt_text_aligned(canvas, area, "\xe2\x80\xb9", KT_ALIGN_LEFT, t->style.overflow);
    if (s.after) (void)kt_text_aligned(canvas, area, "\xe2\x80\xba", KT_ALIGN_RIGHT, t->style.overflow);
    for (i = s.first; i < s.last; i++)
        draw_tab(t, canvas, kt_tabs_rect(t, area, i), i, i == t->active ? 1 : s.pad);
}
