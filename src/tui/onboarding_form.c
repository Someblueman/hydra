#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "onboarding_flow.h"
/* Text fields of the selection and review forms: insertion, deletion and
 * cursor movement; the controller decides what a submitted form runs. */

struct onb_field *onb_form_fields(struct native_onboarding *s) {
    return s->screen == ONB_SCREEN_REVIEW ? s->review_fields : s->select_fields;
}

void onb_form_set(struct onb_field *f, const char *text) {
    copy_text(f->text, sizeof(f->text), text);
    f->cursor = strlen(f->text);
}

void onb_form_insert(struct native_onboarding *s, const struct tv_event *e) {
    struct onb_field *f = &onb_form_fields(s)[s->focus];
    size_t length = strlen(f->text), i;
    if (f->cursor > length) f->cursor = length;
    for (i = 0; i < e->length && length + 1 < sizeof(f->text); i++) {
        unsigned char c = (unsigned char)e->bytes[i];
        if (c < 32 || c == 127) continue;
        memmove(f->text + f->cursor + 1, f->text + f->cursor, length - f->cursor + 1);
        f->text[f->cursor++] = (char)c; length++;
    }
    s->problem[0] = '\0';
}

static bool cursor_key(uint32_t key, size_t *cursor, size_t length) {
    if (key == TV_KEY_LEFT) { if (*cursor) (*cursor)--; }
    else if (key == TV_KEY_RIGHT) { if (*cursor < length) (*cursor)++; }
    else if (key == TV_KEY_HOME) *cursor = 0;
    else if (key == TV_KEY_END) *cursor = length;
    else return false;
    return true;
}

/* Editing keys of the focused field; false for keys the screen handles. */
bool onb_form_key(struct native_onboarding *s, const struct tv_event *e) {
    struct onb_field *f = &onb_form_fields(s)[s->focus];
    size_t length = strlen(f->text);
    if (f->cursor > length) f->cursor = length;
    if (e->key == 127 || e->key == 8) {
        if (f->cursor) { memmove(f->text + f->cursor - 1, f->text + f->cursor, length - f->cursor + 1); f->cursor--; }
    } else if (!cursor_key(e->key, &f->cursor, length)) {
        if (e->key < 32 || e->key >= TV_KEY_UP) return false;
        onb_form_insert(s, e);
        return true;
    }
    s->problem[0] = '\0';
    return true;
}
