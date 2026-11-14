#include "component_internal.h"
#include <string.h>

/* A question in a dialog panel with buttons. */

#define ALL 1U
#define BUTTON_GAP 2

static const struct kt_binding confirm_keys[KT_CONFIRM_ACTIONS] = {
    KT_UI_ROW(KT_CONFIRM_ACCEPT, "confirm.accept", "accept", ALL, NULL, 2, 1, KT_UI_CHORD('y', 0), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_CONFIRM_CANCEL, "confirm.cancel", "cancel", ALL, NULL, 1, 2, KT_UI_CHORD(KT_KEY_ESCAPE, 0),
              KT_UI_CHORD('n', 0)),
    KT_UI_ROW(KT_CONFIRM_PREVIOUS, "confirm.previous", "button", ALL, NULL, -1, 2, KT_UI_CHORD(KT_KEY_LEFT, 0),
              KT_UI_CHORD(KT_KEY_TAB, KT_MOD_SHIFT)),
    KT_UI_ROW(KT_CONFIRM_NEXT, "confirm.next", "button", ALL, NULL, -1, 2, KT_UI_CHORD(KT_KEY_RIGHT, 0),
              KT_UI_CHORD(KT_KEY_TAB, 0)),
    KT_UI_ROW(KT_CONFIRM_PRESS, "confirm.press", "press", ALL, NULL, -1, 2, KT_UI_CHORD(KT_KEY_ENTER, 0),
              KT_UI_CHORD(' ', 0)),
};

void kt_confirm_init(struct kt_confirm *c, const char *title, const char *body, const char *accept,
                     const char *cancel, bool destructive) {
    struct kt_confirm_style *s = &c->style;
    memset(c, 0, sizeof(*c));
    c->title = title;
    c->body = body;
    c->buttons[0] = cancel ? cancel : "Cancel";
    c->buttons[1] = accept ? accept : "OK";
    c->button_count = 2;
    c->cancel = 0;
    c->accept = c->focus = c->chosen = 1;
    c->destructive = destructive;
    s->border = KT_INK(KT_COLOR_ACCENT, 0);
    s->border_destructive = KT_INK(KT_COLOR_DANGER, 0);
    s->title = KT_INK(KT_COLOR_TEXT, KT_BOLD);
    s->body = KT_INK(KT_COLOR_TEXT, 0);
    s->button = KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SUNKEN, 0);
    s->button_focused = KT_STYLE(KT_COLOR_ON_ACCENT, KT_COLOR_ACCENT, KT_BOLD);
    s->button_destructive = KT_STYLE(KT_COLOR_ON_ACCENT, KT_COLOR_DANGER, KT_BOLD);
    s->hints = kt_ui_hint_style();
    memcpy(c->keys, confirm_keys, sizeof(confirm_keys));
}

uint32_t kt_confirm_scope(const struct kt_confirm *c) { (void)c; return ALL; }

struct kt_keymap kt_confirm_keymap(struct kt_confirm *c) {
    struct kt_keymap map;
    map.bindings = c->keys;
    map.count = KT_CONFIRM_ACTIONS;
    return map;
}

int kt_confirm_height(const struct kt_confirm *c, int width) {
    return kt_paragraph_height(c->body, width - 6) + 6;
}

void kt_confirm_place(struct kt_confirm *c, struct kt_rect area) { c->area = area; }

/* The buttons, left to right, ending at the inside's right edge. */
static void button_rects(const struct kt_confirm *c, struct kt_rect inside, struct kt_rect *rects) {
    int total = 0, x;
    size_t i;
    for (i = 0; i < c->button_count; i++) total += kt_text_width(c->buttons[i]) + 2 + (i ? BUTTON_GAP : 0);
    x = inside.x + inside.width - total;
    for (i = 0; i < c->button_count; i++) {
        int width = kt_text_width(c->buttons[i]) + 2;
        rects[i] = (struct kt_rect){x, inside.y + inside.height - 1, width, 1};
        x += width + BUTTON_GAP;
    }
}

/* Inside the border; a dialog given fewer rows than it needs gives up its
 * blank rows first, so the question keeps the room. */
static bool cramped(const struct kt_confirm *c, struct kt_rect area) { return area.height < kt_confirm_height(c, area.width); }
static struct kt_rect inside_of(const struct kt_confirm *c, struct kt_rect area) {
    return cramped(c, area) ? kt_rect_inset(area, 1, 3, 1, 3) : kt_rect_inset(area, 2, 3, 2, 3);
}

