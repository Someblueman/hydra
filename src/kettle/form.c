#include "component_internal.h"
#include <string.h>

/* Labelled fields in a column: a label, the field's row, and its hint or
 * error, with a blank row between fields. */

#define FIELDS KT_FORM_SCOPE_FIELDS
#define ADJUST (KT_FORM_SCOPE_CHOICE | KT_FORM_SCOPE_TOGGLE)

static const struct kt_binding form_keys[KT_FORM_ACTIONS] = {
    KT_UI_ROW(KT_FORM_NEXT, "form.next", "next field", FIELDS, NULL, 0, 2, KT_UI_CHORD(KT_KEY_TAB, 0),
              KT_UI_CHORD(KT_KEY_DOWN, 0)),
    KT_UI_ROW(KT_FORM_PREVIOUS, "form.previous", "previous field", FIELDS, NULL, -1, 2,
              KT_UI_CHORD(KT_KEY_TAB, KT_MOD_SHIFT), KT_UI_CHORD(KT_KEY_UP, 0)),
    KT_UI_ROW(KT_FORM_SUBMIT, "form.submit", "submit", FIELDS, NULL, 2, 1, KT_UI_CHORD(KT_KEY_ENTER, 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_FORM_CANCEL, "form.cancel", "cancel", FIELDS, NULL, 1, 1, KT_UI_CHORD(KT_KEY_ESCAPE, 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_FORM_LEFT, "form.left", "change", ADJUST, NULL, -1, 1, KT_UI_CHORD(KT_KEY_LEFT, 0), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_FORM_RIGHT, "form.right", "change", ADJUST, NULL, -1, 1, KT_UI_CHORD(KT_KEY_RIGHT, 0),
              KT_UI_NO_CHORD),
    KT_UI_ROW(KT_FORM_FLIP, "form.flip", "switch", KT_FORM_SCOPE_TOGGLE, NULL, 0, 1, KT_UI_CHORD(' ', 0),
              KT_UI_NO_CHORD),
};

static bool framed(const struct kt_form *f) { return f->title != NULL; }
static struct kt_rect inside_of(const struct kt_form *f, struct kt_rect area) {
    return framed(f) ? kt_rect_inset(area, 2, 3, 2, 3) : area;
}

static bool has_note(const struct kt_field *field) { return field->error[0] || (field->hint && field->hint[0]); }
static int field_rows(const struct kt_field *field) { return 2 + (has_note(field) ? 1 : 0); }

/* The rows of field `index` in the inside area: its label row first. */
static struct kt_rect field_rect(const struct kt_form *f, struct kt_rect inside, size_t index) {
    struct kt_rect r = inside;
    size_t i;
    for (i = 0; i < index && i < f->count; i++) r.y += field_rows(&f->fields[i]) + 1;
    r.height = index < f->count ? field_rows(&f->fields[index]) : 0;
    return r;
}

int kt_form_height(const struct kt_form *f, int width) {
    int rows = 0;
    size_t i;
    (void)width;
    for (i = 0; i < f->count; i++) rows += field_rows(&f->fields[i]) + (i ? 1 : 0);
    return rows + (framed(f) ? 4 : 0);
}

static const struct kt_field *focused(const struct kt_form *f) { return f->focus < f->count ? &f->fields[f->focus] : NULL; }

uint32_t kt_form_scope(const struct kt_form *f) {
    const struct kt_field *field = focused(f);
    uint32_t scope = FIELDS;
    if (field && field->kind == KT_FIELD_CHOICE) scope |= KT_FORM_SCOPE_CHOICE;
    if (field && field->kind == KT_FIELD_TOGGLE) scope |= KT_FORM_SCOPE_TOGGLE;
    return scope;
}

struct kt_keymap kt_form_keymap(struct kt_form *f) {
    struct kt_keymap map;
    map.bindings = f->keys;
    map.count = KT_FORM_ACTIONS;
    return map;
}

void kt_form_place(struct kt_form *f, struct kt_rect area) {
    struct kt_rect inside = inside_of(f, area);
    size_t i;
    f->area = area;
    for (i = 0; i < f->count; i++) {
        struct kt_rect r = field_rect(f, inside, i);
        if (f->fields[i].kind == KT_FIELD_TEXT && f->fields[i].input)
            kt_textinput_place(f->fields[i].input, (struct kt_rect){r.x, r.y + 1, r.width, 1});
    }
}

void kt_form_focus(struct kt_form *f, size_t index) {
    size_t i;
    if (index < f->count) f->focus = index;
    for (i = 0; i < f->count; i++)
        if (f->fields[i].kind == KT_FIELD_TEXT && f->fields[i].input) f->fields[i].input->focused = i == f->focus;
}

void kt_form_init(struct kt_form *f, const char *title, struct kt_field *fields, size_t count) {
    struct kt_form_style *s = &f->style;
    memset(f, 0, sizeof(*f));
    f->title = title;
    f->fields = fields;
    f->count = count;
    s->border = KT_INK(KT_COLOR_ACCENT, 0);
    s->title = KT_INK(KT_COLOR_TEXT, KT_BOLD);
    s->label = KT_INK(KT_COLOR_MUTED, 0);
    s->label_focused = KT_INK(KT_COLOR_TEXT, KT_BOLD);
    s->hint = KT_INK(KT_COLOR_MUTED, 0);
    s->error = KT_INK(KT_COLOR_DANGER, 0);
    s->option = KT_INK(KT_COLOR_MUTED, 0);
    s->option_selected = KT_INK(KT_COLOR_TEXT, KT_BOLD | KT_UNDERLINE);
    s->field = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_SUNKEN, 0);
    s->prompt = KT_INK(KT_COLOR_FAINT, 0);
    s->prompt_focused = KT_INK(KT_COLOR_ACCENT, 0);
    s->hints = kt_ui_hint_style();
    memcpy(f->keys, form_keys, sizeof(form_keys));
    kt_form_focus(f, 0);
}

/* ---- checking ----------------------------------------------------------- */

static void text_error(struct kt_field *field) {
    field->error[0] = '\0';
    if (field->kind != KT_FIELD_TEXT || !field->input || field->input->valid) return;
    snprintf(field->error, sizeof(field->error), "%s", field->input->error);
}

static size_t first_error(const struct kt_form *f) {
    size_t i;
    for (i = 0; i < f->count; i++)
        if (f->fields[i].error[0]) return i;
    return KT_NONE;
}

bool kt_form_check(struct kt_form *f) {
    bool valid = true;
    size_t i;
    for (i = 0; i < f->count; i++) {
        struct kt_field *field = &f->fields[i];
        if (field->kind == KT_FIELD_TEXT && field->input) (void)kt_textinput_validate(field->input);
        text_error(field);
    }
    if (f->validate) valid = f->validate(f->validate_context, f);
    return valid && first_error(f) == KT_NONE;
}

static enum kt_form_result submit(struct kt_form *f) {
    size_t bad;
    if (kt_form_check(f)) return KT_FORM_SUBMITTED;
    bad = first_error(f);
    if (bad != KT_NONE) kt_form_focus(f, bad);
    kt_form_place(f, f->area);
    return KT_FORM_INVALID;
}

/* ---- input -------------------------------------------------------------- */

static enum kt_form_result adjust(struct kt_form *f, int step) {
    struct kt_field *field = &f->fields[f->focus];
    if (field->kind == KT_FIELD_TOGGLE) { field->on = !field->on; return KT_FORM_CHANGED; }
    if (field->kind != KT_FIELD_CHOICE || !field->option_count) return KT_FORM_HANDLED;
    field->choice = (field->choice + field->option_count + (size_t)(step < 0 ? -1 : 1)) % field->option_count;
    return KT_FORM_CHANGED;
}

static enum kt_form_result move(struct kt_form *f, int step) {
    if (!f->count) return KT_FORM_HANDLED;
    kt_form_focus(f, (f->focus + f->count + (size_t)(step < 0 ? -1 : 1)) % f->count);
    return KT_FORM_HANDLED;
}

static enum kt_form_result perform(struct kt_form *f, int action) {
    switch (action) {
    case KT_FORM_NEXT: return move(f, 1);
    case KT_FORM_PREVIOUS: return move(f, -1);
    case KT_FORM_SUBMIT: return submit(f);
    case KT_FORM_CANCEL: return KT_FORM_CANCELLED;
    case KT_FORM_LEFT: return adjust(f, -1);
    case KT_FORM_RIGHT: case KT_FORM_FLIP: return adjust(f, 1);
    default: return KT_FORM_HANDLED;
    }
}

static enum kt_form_result text_input(struct kt_form *f, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct kt_field *field = f->focus < f->count ? &f->fields[f->focus] : NULL;
    enum kt_textinput_result r;
    bool grew;
    if (!field || field->kind != KT_FIELD_TEXT || !field->input) return KT_FORM_IGNORED;
    grew = !has_note(field);
    r = kt_textinput_update(field->input, msg, cmds);
    if (r == KT_TEXTINPUT_IGNORED) return KT_FORM_IGNORED;
    if (r != KT_TEXTINPUT_CHANGED) return KT_FORM_HANDLED;
    text_error(field);
    /* An error that appears or goes moves the fields below. */
    if (grew != !has_note(field)) kt_form_place(f, f->area);
    return KT_FORM_CHANGED;
}

/* The option of a choice row under column x, or KT_NONE. */
static size_t option_at(const struct kt_field *field, int x, int left) {
    size_t i;
    int at = left + 2;   /* after the well's bar */
    for (i = 0; i < field->option_count; i++) {
        int width = kt_text_width(field->options[i]);
        if (x >= at && x < at + width) return i;
        at += width + 2;
    }
    return KT_NONE;
}

static enum kt_form_result click_field(struct kt_form *f, size_t index, const struct kt_msg *msg, int line) {
    struct kt_field *field = &f->fields[index];
    struct kt_rect inside = inside_of(f, f->area);
    size_t option;
    kt_form_focus(f, index);
    if (line != 1) return KT_FORM_HANDLED;
    if (field->kind == KT_FIELD_TEXT) return text_input(f, msg, NULL);
    if (field->kind == KT_FIELD_TOGGLE) return adjust(f, 1);
    option = option_at(field, msg->u.event.x, inside.x);
    if (option == KT_NONE) return KT_FORM_HANDLED;
    field->choice = option;
    return KT_FORM_CHANGED;
}

static enum kt_form_result click(struct kt_form *f, const struct kt_msg *msg) {
    struct kt_rect inside = inside_of(f, f->area);
    size_t i;
    int x, y;
    if (!kt_ui_click(msg, f->area, &x, &y)) return KT_FORM_IGNORED;
    for (i = 0; i < f->count; i++) {
        struct kt_rect r = field_rect(f, inside, i);
        if (kt_ui_inside(r, msg->u.event.x, msg->u.event.y)) return click_field(f, i, msg, msg->u.event.y - r.y);
    }
    return KT_FORM_HANDLED;
}

enum kt_form_result kt_form_update(struct kt_form *f, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action;
    if (msg->type == KT_MSG_MOUSE) return click(f, msg);
    action = kt_ui_action(f->keys, KT_FORM_ACTIONS, &f->chord, kt_form_scope(f), msg, &consumed);
    if (action >= 0) return perform(f, action);
    if (consumed) return KT_FORM_HANDLED;
    return text_input(f, msg, cmds);
}

/* ---- view --------------------------------------------------------------- */

/* The field's row: a well with a bar, then what the field shows. */
static struct kt_rect well(const struct kt_form *f, struct kt_canvas *canvas, struct kt_rect row, bool focus) {
    int bar;
    kt_fill(canvas, row, f->style.field);
    bar = kt_text_aligned(canvas, row, "\xe2\x94\x83 ", KT_ALIGN_LEFT, focus ? f->style.prompt_focused : f->style.prompt);
    return kt_rect_inset(row, 0, 0, 0, bar);
}

static void draw_choice(const struct kt_form *f, struct kt_canvas *canvas, struct kt_rect row,
                        const struct kt_field *field, bool focus) {
    struct kt_rect room = well(f, canvas, row, focus);
    size_t i;
    for (i = 0; i < field->option_count && room.width > 0; i++) {
        struct kt_style style = i == field->choice ? f->style.option_selected : f->style.option;
        int used = kt_text_aligned(canvas, room, field->options[i], KT_ALIGN_LEFT, style);
        room = kt_rect_inset(room, 0, 0, 0, used + 2);
    }
}

static void draw_toggle(const struct kt_form *f, struct kt_canvas *canvas, struct kt_rect row,
                        const struct kt_field *field, bool focus) {
    struct kt_rect room = well(f, canvas, row, focus);
    const char *label = field->on ? "on" : "off";
    int used;
    if (field->options && field->option_count >= 2) label = field->options[field->on ? 1 : 0];
    used = kt_text_aligned(canvas, room, field->on ? "[x]" : "[ ]", KT_ALIGN_LEFT, f->style.option_selected);
    (void)kt_text_aligned(canvas, kt_rect_inset(room, 0, 0, 0, used + 1), label, KT_ALIGN_LEFT, f->style.option);
}

static void draw_field(const struct kt_form *f, struct kt_canvas *canvas, struct kt_rect r, size_t index) {
    const struct kt_field *field = &f->fields[index];
    bool focus = index == f->focus;
    struct kt_rect row = {r.x, r.y + 1, r.width, 1};
    (void)kt_text_aligned(canvas, (struct kt_rect){r.x, r.y, r.width, 1}, field->label, KT_ALIGN_LEFT,
                          focus ? f->style.label_focused : f->style.label);
    if (field->kind == KT_FIELD_TEXT && field->input) kt_textinput_view(field->input, canvas, row);
    else if (field->kind == KT_FIELD_CHOICE) draw_choice(f, canvas, row, field, focus);
    else if (field->kind == KT_FIELD_TOGGLE) draw_toggle(f, canvas, row, field, focus);
    if (field->error[0])
        (void)kt_text_aligned(canvas, (struct kt_rect){r.x, r.y + 2, r.width, 1}, field->error, KT_ALIGN_LEFT,
                              f->style.error);
    else if (has_note(field))
        (void)kt_text_aligned(canvas, (struct kt_rect){r.x, r.y + 2, r.width, 1}, field->hint, KT_ALIGN_LEFT,
                              f->style.hint);
}

void kt_form_view(const struct kt_form *f, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_rect inside = area;
    size_t i;
    if (framed(f)) {
        struct kt_help_source hints;
        hints.bindings = f->keys;
        hints.count = KT_FORM_ACTIONS;
        hints.scope = FIELDS;
        inside = kt_ui_dialog(canvas, area, f->title, f->style.border, f->style.title, &hints, 1, &f->style.hints);
    }
    for (i = 0; i < f->count; i++) {
        struct kt_rect r = field_rect(f, inside, i);
        if (r.y >= inside.y + inside.height) break;
        draw_field(f, canvas, r, i);
    }
}
