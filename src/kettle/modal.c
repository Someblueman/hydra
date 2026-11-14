#include "component_internal.h"
#include <string.h>

/* Dialogs layered over the application's view: a stack, the top of which
 * has the input. */

void kt_modals_init(struct kt_modals *m) {
    memset(m, 0, sizeof(*m));
    m->backdrop = KT_STYLE(KT_COLOR_FAINT, KT_KEEP, 0);
}

static int modal_height(const struct kt_modal *modal, int width) {
    return modal->measure ? modal->measure(modal->component, width) : modal->height;
}

/* Centred, a little above the middle, and never larger than the area less a
 * margin, so the base stays in sight around it. */
struct kt_rect kt_modals_rect(const struct kt_modals *m, size_t index, struct kt_rect area) {
    const struct kt_modal *modal;
    struct kt_rect r;
    int width, height, spare;
    if (index >= m->count) return (struct kt_rect){area.x, area.y, 0, 0};
    modal = &m->stack[index];
    width = modal->width > 0 ? modal->width : area.width;
    if (width > area.width - 4) width = area.width - 4;
    if (width < 1) width = area.width;
    height = modal_height(modal, width);
    if (height > area.height - 2) height = area.height - 2;
    if (height < 1) height = area.height;
    r = kt_rect_center(area, width, height);
    spare = r.y - area.y;
    r.y -= spare / 3;
    return r;
}

static void place(struct kt_modals *m, size_t index) {
    const struct kt_modal *modal = &m->stack[index];
    if (modal->place) modal->place(modal->component, kt_modals_rect(m, index, m->area));
}

void kt_modals_place(struct kt_modals *m, struct kt_rect area) {
    size_t i;
    m->area = area;
    for (i = 0; i < m->count; i++) place(m, i);
}

bool kt_modals_push(struct kt_modals *m, const struct kt_modal *modal) {
    if (m->count == KT_MODALS_MAX || !modal->update || !modal->view) return false;
    m->stack[m->count++] = *modal;
    place(m, m->count - 1);
    return true;
}

void kt_modals_pop(struct kt_modals *m) {
    if (m->count) m->count--;
}

const struct kt_modal *kt_modals_top(const struct kt_modals *m) { return m->count ? &m->stack[m->count - 1] : NULL; }

static bool input(const struct kt_msg *msg) {
    return msg->type == KT_MSG_KEY || msg->type == KT_MSG_MOUSE || msg->type == KT_MSG_PASTE;
}

enum kt_modals_result kt_modals_update(struct kt_modals *m, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct kt_modal *top;
    int result;
    if (!m->count) return KT_MODALS_IGNORED;
    top = &m->stack[m->count - 1];
    /* A new size moves the modals; the application still hears of it. */
    result = top->update(top->component, msg, cmds);
    m->id = top->id;
    if (result > 1) { m->result = result; return KT_MODALS_RESULT; }
    if (result == 1) return KT_MODALS_HANDLED;
    if (msg->type == KT_MSG_KEY && kt_event_is(&msg->u.event, KT_KEY_ESCAPE, 0)) {
        kt_modals_pop(m);
        return KT_MODALS_CLOSED;
    }
    return input(msg) ? KT_MODALS_HANDLED : KT_MODALS_IGNORED;
}

void kt_modals_view(const struct kt_modals *m, struct kt_canvas *canvas, struct kt_rect area) {
    size_t i;
    if (!m->count) return;
    kt_restyle(canvas, area, m->backdrop);
    for (i = 0; i < m->count; i++) {
        const struct kt_modal *modal = &m->stack[i];
        modal->view(modal->component, canvas, kt_modals_rect(m, i, area));
    }
}

/* ---- the components as modals ------------------------------------------ */

