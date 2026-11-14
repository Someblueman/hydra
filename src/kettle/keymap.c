#include "keymap.h"
#include <stdio.h>
#include <string.h>

/* Named keys. The first row for a key gives its configuration spelling and,
 * when display is set, its hint text; later rows are accepted aliases. */
struct key_name { const char *name, *display; uint32_t key; };
static const struct key_name key_names[] = {
    {"tab", "Tab", KT_KEY_TAB}, {"enter", "Enter", KT_KEY_ENTER}, {"esc", "Esc", KT_KEY_ESCAPE},
    {"space", "Space", ' '}, {"backspace", "Backspace", KT_KEY_BACKSPACE},
    {"delete", "Del", KT_KEY_DELETE}, {"insert", "Ins", KT_KEY_INSERT},
    {"up", "Up", KT_KEY_UP}, {"down", "Down", KT_KEY_DOWN}, {"left", "Left", KT_KEY_LEFT},
    {"right", "Right", KT_KEY_RIGHT}, {"home", "Home", KT_KEY_HOME}, {"end", "End", KT_KEY_END},
    {"pageup", "PgUp", KT_KEY_PAGE_UP}, {"pagedown", "PgDn", KT_KEY_PAGE_DOWN},
    {"comma", NULL, ','}, {"hash", NULL, '#'}, {"plus", NULL, '+'}, {"minus", NULL, '-'},
    {"return", NULL, KT_KEY_ENTER}, {"escape", NULL, KT_KEY_ESCAPE}, {"del", NULL, KT_KEY_DELETE},
    {"ins", NULL, KT_KEY_INSERT}, {"pgup", NULL, KT_KEY_PAGE_UP}, {"pgdn", NULL, KT_KEY_PAGE_DOWN}};
#define KEY_NAMES (sizeof(key_names) / sizeof(key_names[0]))

struct modifier_name { const char *name, *display; unsigned mod; };
static const struct modifier_name modifier_names[] = {
    {"ctrl", "Ctrl", KT_MOD_CTRL}, {"alt", "Alt", KT_MOD_ALT}, {"shift", "Shift", KT_MOD_SHIFT},
    {"control", NULL, KT_MOD_CTRL}, {"meta", NULL, KT_MOD_ALT}};
#define MODIFIER_NAMES (sizeof(modifier_names) / sizeof(modifier_names[0]))

/* Bounded text output that reports the untruncated length. A piece that does
 * not fit ends the output, so truncation never splits a character. */
struct writer { char *out; size_t capacity, written, length; bool full; };

static void put(struct writer *w, const char *text) {
    size_t n = strlen(text);
    if (!w->full && w->written + n < w->capacity) { memcpy(w->out + w->written, text, n); w->written += n; }
    else w->full = true;
    w->length += n;
}

static size_t finish(struct writer *w) {
    if (w->capacity) w->out[w->written] = '\0';
    return w->length;
}

