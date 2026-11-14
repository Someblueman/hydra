#include "component_internal.h"
#include <string.h>

/* Notifications that expire. Each holds its own one-shot timer, cancelled
 * when it is dismissed or replaced, so no timer outlives what it is for. */

static const struct kt_binding toasts_keys[KT_TOASTS_ACTIONS] = {
    KT_UI_ROW(KT_TOASTS_DISMISS, "toast.dismiss", "dismiss", 1, NULL, -1, 0, KT_UI_NO_CHORD, KT_UI_NO_CHORD),
};

void kt_toasts_init(struct kt_toasts *t, uint64_t tag) {
    memset(t, 0, sizeof(*t));
    t->limit = 3;
    t->timeout_ms = 4000;
    t->width = 48;
    t->tag = tag;
    t->style.body = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_RAISED, 0);
    t->style.info = KT_INK(KT_COLOR_INFO, 0);
    t->style.success = KT_INK(KT_COLOR_SUCCESS, 0);
    t->style.warning = KT_INK(KT_COLOR_WARNING, 0);
    t->style.error = KT_INK(KT_COLOR_DANGER, 0);
    memcpy(t->keys, toasts_keys, sizeof(toasts_keys));
}

static size_t limit_of(const struct kt_toasts *t) {
    if (!t->limit) return 1;
    return t->limit < KT_TOASTS_MAX ? t->limit : KT_TOASTS_MAX;
}

static void remove_at(struct kt_toasts *t, struct kt_cmds *cmds, size_t i) {
    if (i >= t->count) return;
    if (cmds && t->items[i].timer) kt_cmd_cancel(cmds, t->items[i].timer);
    memmove(&t->items[i], &t->items[i + 1], (t->count - i - 1) * sizeof(t->items[0]));
    t->count--;
}

void kt_toasts_push(struct kt_toasts *t, struct kt_cmds *cmds, enum kt_toast_kind kind, const char *text) {
    struct kt_toast *item;
    while (t->count >= limit_of(t)) remove_at(t, cmds, 0);
    item = &t->items[t->count++];
    memset(item, 0, sizeof(*item));
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    item->kind = kind;
    item->timer = cmds && t->timeout_ms ? kt_cmd_timer(cmds, t->tag, t->timeout_ms, 0) : 0;
}

void kt_toasts_dismiss(struct kt_toasts *t, struct kt_cmds *cmds) {
    if (t->count) remove_at(t, cmds, t->count - 1);
}

void kt_toasts_clear(struct kt_toasts *t, struct kt_cmds *cmds) {
    while (t->count) remove_at(t, cmds, t->count - 1);
}

void kt_toasts_place(struct kt_toasts *t, struct kt_rect area) { t->area = area; }

struct kt_keymap kt_toasts_keymap(struct kt_toasts *t) {
    struct kt_keymap map;
    map.bindings = t->keys;
    map.count = KT_TOASTS_ACTIONS;
    return map;
}

/* Toast i's row in an area: as wide as its text, the newest on the bottom
 * row, all of them against the right edge. */
static struct kt_rect toast_rect(const struct kt_toasts *t, struct kt_rect area, size_t i) {
    int width = kt_text_width(t->items[i].text) + 5;
    if (width > t->width) width = t->width;
    if (width > area.width) width = area.width;
    struct kt_rect r = {area.x + area.width - width, area.y + area.height - (int)(t->count - i), width, 1};
    if (r.y < area.y) r.height = 0;
    return r;
}

static enum kt_toasts_result expire(struct kt_toasts *t, const struct kt_msg *msg) {
    size_t i;
    for (i = 0; i < t->count; i++) {
        if (t->items[i].timer != msg->u.timer.id) continue;
        t->items[i].timer = 0;   /* a one-shot timer is finished once it fires */
        remove_at(t, NULL, i);
        return KT_TOASTS_EXPIRED;
    }
    return KT_TOASTS_IGNORED;
}

static enum kt_toasts_result click(struct kt_toasts *t, const struct kt_msg *msg, struct kt_cmds *cmds) {
    size_t i;
    int x, y;
    for (i = 0; i < t->count; i++) {
        if (!kt_ui_click(msg, toast_rect(t, t->area, i), &x, &y)) continue;
        remove_at(t, cmds, i);
        return KT_TOASTS_DISMISSED;
    }
    return KT_TOASTS_IGNORED;
}

enum kt_toasts_result kt_toasts_update(struct kt_toasts *t, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action;
    if (msg->type == KT_MSG_TIMER) return msg->tag == t->tag ? expire(t, msg) : KT_TOASTS_IGNORED;
    if (msg->type == KT_MSG_MOUSE) return click(t, msg, cmds);
    action = kt_ui_action(t->keys, KT_TOASTS_ACTIONS, &t->chord, 1, msg, &consumed);
    if (action == KT_TOASTS_DISMISS && t->count) {
        kt_toasts_dismiss(t, cmds);
        return KT_TOASTS_DISMISSED;
    }
    return action >= 0 || consumed ? KT_TOASTS_HANDLED : KT_TOASTS_IGNORED;
}

static struct kt_style kind_style(const struct kt_toasts *t, enum kt_toast_kind kind) {
    switch (kind) {
    case KT_TOAST_SUCCESS: return t->style.success;
    case KT_TOAST_WARNING: return t->style.warning;
    case KT_TOAST_ERROR: return t->style.error;
    default: return t->style.info;
    }
}

static const char *kind_icon(enum kt_toast_kind kind) {
    switch (kind) {
    case KT_TOAST_SUCCESS: return "\xe2\x9c\x93";
    case KT_TOAST_WARNING: return "\xe2\x96\xb2";
    case KT_TOAST_ERROR: return "\xe2\x9c\x97";
    default: return "\xe2\x80\xa2";
    }
}

void kt_toasts_view(const struct kt_toasts *t, struct kt_canvas *canvas, struct kt_rect area) {
    size_t i;
    for (i = 0; i < t->count; i++) {
        struct kt_rect r = toast_rect(t, area, i);
        struct kt_style tone = kind_style(t, t->items[i].kind);
        if (r.height < 1 || r.width < 4) continue;
        kt_fill(canvas, r, t->style.body);
        kt_box(canvas, r, &kt_border_bar, tone);
        (void)kt_text_aligned(canvas, (struct kt_rect){r.x + 2, r.y, 1, 1}, kind_icon(t->items[i].kind), KT_ALIGN_LEFT,
                              tone);
        (void)kt_text_aligned(canvas, kt_rect_inset(r, 0, 1, 0, 4), t->items[i].text, KT_ALIGN_LEFT,
                              KT_INK(KT_KEEP, 0));
    }
}