static int confirm_update(void *c, const struct kt_msg *msg, struct kt_cmds *cmds) {
    return (int)kt_confirm_update(c, msg, cmds);
}
static void confirm_view(const void *c, struct kt_canvas *canvas, struct kt_rect area) { kt_confirm_view(c, canvas, area); }
static void confirm_place(void *c, struct kt_rect area) { kt_confirm_place(c, area); }
static int confirm_height(const void *c, int width) { return kt_confirm_height(c, width); }

struct kt_modal kt_confirm_modal(struct kt_confirm *confirm, uint32_t id, int width) {
    struct kt_modal modal = {0, NULL, confirm_update, confirm_view, confirm_place, confirm_height, 0, 0};
    modal.id = id;
    modal.component = confirm;
    modal.width = width > 0 ? width : 58;
    return modal;
}

static int form_update(void *f, const struct kt_msg *msg, struct kt_cmds *cmds) { return (int)kt_form_update(f, msg, cmds); }
static void form_view(const void *f, struct kt_canvas *canvas, struct kt_rect area) { kt_form_view(f, canvas, area); }
static void form_place(void *f, struct kt_rect area) { kt_form_place(f, area); }
static int form_height(const void *f, int width) { return kt_form_height(f, width); }

struct kt_modal kt_form_modal(struct kt_form *form, uint32_t id, int width) {
    struct kt_modal modal = {0, NULL, form_update, form_view, form_place, form_height, 0, 0};
    modal.id = id;
    modal.component = form;
    modal.width = width > 0 ? width : 58;
    return modal;
}

static int palette_update(void *p, const struct kt_msg *msg, struct kt_cmds *cmds) {
    return (int)kt_palette_update(p, msg, cmds);
}
static void palette_view(const void *p, struct kt_canvas *canvas, struct kt_rect area) { kt_palette_view(p, canvas, area); }
static void palette_place(void *p, struct kt_rect area) { kt_palette_place(p, area); }
static int palette_height(const void *p, int width) { return kt_palette_height(p, width); }

struct kt_modal kt_palette_modal(struct kt_palette *palette, uint32_t id, int width) {
    struct kt_modal modal = {0, NULL, palette_update, palette_view, palette_place, palette_height, 0, 0};
    modal.id = id;
    modal.component = palette;
    modal.width = width > 0 ? width : 64;
    return modal;
}

/* The help screen: its own keys close it; everything else is taken. */
static int help_update(void *h, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct kt_help *help = h;
    enum kt_help_result result;
    if (!help->full) help->full = true;
    result = kt_help_update(help, msg, cmds);
    if (result == KT_HELP_CLOSED) return (int)result;
    return result == KT_HELP_IGNORED && msg->type != KT_MSG_KEY ? 0 : 1;
}

static void help_view(const void *h, struct kt_canvas *canvas, struct kt_rect area) {
    const struct kt_help *help = h;
    struct kt_help_source hints;
    struct kt_binding keys[KT_HELP_ACTIONS];
    struct kt_help_style stock = kt_ui_hint_style();
    struct kt_rect inside;
    memcpy(keys, help->keys, sizeof(keys));
    keys[KT_HELP_TOGGLE].label = keys[KT_HELP_CLOSE].label = "close";
    hints.bindings = keys;
    hints.count = KT_HELP_ACTIONS;
    hints.scope = KT_SCOPE_ALL;
    inside = kt_ui_dialog(canvas, area, "Keyboard", KT_INK(KT_COLOR_BORDER, 0), KT_INK(KT_COLOR_TEXT, KT_BOLD), &hints,
                          1, &stock);
    (void)kt_help_full_view(help, canvas, inside);
}

static int help_height(const void *h, int width) { return kt_help_full_height(h, width - 6) + 4; }

struct kt_modal kt_help_modal(struct kt_help *help, uint32_t id, int width) {
    struct kt_modal modal = {0, NULL, help_update, help_view, NULL, help_height, 0, 0};
    modal.id = id;
    modal.component = help;
    modal.width = width > 0 ? width : kt_help_full_width(help, 2) + 6;
    return modal;
}