static bool blank(char c) { return c == ' ' || c == '\t'; }
static uint32_t lower(uint32_t c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static uint32_t upper(uint32_t c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
static bool letter(uint32_t c) { return lower(c) >= 'a' && lower(c) <= 'z'; }

/* Whole-word comparison that ignores ASCII case. */
static bool same_word(const char *text, size_t length, const char *word) {
    size_t i;
    if (strlen(word) != length) return false;
    for (i = 0; i < length; i++)
        if (lower((unsigned char)text[i]) != (unsigned char)word[i]) return false;
    return true;
}

static void trim(const char **text, size_t *length) {
    while (*length && (blank(**text) || **text == '\r')) { (*text)++; (*length)--; }
    while (*length && (blank((*text)[*length - 1]) || (*text)[*length - 1] == '\r')) (*length)--;
}

/* Consumes one "modifier+" or "modifier-" prefix that leaves a key behind. */
static bool take_modifier(const char **text, size_t *length, unsigned *mods) {
    size_t i;
    for (i = 0; i < MODIFIER_NAMES; i++) {
        size_t n = strlen(modifier_names[i].name);
        if (*length <= n + 1 || !same_word(*text, n, modifier_names[i].name)) continue;
        if ((*text)[n] != '+' && (*text)[n] != '-') continue;
        *mods |= modifier_names[i].mod;
        *text += n + 1; *length -= n + 1;
        return true;
    }
    return false;
}

/* "f1" to "f12", without leading zeros. */
static bool function_key(const char *text, size_t length, uint32_t *key) {
    unsigned number;
    if (length < 2 || length > 3 || lower((unsigned char)text[0]) != 'f') return false;
    if (text[1] < '1' || text[1] > '9') return false;
    number = (unsigned)(text[1] - '0');
    if (length == 3) {
        if (text[2] < '0' || text[2] > '9') return false;
        number = number * 10U + (unsigned)(text[2] - '0');
    }
    if (number > 12U) return false;
    *key = KT_KEY_F1 + number - 1U;
    return true;
}

/* One printable character, written as itself. */
static bool character_key(const char *text, size_t length, uint32_t *key) {
    uint32_t cp;
    if (!length || kt_utf8_decode(text, length, &cp) != length) return false;
    if (length == 1 && (unsigned char)text[0] >= 0x80) return false;
    if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) return false;
    *key = cp;
    return true;
}

static bool parse_key(const char *text, size_t length, uint32_t *key) {
    size_t i;
    if (character_key(text, length, key)) return true;
    for (i = 0; i < KEY_NAMES; i++)
        if (same_word(text, length, key_names[i].name)) { *key = key_names[i].key; return true; }
    return function_key(text, length, key);
}

/* Ctrl combinations a terminal sends as another key's byte. */
static void control_alias(struct kt_keystroke *k) {
    k->key = lower(k->key);
    switch (k->key) {
    case 'i': k->key = KT_KEY_TAB; break;
    case 'm': k->key = KT_KEY_ENTER; break;
    case '[': k->key = KT_KEY_ESCAPE; break;
    case '?': k->key = KT_KEY_BACKSPACE; break;
    case '@': k->key = ' '; return;
    default: return;
    }
    k->mods &= ~(unsigned)KT_MOD_CTRL;
}

/* Rewrites a keystroke as kt_input reports it; false when no terminal can. */
static bool normalize(struct kt_keystroke *k) {
    if ((k->mods & KT_MOD_SHIFT) && k->key < KT_KEY_UP) {
        if (!letter(k->key)) return false;
        k->key = upper(k->key);
        k->mods &= ~(unsigned)KT_MOD_SHIFT;
    }
    if (k->mods & KT_MOD_CTRL) control_alias(k);
    return true;
}

static bool parse_keystroke(const char *text, size_t length, struct kt_keystroke *out) {
    out->key = 0; out->mods = 0;
    while (take_modifier(&text, &length, &out->mods)) {}
    return parse_key(text, length, &out->key) && normalize(out);
}

/* The next blank-separated word at or after *at; false at the end. */
static bool next_word(const char *text, size_t length, size_t *at, const char **word, size_t *word_length) {
    size_t i = *at, start;
    while (i < length && blank(text[i])) i++;
    start = i;
    while (i < length && !blank(text[i])) i++;
    *at = i; *word = text + start; *word_length = i - start;
    return i > start;
}

bool kt_chord_parse(const char *text, size_t length, struct kt_chord *out) {
    const char *word;
    size_t at = 0, n;
    memset(out, 0, sizeof(*out));
    while (next_word(text, length, &at, &word, &n)) {
        if (out->count == KT_CHORD_MAX) return false;
        if (!parse_keystroke(word, n, &out->keys[out->count])) return false;
        out->count++;
    }
    return out->count > 0;
}

static const struct key_name *named(uint32_t key) {
    size_t i;
    for (i = 0; i < KEY_NAMES; i++) if (key_names[i].key == key) return &key_names[i];
    return NULL;
}

/* The word for a named key in this form, or NULL when it is written as its
 * character (or has no name). */
static const char *key_word(uint32_t key, bool display) {
    const struct key_name *name = named(key);
    if (!name) return NULL;
    return display ? name->display : name->name;
}

static void put_key(struct writer *w, const struct kt_keystroke *k, bool display) {
    const char *word = key_word(k->key, display);
    char text[16];
    uint32_t cp = k->key;
    if (word) { put(w, word); return; }
    /* Ctrl with a letter reads as it is spoken: Ctrl-B. */
    if (display && (k->mods & KT_MOD_CTRL)) cp = upper(cp);
    if (cp >= KT_KEY_F1 && cp <= KT_KEY_F12) snprintf(text, sizeof(text), "%c%u", display ? 'F' : 'f', (unsigned)(cp - KT_KEY_F1) + 1U);
    else if (cp < 0x20 || cp >= KT_KEY_UP) snprintf(text, sizeof(text), "?");
    else text[kt_utf8_encode(cp, text)] = '\0';
    put(w, text);
}

static void put_keystroke(struct writer *w, const struct kt_keystroke *k, bool display) {
    size_t i;
    for (i = 0; i < MODIFIER_NAMES; i++) {
        if (!modifier_names[i].display || !(k->mods & modifier_names[i].mod)) continue;
        put(w, display ? modifier_names[i].display : modifier_names[i].name);
        put(w, display ? "-" : "+");
    }
    put_key(w, k, display);
}

static void put_chord(struct writer *w, const struct kt_chord *chord, bool display) {
    unsigned i;
    for (i = 0; i < chord->count && i < KT_CHORD_MAX; i++) {
        if (i) put(w, " ");
        put_keystroke(w, &chord->keys[i], display);
    }
}

static size_t chord_text(const struct kt_chord *chord, char *out, size_t capacity, bool display) {
    struct writer w = {out, capacity, 0, 0, false};
    put_chord(&w, chord, display);
    return finish(&w);
}

/* The hint spelling of a named key: arrows as glyphs, page keys short, the
 * rest as configuration writes them. */
static const char *hint_word(uint32_t key) {
    switch (key) {
    case KT_KEY_UP: return "\xe2\x86\x91";
    case KT_KEY_DOWN: return "\xe2\x86\x93";
    case KT_KEY_LEFT: return "\xe2\x86\x90";
    case KT_KEY_RIGHT: return "\xe2\x86\x92";
    case KT_KEY_PAGE_UP: return "pgup";
    case KT_KEY_PAGE_DOWN: return "pgdn";
    case KT_KEY_DELETE: return "del";
    default: return key_word(key, false);
    }
}

static void put_hint(struct writer *w, const struct kt_keystroke *k) {
    const char *word = hint_word(k->key);
    size_t i;
    for (i = 0; i < MODIFIER_NAMES; i++) {
        if (!modifier_names[i].display || !(k->mods & modifier_names[i].mod)) continue;
        put(w, modifier_names[i].name);
        put(w, "+");
    }
    if (word) put(w, word);
    else put_key(w, k, false);
}

size_t kt_chord_hint(const struct kt_chord *chord, char *out, size_t capacity) {
    struct writer w = {out, capacity, 0, 0, false};
    unsigned i;
    for (i = 0; i < chord->count && i < KT_CHORD_MAX; i++) {
        if (i) put(&w, " ");
        put_hint(&w, &chord->keys[i]);
    }
    return finish(&w);
}

size_t kt_chord_format(const struct kt_chord *chord, char *out, size_t capacity) {
    return chord_text(chord, out, capacity, true);
}

size_t kt_chord_config(const struct kt_chord *chord, char *out, size_t capacity) {
    return chord_text(chord, out, capacity, false);
}

static bool starts_with(const struct kt_chord *chord, const struct kt_chord *prefix) {
    unsigned i;
    if (chord->count < prefix->count) return false;
    for (i = 0; i < prefix->count; i++)
        if (chord->keys[i].key != prefix->keys[i].key || chord->keys[i].mods != prefix->keys[i].mods) return false;
    return true;
}

bool kt_chord_equal(const struct kt_chord *a, const struct kt_chord *b) {
    return a->count == b->count && starts_with(a, b);
}

bool kt_chord_extends(const struct kt_chord *chord, const struct kt_chord *prefix) {
    return chord->count > prefix->count && starts_with(chord, prefix);
}

void kt_keymap_reset(struct kt_keymap_state *state) { memset(state, 0, sizeof(*state)); }

/* The first live binding whose chord is exactly attempt, and whether any live
 * chord continues beyond it. */
static const struct kt_binding *search(const struct kt_keymap *map, uint32_t scope,
                                       const struct kt_chord *attempt, bool *longer) {
    const struct kt_binding *exact = NULL;
    size_t i;
    unsigned c;
    for (i = 0; i < map->count; i++) {
        const struct kt_binding *b = &map->bindings[i];
        if (!(b->scope & scope)) continue;
        for (c = 0; c < b->chord_count && c < KT_BINDING_CHORDS; c++) {
            if (kt_chord_extends(&b->chords[c], attempt)) *longer = true;
            else if (!exact && kt_chord_equal(&b->chords[c], attempt)) exact = b;
        }
    }
    return exact;
}

enum kt_key_match kt_keymap_feed(const struct kt_keymap *map, struct kt_keymap_state *state, uint32_t scope,
                                 const struct kt_event *event, const struct kt_binding **matched) {
    struct kt_chord attempt;
    const struct kt_binding *exact;
    bool longer = false;
    if (matched) *matched = NULL;
    if (event->type != KT_KEY) return KT_KEY_UNBOUND;
    if (state->pending.count >= KT_CHORD_MAX) state->pending.count = 0;
    attempt = state->pending;
    attempt.keys[attempt.count].key = event->key;
    attempt.keys[attempt.count].mods = event->mods;
    attempt.count++;
    exact = search(map, scope, &attempt, &longer);
    if (exact) {
        state->pending.count = 0;
        if (matched) *matched = exact;
        return KT_KEY_ACTION;
    }
    if (longer) { state->pending = attempt; return KT_KEY_PENDING; }
    if (!state->pending.count) return KT_KEY_UNBOUND;
    state->abandoned = attempt;
    state->pending.count = 0;
    return KT_KEY_ABANDONED;
}

static struct kt_binding *find(const struct kt_keymap *map, const char *name, size_t length) {
    size_t i;
    for (i = 0; i < map->count; i++)
        if (strlen(map->bindings[i].name) == length && !memcmp(map->bindings[i].name, name, length))
            return &map->bindings[i];
    return NULL;
}

const struct kt_binding *kt_keymap_find(const struct kt_keymap *map, const char *name) {
    return find(map, name, strlen(name));
}

/* Collects refused lines: the first is described, the rest are counted. */
struct refusals { char *out; size_t capacity; unsigned count; };

static bool refuse(struct refusals *r, unsigned line, const char *reason, const char *subject, size_t length) {
    if (!r->count++ && r->capacity) {
        if (subject) snprintf(r->out, r->capacity, "line %u: %s '%.*s'", line, reason, length > 48 ? 48 : (int)length, subject);
        else snprintf(r->out, r->capacity, "line %u: %s", line, reason);
    }
    return false;
}

static void refusals_finish(struct refusals *r) {
    size_t used;
    if (r->count < 2 || !r->capacity) return;
    used = strlen(r->out);
    snprintf(r->out + used, r->capacity - used, " (and %u more)", r->count - 1U);
}

/* Reads "chord[, chord]" or "none" into chords; false after reporting why. */
static bool parse_chords(const char *value, size_t length, struct kt_chord *chords, unsigned *count,
                         struct refusals *r, unsigned line) {
    *count = 0;
    if (same_word(value, length, "none")) return true;
    for (;;) {
        const char *piece = value;
        size_t n = 0;
        while (n < length && value[n] != ',') n++;
        value += n; length -= n;
        trim(&piece, &n);
        if (*count == KT_BINDING_CHORDS) return refuse(r, line, "too many chords for one action", NULL, 0);
        if (!kt_chord_parse(piece, n, &chords[*count])) return refuse(r, line, "cannot read chord", piece, n);
        (*count)++;
        if (!length) return true;
        value++; length--;
    }
}

/* Applies one line that is neither blank nor a comment. */
static bool configure_line(struct kt_keymap *map, const char *text, size_t length, struct refusals *r, unsigned line) {
    const char *value = memchr(text, '=', length);
    struct kt_chord chords[KT_BINDING_CHORDS];
    struct kt_binding *binding;
    size_t name_length, value_length;
    unsigned count, i;
    if (!value) return refuse(r, line, "expected 'action = chord'", NULL, 0);
    name_length = (size_t)(value - text);
    value_length = length - name_length - 1;
    value++;
    trim(&text, &name_length);
    trim(&value, &value_length);
    binding = find(map, text, name_length);
    if (!binding) return refuse(r, line, "unknown action", text, name_length);
    if (!value_length) return refuse(r, line, "no chord given; write none to unbind", NULL, 0);
    if (!parse_chords(value, value_length, chords, &count, r, line)) return false;
    for (i = 0; i < count; i++) binding->chords[i] = chords[i];
    binding->chord_count = count;
    binding->line = line;
    return true;
}

bool kt_keymap_configure(struct kt_keymap *map, const char *text, size_t length, char *error, size_t error_capacity) {
    struct refusals r = {error, error_capacity, 0};
    unsigned line = 0;
    size_t at = 0;
    if (error_capacity) error[0] = '\0';
    while (at < length) {
        const char *start = text + at, *comment;
        size_t n = 0;
        while (at + n < length && start[n] != '\n') n++;
        at += n + 1;
        line++;
        comment = memchr(start, '#', n);
        if (comment) n = (size_t)(comment - start);
        trim(&start, &n);
        if (n) (void)configure_line(map, start, n, &r, line);
    }
    refusals_finish(&r);
    return !r.count;
}

/* "name" or, when configuration set the binding, "name (line N)". */
static void put_owner(struct writer *w, const struct kt_binding *b) {
    char line[32];
    put(w, b->name);
    if (!b->line) return;
    snprintf(line, sizeof(line), " (line %u)", b->line);
    put(w, line);
}

/* Reports first as the beginning of the longer second; false when it is not. */
static bool prefix_conflict(const struct kt_binding *a, const struct kt_chord *first,
                            const struct kt_binding *b, const struct kt_chord *second, struct writer *w) {
    if (!kt_chord_extends(second, first)) return false;
    put_owner(w, a); put(w, " uses "); put_chord(w, first, true);
    put(w, ", which begins "); put_chord(w, second, true);
    put(w, " of "); put_owner(w, b);
    return true;
}

/* Describes how two live chords collide; false when they do not. */
static bool conflict(const struct kt_binding *a, const struct kt_chord *first,
                     const struct kt_binding *b, const struct kt_chord *second, struct writer *w) {
    if (!kt_chord_equal(first, second))
        return prefix_conflict(a, first, b, second, w) || prefix_conflict(b, second, a, first, w);
    put_owner(w, a);
    if (a == b) { put(w, " lists "); put_chord(w, first, true); put(w, " twice"); return true; }
    put(w, " and "); put_owner(w, b); put(w, " both use ");
    put_chord(w, first, true);
    return true;
}

/* Chord slot index of the table: its chord, or NULL when the slot is empty. */
static const struct kt_chord *slot(const struct kt_keymap *map, size_t index, const struct kt_binding **owner) {
    const struct kt_binding *b = &map->bindings[index / KT_BINDING_CHORDS];
    *owner = b;
    return index % KT_BINDING_CHORDS < b->chord_count ? &b->chords[index % KT_BINDING_CHORDS] : NULL;
}

bool kt_keymap_check(const struct kt_keymap *map, char *error, size_t error_capacity) {
    struct writer w = {error, error_capacity, 0, 0, false};
    const struct kt_binding *a, *b;
    size_t slots = map->count * KT_BINDING_CHORDS, p, q;
    if (error_capacity) error[0] = '\0';
    for (p = 0; p < slots; p++) {
        const struct kt_chord *first = slot(map, p, &a);
        for (q = p + 1; first && q < slots; q++) {
            const struct kt_chord *second = slot(map, q, &b);
            if (!second || !(a->scope & b->scope) || !conflict(a, first, b, second, &w)) continue;
            (void)finish(&w);
            return false;
        }
    }
    return true;
}

size_t kt_binding_config(const struct kt_binding *binding, char *out, size_t capacity) {
    struct writer w = {out, capacity, 0, 0, false};
    unsigned i;
    put(&w, binding->name);
    put(&w, " = ");
    if (!binding->chord_count) put(&w, "none");
    for (i = 0; i < binding->chord_count && i < KT_BINDING_CHORDS; i++) {
        if (i) put(&w, ", ");
        put_chord(&w, &binding->chords[i], false);
    }
    return finish(&w);
}