static enum kt_confirm_result press(struct kt_confirm *c, size_t button) {
    if (button >= c->button_count) return KT_CONFIRM_HANDLED;
    c->chosen = button;
    if (button == c->accept) return KT_CONFIRM_ACCEPTED;
    return button == c->cancel ? KT_CONFIRM_CANCELLED : KT_CONFIRM_CHOSEN;
}

static enum kt_confirm_result click(struct kt_confirm *c, const struct kt_msg *msg) {
    struct kt_rect rects[KT_CONFIRM_BUTTONS];
    size_t i;
    int x, y;
    if (!kt_ui_click(msg, c->area, &x, &y)) return msg->type == KT_MSG_MOUSE ? KT_CONFIRM_HANDLED : KT_CONFIRM_IGNORED;
    button_rects(c, inside_of(c, c->area), rects);
    for (i = 0; i < c->button_count; i++)
        if (kt_ui_inside(rects[i], msg->u.event.x, msg->u.event.y)) return press(c, i);
    return KT_CONFIRM_HANDLED;
}

static enum kt_confirm_result perform(struct kt_confirm *c, int action) {
    size_t n = c->button_count ? c->button_count : 1;
    switch (action) {
    case KT_CONFIRM_ACCEPT: return press(c, c->accept);
    case KT_CONFIRM_CANCEL: return press(c, c->cancel);
    case KT_CONFIRM_PREVIOUS: c->focus = (c->focus + n - 1) % n; return KT_CONFIRM_HANDLED;
    case KT_CONFIRM_NEXT: c->focus = (c->focus + 1) % n; return KT_CONFIRM_HANDLED;
    case KT_CONFIRM_PRESS: return press(c, c->focus);
    default: return KT_CONFIRM_HANDLED;
    }
}

enum kt_confirm_result kt_confirm_update(struct kt_confirm *c, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action;
    (void)cmds;
    if (msg->type == KT_MSG_MOUSE) return click(c, msg);
    action = kt_ui_action(c->keys, KT_CONFIRM_ACTIONS, &c->chord, ALL, msg, &consumed);
    if (action >= 0) return perform(c, action);
    return consumed ? KT_CONFIRM_HANDLED : KT_CONFIRM_IGNORED;
}

/* ---- view --------------------------------------------------------------- */

static void lower(char *out, size_t capacity, const char *text) {
    size_t i;
    for (i = 0; text && text[i] && i + 1 < capacity; i++)
        out[i] = text[i] >= 'A' && text[i] <= 'Z' ? (char)(text[i] + 32) : text[i];
    out[i] = '\0';
}

/* The focused button is filled; a destructive one in the danger colour,
 * which its text keeps when the focus is elsewhere. */
static struct kt_style button_style(const struct kt_confirm *c, size_t i) {
    struct kt_style style = c->style.button;
    bool danger = c->destructive && i == c->accept;
    if (i == c->focus) return danger ? c->style.button_destructive : c->style.button_focused;
    if (danger) style.fg = c->style.button_destructive.bg;
    return style;
}

void kt_confirm_view(const struct kt_confirm *c, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_binding keys[KT_CONFIRM_ACTIONS];
    struct kt_help_source hints;
    struct kt_rect inside, rects[KT_CONFIRM_BUTTONS];
    char accept[48];
    size_t i;
    /* The accepting key's hint names what it does, as its button does. */
    memcpy(keys, c->keys, sizeof(keys));
    lower(accept, sizeof(accept), c->accept < c->button_count ? c->buttons[c->accept] : NULL);
    if (accept[0]) keys[KT_CONFIRM_ACCEPT].label = accept;
    hints.bindings = keys;
    hints.count = KT_CONFIRM_ACTIONS;
    hints.scope = ALL;
    (void)kt_ui_dialog(canvas, area, c->title, c->destructive ? c->style.border_destructive : c->style.border,
                       c->style.title, &hints, 1, &c->style.hints);
    inside = inside_of(c, area);
    (void)kt_paragraph(canvas, kt_rect_inset(inside, 0, 0, cramped(c, area) ? 1 : 2, 0), c->body, c->style.body);
    button_rects(c, inside, rects);
    for (i = 0; i < c->button_count; i++) {
        struct kt_style style = button_style(c, i);
        kt_fill(canvas, rects[i], style);
        (void)kt_text_aligned(canvas, rects[i], c->buttons[i], KT_ALIGN_CENTER, style);
    }
}
