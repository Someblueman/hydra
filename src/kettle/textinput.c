#include "component_internal.h"
#include <string.h>

/* One line of editable text in caller storage. Positions are byte offsets
 * on scalar boundaries; widths are display columns. */

enum { SCOPE_EDIT = 1, SCOPE_SUGGEST = 2 };
#define EDITING "Editing"

static const struct kt_binding textinput_keys[KT_TEXTINPUT_ACTIONS] = {
    KT_UI_ROW(KT_TEXTINPUT_LEFT, "input.left", "move", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_LEFT, 0), KT_UI_CHORD('b', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_RIGHT, "input.right", "move", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_RIGHT, 0), KT_UI_CHORD('f', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_WORD_LEFT, "input.word_left", "word", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD('b', KT_MOD_ALT), KT_UI_CHORD(KT_KEY_LEFT, KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_WORD_RIGHT, "input.word_right", "word", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD('f', KT_MOD_ALT), KT_UI_CHORD(KT_KEY_RIGHT, KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_HOME, "input.home", "start", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_HOME, 0), KT_UI_CHORD('a', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_END, "input.end", "end", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_END, 0), KT_UI_CHORD('e', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_BACKSPACE, "input.backspace", "delete back", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_BACKSPACE, 0), KT_UI_CHORD('h', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_DELETE, "input.delete", "delete", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_DELETE, 0), KT_UI_CHORD('d', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_DELETE_WORD, "input.delete_word", "delete word", SCOPE_EDIT, EDITING, -1, 2,
              KT_UI_CHORD(KT_KEY_BACKSPACE, KT_MOD_ALT), KT_UI_CHORD('w', KT_MOD_CTRL)),
    KT_UI_ROW(KT_TEXTINPUT_DELETE_START, "input.delete_start", "delete to start", SCOPE_EDIT, EDITING, -1, 1,
              KT_UI_CHORD('u', KT_MOD_CTRL), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TEXTINPUT_DELETE_END, "input.delete_end", "delete to end", SCOPE_EDIT, EDITING, -1, 1,
              KT_UI_CHORD('k', KT_MOD_CTRL), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TEXTINPUT_ACCEPT, "input.accept", "complete", SCOPE_SUGGEST, EDITING, 1, 1,
              KT_UI_CHORD(KT_KEY_TAB, 0), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TEXTINPUT_NEXT_SUGGESTION, "input.next_suggestion", "suggestion", SCOPE_SUGGEST, EDITING, -1, 1,
              KT_UI_CHORD('n', KT_MOD_CTRL), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TEXTINPUT_PREVIOUS_SUGGESTION, "input.previous_suggestion", "suggestion", SCOPE_SUGGEST, EDITING,
              -1, 1, KT_UI_CHORD('p', KT_MOD_CTRL), KT_UI_NO_CHORD),
    KT_UI_ROW(KT_TEXTINPUT_SUBMIT, "input.submit", "submit", SCOPE_EDIT, EDITING, 1, 1,
              KT_UI_CHORD(KT_KEY_ENTER, 0), KT_UI_NO_CHORD),
};

static char *text_of(struct kt_textinput *t) { return t->buffer ? t->buffer : t->local; }
static size_t capacity_of(const struct kt_textinput *t) { return t->buffer ? t->capacity : sizeof(t->local); }
const char *kt_textinput_value(const struct kt_textinput *t) { return t->buffer ? t->buffer : t->local; }

static size_t scalars(const char *text, size_t length) {
    size_t i, n = 0;
    for (i = 0; i < length; i++) n += ((unsigned char)text[i] & 0xC0U) != 0x80U;
    return n;
}

static size_t previous(const char *text, size_t at) {
    if (!at) return 0;
    at--;
    while (at > 0 && ((unsigned char)text[at] & 0xC0U) == 0x80U) at--;
    return at;
}

static size_t following(const char *text, size_t length, size_t at) {
    uint32_t cp;
    size_t used;
    if (at >= length) return length;
    used = kt_utf8_decode(text + at, length - at, &cp);
    return at + (used ? used : 1);
}

static int columns(const char *text, size_t from, size_t to) {
    int width = 0;
    if (to > from) (void)kt_text_fit(text + from, to - from, 1 << 20, &width);
    return width;
}

static bool blank_at(const char *text, size_t at) { return text[at] == ' '; }

/* ---- validation, suggestions, scrolling --------------------------------- */

static void validate(struct kt_textinput *t) {
    t->valid = true;
    t->error[0] = '\0';
    if (t->validate) t->valid = t->validate(t->validate_context, kt_textinput_value(t), t->error, sizeof(t->error));
}

bool kt_textinput_validate(struct kt_textinput *t) {
    validate(t);
    return t->valid;
}

static bool extends(const char *suggestion, const char *text, size_t length) {
    size_t i;
    if (!suggestion || strlen(suggestion) <= length) return false;
    for (i = 0; i < length; i++) {
        unsigned char a = (unsigned char)suggestion[i], b = (unsigned char)text[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
        if (a != b) return false;
    }
    return true;
}

static size_t offered_count(const struct kt_textinput *t) {
    size_t i, n = 0;
    for (i = 0; i < t->suggestion_count; i++) n += extends(t->suggestions[i], kt_textinput_value(t), t->length);
    return n;
}

const char *kt_textinput_suggestion(const struct kt_textinput *t) {
    size_t i, n = offered_count(t), want;
    if (!n || !t->length) return NULL;
    want = t->suggestion % n;
    for (i = 0; i < t->suggestion_count; i++) {
        if (!extends(t->suggestions[i], kt_textinput_value(t), t->length)) continue;
        if (!want--) return t->suggestions[i];
    }
    return NULL;
}

static int prompt_width(const struct kt_textinput *t) { return t->prompt ? kt_text_width(t->prompt) : 0; }

/* Columns for text: the area less the prompt and one cell for the cursor. */
static int text_room(const struct kt_textinput *t) { return t->area.width - prompt_width(t) - 1; }

static void keep_cursor_visible(struct kt_textinput *t) {
    const char *text = kt_textinput_value(t);
    int room = text_room(t);
    if (t->offset > t->cursor) t->offset = t->cursor;
    if (room < 1) return;
    while (t->offset < t->cursor && columns(text, t->offset, t->cursor) > room)
        t->offset = following(text, t->length, t->offset);
}

static enum kt_textinput_result changed(struct kt_textinput *t) {
    t->suggestion = 0;
    validate(t);
    keep_cursor_visible(t);
    return KT_TEXTINPUT_CHANGED;
}

static enum kt_textinput_result moved(struct kt_textinput *t, size_t cursor) {
    t->cursor = cursor;
    keep_cursor_visible(t);
    return KT_TEXTINPUT_HANDLED;
}

/* ---- editing ------------------------------------------------------------ */

static bool insert_scalar(struct kt_textinput *t, uint32_t cp) {
    char bytes[4], *text = text_of(t);
    size_t n = kt_utf8_encode(cp, bytes);
    if (t->length + n + 1 > capacity_of(t)) return false;
    if (t->limit && scalars(text, t->length) >= t->limit) return false;
    memmove(text + t->cursor + n, text + t->cursor, t->length - t->cursor + 1);
    memcpy(text + t->cursor, bytes, n);
    t->length += n;
    t->cursor += n;
    return true;
}

/* Typed or pasted text, one scalar at a time: breaks and tabs become
 * spaces, other controls are left out, invalid bytes become '?'. */
bool kt_textinput_insert(struct kt_textinput *t, const char *bytes, size_t length) {
    size_t at = 0;
    bool any = false;
    while (at < length) {
        uint32_t cp;
        size_t used = kt_utf8_decode(bytes + at, length - at, &cp);
        if (!used) { used = 1; cp = '?'; }
        at += used;
        if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
        if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) continue;
        if (!insert_scalar(t, cp)) break;
        any = true;
    }
    return any;
}

static void erase(struct kt_textinput *t, size_t from, size_t to) {
    char *text = text_of(t);
    if (to <= from) return;
    memmove(text + from, text + to, t->length - to + 1);
    t->length -= to - from;
    t->cursor = from;
    if (t->offset > from) t->offset = from;
}

static size_t word_left(const char *text, size_t at) {
    while (at > 0 && blank_at(text, previous(text, at))) at = previous(text, at);
    while (at > 0 && !blank_at(text, previous(text, at))) at = previous(text, at);
    return at;
}

static size_t word_right(const char *text, size_t length, size_t at) {
    while (at < length && blank_at(text, at)) at = following(text, length, at);
    while (at < length && !blank_at(text, at)) at = following(text, length, at);
    return at;
}

void kt_textinput_set(struct kt_textinput *t, const char *value) {
    text_of(t)[0] = '\0';
    t->length = t->cursor = t->offset = 0;
    if (value) (void)kt_textinput_insert(t, value, strlen(value));
    (void)changed(t);
}

static enum kt_textinput_result accept(struct kt_textinput *t) {
    const char *offer = kt_textinput_suggestion(t);
    char copy[512];
    if (!offer) return KT_TEXTINPUT_HANDLED;
    snprintf(copy, sizeof(copy), "%s", offer);
    kt_textinput_set(t, copy);
    return KT_TEXTINPUT_CHANGED;
}

static enum kt_textinput_result step_suggestion(struct kt_textinput *t, bool forward) {
    size_t n = offered_count(t);
    if (n) t->suggestion = forward ? (t->suggestion + 1) % n : (t->suggestion + n - 1) % n;
    return KT_TEXTINPUT_HANDLED;
}

static enum kt_textinput_result submit(struct kt_textinput *t) {
    validate(t);
    return t->valid ? KT_TEXTINPUT_SUBMITTED : KT_TEXTINPUT_INVALID;
}

static enum kt_textinput_result delete_range(struct kt_textinput *t, size_t from, size_t to) {
    if (to <= from) return KT_TEXTINPUT_HANDLED;
    erase(t, from, to);
    return changed(t);
}

static enum kt_textinput_result move_right(struct kt_textinput *t) {
    if (t->cursor == t->length && kt_textinput_suggestion(t)) return accept(t);
    return moved(t, following(kt_textinput_value(t), t->length, t->cursor));
}

static enum kt_textinput_result perform(struct kt_textinput *t, int action) {
    const char *text = kt_textinput_value(t);
    switch (action) {
    case KT_TEXTINPUT_LEFT: return moved(t, previous(text, t->cursor));
    case KT_TEXTINPUT_RIGHT: return move_right(t);
    case KT_TEXTINPUT_WORD_LEFT: return moved(t, word_left(text, t->cursor));
    case KT_TEXTINPUT_WORD_RIGHT: return moved(t, word_right(text, t->length, t->cursor));
    case KT_TEXTINPUT_HOME: return moved(t, 0);
    case KT_TEXTINPUT_END: return moved(t, t->length);
    case KT_TEXTINPUT_BACKSPACE: return delete_range(t, previous(text, t->cursor), t->cursor);
    case KT_TEXTINPUT_DELETE: return delete_range(t, t->cursor, following(text, t->length, t->cursor));
    case KT_TEXTINPUT_DELETE_WORD: return delete_range(t, word_left(text, t->cursor), t->cursor);
    case KT_TEXTINPUT_DELETE_START: return delete_range(t, 0, t->cursor);
    case KT_TEXTINPUT_DELETE_END: return delete_range(t, t->cursor, t->length);
    case KT_TEXTINPUT_ACCEPT: return accept(t);
    case KT_TEXTINPUT_NEXT_SUGGESTION: return step_suggestion(t, true);
    case KT_TEXTINPUT_PREVIOUS_SUGGESTION: return step_suggestion(t, false);
    case KT_TEXTINPUT_SUBMIT: return submit(t);
    default: return KT_TEXTINPUT_HANDLED;
    }
}

/* A click puts the cursor on the character under it. */
static enum kt_textinput_result click(struct kt_textinput *t, int x) {
    const char *text = kt_textinput_value(t);
    size_t at = t->offset;
    int column = prompt_width(t);
    while (at < t->length) {
        size_t next = following(text, t->length, at);
        int width = columns(text, at, next);
        if (x < column + width) break;
        column += width;
        at = next;
    }
    return moved(t, at);
}

enum kt_textinput_result kt_textinput_update(struct kt_textinput *t, const struct kt_msg *msg, struct kt_cmds *cmds) {
    bool consumed;
    int action, x, y;
    (void)cmds;
    if (!t->focused) return KT_TEXTINPUT_IGNORED;
    if (msg->type == KT_MSG_PASTE)
        return kt_textinput_insert(t, msg->u.paste.bytes, msg->u.paste.length) ? changed(t) : KT_TEXTINPUT_HANDLED;
    if (kt_ui_click(msg, t->area, &x, &y)) return click(t, x);
    action = kt_ui_action(t->keys, KT_TEXTINPUT_ACTIONS, &t->chord, kt_textinput_scope(t), msg, &consumed);
    if (action >= 0) return perform(t, action);
    if (consumed) return KT_TEXTINPUT_HANDLED;
    if (msg->type == KT_MSG_KEY && kt_event_text(&msg->u.event))
        return insert_scalar(t, msg->u.event.key) ? changed(t) : KT_TEXTINPUT_HANDLED;
    return KT_TEXTINPUT_IGNORED;
}

uint32_t kt_textinput_scope(const struct kt_textinput *t) {
    return SCOPE_EDIT | (t->cursor == t->length && kt_textinput_suggestion(t) ? SCOPE_SUGGEST : 0);
}

struct kt_keymap kt_textinput_keymap(struct kt_textinput *t) {
    struct kt_keymap map;
    map.bindings = t->keys;
    map.count = KT_TEXTINPUT_ACTIONS;
    return map;
}

void kt_textinput_init(struct kt_textinput *t, char *buffer, size_t capacity) {
    memset(t, 0, sizeof(*t));
    if (buffer && capacity) { t->buffer = buffer; t->capacity = capacity; }
    t->prompt = "\xe2\x94\x83 ";
    t->style.text = KT_INK(KT_COLOR_TEXT, 0);
    t->style.prompt = KT_INK(KT_COLOR_FAINT, 0);
    t->style.prompt_focused = KT_INK(KT_COLOR_ACCENT, 0);
    t->style.placeholder = KT_INK(KT_COLOR_FAINT, 0);
    t->style.cursor = KT_STYLE(KT_KEEP, KT_KEEP, KT_REVERSE);
    t->style.suggestion = KT_INK(KT_COLOR_FAINT, 0);
    t->style.field = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_SUNKEN, 0);
    memcpy(t->keys, textinput_keys, sizeof(textinput_keys));
    if (!memchr(text_of(t), '\0', capacity_of(t))) text_of(t)[0] = '\0';
    t->length = t->cursor = strlen(text_of(t));
    t->valid = true;
}

void kt_textinput_place(struct kt_textinput *t, struct kt_rect area) {
    t->area = area;
    keep_cursor_visible(t);
}

bool kt_textinput_cursor(const struct kt_textinput *t, int *x, int *y) {
    if (!t->focused) return false;
    *x = t->area.x + prompt_width(t) + columns(kt_textinput_value(t), t->offset, t->cursor);
    *y = t->area.y;
    return true;
}

/* ---- view --------------------------------------------------------------- */

/* Up to `room` columns of the text from `from`, as a terminated copy. */
static int visible(const char *text, size_t length, size_t from, int room, char *out, size_t capacity) {
    int width = 0;
    size_t bytes = room > 0 ? kt_text_fit(text + from, length - from, room, &width) : 0;
    if (bytes >= capacity) bytes = capacity - 1;
    memcpy(out, text + from, bytes);
    out[bytes] = '\0';
    return width;
}

static void draw_suggestion(const struct kt_textinput *t, struct kt_canvas *canvas, struct kt_rect area, int x) {
    const char *offer = kt_textinput_suggestion(t);
    if (!offer || t->cursor != t->length) return;
    (void)kt_text_aligned(canvas, (struct kt_rect){x, area.y, area.x + area.width - x, 1}, offer + t->length,
                          KT_ALIGN_LEFT, t->style.suggestion);
}

void kt_textinput_view(const struct kt_textinput *t, struct kt_canvas *canvas, struct kt_rect area) {
    const char *text = kt_textinput_value(t);
    char shown[1024];
    int prompt = prompt_width(t), x = area.x + prompt, cursor;
    struct kt_rect row = {area.x, area.y, area.width, 1};
    if (area.width < 1 || area.height < 1) return;
    kt_fill(canvas, row, t->style.field);
    if (t->prompt) (void)kt_text_aligned(canvas, row, t->prompt, KT_ALIGN_LEFT,
                                         t->focused ? t->style.prompt_focused : t->style.prompt);
    row = (struct kt_rect){x, area.y, area.width - prompt, 1};
    if (!t->length && t->placeholder) (void)kt_text_aligned(canvas, row, t->placeholder, KT_ALIGN_LEFT, t->style.placeholder);
    (void)visible(text, t->length, t->offset, area.width - prompt, shown, sizeof(shown));
    (void)kt_text_aligned(canvas, row, shown, KT_ALIGN_LEFT, t->style.text);
    cursor = x + columns(text, t->offset, t->cursor);
    draw_suggestion(t, canvas, area, cursor);
    if (t->focused && cursor < area.x + area.width)
        kt_restyle(canvas, (struct kt_rect){cursor, area.y, 1, 1}, t->style.cursor);
}
