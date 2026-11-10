#include "input.h"
#include <string.h>

void kt_input_init(struct kt_input *in) { memset(in, 0, sizeof(*in)); }

static bool final_byte(unsigned char byte) { return byte >= 0x40 && byte <= 0x7e; }

/* Emits the buffered bytes as one event and empties the buffer. */
static bool event(struct kt_input *in, struct kt_event *out, uint32_t key, unsigned mods) {
    memset(out, 0, sizeof(*out));
    out->type = in->paste ? KT_PASTE : KT_KEY;
    out->key = key; out->mods = mods; out->length = in->length;
    memcpy(out->bytes, in->bytes, in->length);
    in->length = 0;
    return true;
}

static bool mouse(struct kt_event *out) {
    const unsigned char *p = (const unsigned char *)out->bytes + 3;
    unsigned values[3] = {0,0,0};
    size_t i;
    for (i = 0; i < 3; i++) {
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            values[i] = values[i] * 10U + (*p++ - '0');
            if (values[i] > 65535) return false;
        }
        if (i < 2 && *p++ != ';') return false;
    }
    if ((*p != 'm' && *p != 'M') || p[1] || !values[1] || !values[2]) return false;
    out->type = KT_MOUSE; out->x = (int)values[1] - 1; out->y = (int)values[2] - 1;
    out->button = values[0]; out->release = *p == 'm';
    return true;
}

/* One byte below 0x80 as a key. A C0 control is its letter with Ctrl, except
 * the four bytes that keys of their own send. */
static uint32_t byte_key(unsigned char byte, unsigned *mods) {
    switch (byte) {
    case 0x09: return KT_KEY_TAB;
    case 0x0d: return KT_KEY_ENTER;
    case 0x1b: return KT_KEY_ESCAPE;
    case 0x7f: return KT_KEY_BACKSPACE;
    default: break;
    }
    if (byte >= 0x20) return byte;
    *mods |= KT_MOD_CTRL;
    if (!byte) return ' ';
    return byte < 0x1b ? (uint32_t)byte + 'a' - 1U : (uint32_t)byte + '@';
}

/* A key that is not an escape sequence, starting at offset (1 after Escape).
 * False while a UTF-8 scalar is incomplete. */
static bool text_key(struct kt_input *in, struct kt_event *out, size_t offset, unsigned mods) {
    uint32_t cp;
    if ((unsigned char)in->bytes[offset] < 0x80) cp = byte_key((unsigned char)in->bytes[offset], &mods);
    else if (!kt_utf8_decode(in->bytes + offset, in->length - offset, &cp)) return false;
    return event(in, out, cp, mods);
}

struct sequence { unsigned values[2]; size_t count; unsigned char final; bool ss3; };

/* Reads one decimal field; false when it is absent or too large. */
static bool field(const char **cursor, const char *end, unsigned *value) {
    const char *p = *cursor;
    *value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        *value = *value * 10U + (unsigned)(*p++ - '0');
        if (*value > 9999U) return false;
    }
    if (p == *cursor) return false;
    *cursor = p;
    return true;
}

/* Splits "ESC [ n ; m final" or "ESC O n final" into at most two numbers.
 * False for anything else: private markers, empty or extra fields. */
static bool sequence_fields(const struct kt_event *e, struct sequence *s) {
    const char *p = e->bytes + 2, *end = e->bytes + e->length - 1;
    memset(s, 0, sizeof(*s));
    s->ss3 = e->bytes[1] == 'O';
    s->final = (unsigned char)*end;
    while (p < end) {
        if (s->count == 2 || !field(&p, end, &s->values[s->count])) return false;
        s->count++;
        if (p == end) break;
        if (*p++ != ';' || p == end) return false;
    }
    return true;
}

/* xterm's modifier field is 1 plus a bit set: Shift 1, Alt 2, Ctrl 4, Meta 8. */
static bool modifier(unsigned field_value, unsigned *mods) {
    unsigned bits = field_value - 1U;
    if (!field_value || bits > 15U) return false;
    if (bits & 1U) *mods |= KT_MOD_SHIFT;
    if (bits & 10U) *mods |= KT_MOD_ALT;
    if (bits & 4U) *mods |= KT_MOD_CTRL;
    return true;
}

static uint32_t letter_key(unsigned char final) {
    switch (final) {
    case 'A': return KT_KEY_UP;
    case 'B': return KT_KEY_DOWN;
    case 'C': return KT_KEY_RIGHT;
    case 'D': return KT_KEY_LEFT;
    case 'H': return KT_KEY_HOME;
    case 'F': return KT_KEY_END;
    case 'P': return KT_KEY_F1;
    case 'Q': return KT_KEY_F2;
    case 'R': return KT_KEY_F3;
    case 'S': return KT_KEY_F4;
    case 'Z': return KT_KEY_TAB;
    default: return KT_KEY_SEQUENCE;
    }
}

/* "CSI n ~" keys by n; zero entries are not keys. */
static uint32_t tilde_key(unsigned number) {
    static const uint32_t keys[] = {
        0, KT_KEY_HOME, KT_KEY_INSERT, KT_KEY_DELETE, KT_KEY_END, KT_KEY_PAGE_UP, KT_KEY_PAGE_DOWN,
        KT_KEY_HOME, KT_KEY_END, 0, 0, KT_KEY_F1, KT_KEY_F2, KT_KEY_F3, KT_KEY_F4, KT_KEY_F5, 0,
        KT_KEY_F6, KT_KEY_F7, KT_KEY_F8, KT_KEY_F9, KT_KEY_F10, 0, KT_KEY_F11, KT_KEY_F12};
    return number < sizeof(keys) / sizeof(keys[0]) && keys[number] ? keys[number] : KT_KEY_SEQUENCE;
}

/* A letter-final key takes no fields, "1;m", or after SS3 a lone "m". */
static bool letter_modifier(const struct sequence *s, unsigned *mods) {
    if (!s->count) return true;
    if (s->count == 2) return s->values[0] == 1 && modifier(s->values[1], mods);
    return s->ss3 && modifier(s->values[0], mods);
}

/* "CSI n ~" and "CSI n ; m ~". */
static bool tilde_sequence(const struct sequence *s, uint32_t *key, unsigned *mods) {
    if (s->ss3 || !s->count) return false;
    *key = tilde_key(s->values[0]);
    return s->count == 1 || modifier(s->values[1], mods);
}

/* Keys named by their final letter; "CSI Z" is Tab with Shift. */
static bool letter_sequence(const struct sequence *s, uint32_t *key, unsigned *mods) {
    *key = letter_key(s->final);
    if (s->final == 'Z') *mods |= KT_MOD_SHIFT;
    return letter_modifier(s, mods);
}

/* Names a complete CSI or SS3 key; any other sequence stays KT_KEY_SEQUENCE. */
static void name_key(struct kt_event *out) {
    struct sequence s;
    unsigned mods = 0;
    uint32_t key = KT_KEY_SEQUENCE;
    bool known;
    if (!sequence_fields(out, &s)) return;
    known = s.final == '~' ? tilde_sequence(&s, &key, &mods) : letter_sequence(&s, &key, &mods);
    if (!known || key == KT_KEY_SEQUENCE) return;
    out->key = key; out->mods = mods;
}

/* A complete CSI or SS3 sequence: paste brackets, mouse reports and keys. */
static bool sequence(struct kt_input *in, struct kt_event *out) {
    event(in, out, KT_KEY_SEQUENCE, 0);
    if (!strcmp(out->bytes, "\033[200~")) { in->paste = true; out->type = KT_PASTE_BEGIN; }
    else if (!strcmp(out->bytes, "\033[201~")) { in->paste = false; out->type = KT_PASTE_END; }
    else if (in->paste) return true;
    else if (out->bytes[2] == '<') return mouse(out);
    else name_key(out);
    return true;
}

/* The byte after Escape decides: '[' and 'O' open a sequence that runs to its
 * final byte; any other key is that key with Alt. */
static bool escape(struct kt_input *in, unsigned char byte, struct kt_event *out) {
    char lead = in->bytes[1];
    if (lead != '[' && lead != 'O') return text_key(in, out, 1, KT_MOD_ALT);
    if (in->length < 3 || !final_byte(byte)) return false;
    if (in->length == 3 && lead == '[' && byte == 'M' && !in->paste) {
        in->legacy_remaining = 3; in->length = 0; return false;
    }
    return sequence(in, out);
}

/* What an unfinished buffer means once nothing more belongs to it. */
static bool pending(struct kt_input *in, struct kt_event *out) {
    if (!in->length) return false;
    if ((unsigned char)in->bytes[0] >= 0x80) return event(in, out, '?', 0);
    if (in->length == 1) return event(in, out, KT_KEY_ESCAPE, 0);
    if ((unsigned char)in->bytes[1] >= 0x80) return event(in, out, '?', KT_MOD_ALT);
    if (in->length == 2) return event(in, out, (unsigned char)in->bytes[1], KT_MOD_ALT);
    in->length = 0;
    return false;
}

bool kt_input_feed(struct kt_input *in, unsigned char byte, struct kt_event *out) {
    if (in->legacy_remaining) { in->legacy_remaining--; return false; }
    if (byte == 0x1b && (in->length || in->discard)) {
        bool emitted = pending(in, out);
        in->discard = false; in->bytes[0] = '\033'; in->bytes[1] = '\0'; in->length = 1;
        return emitted;
    }
    if (in->discard) { in->discard = !final_byte(byte); return false; }
    if (in->length == sizeof(in->bytes) - 1) {
        in->discard = !final_byte(byte); in->length = 0; return false;
    }
    in->bytes[in->length++] = (char)byte;
    in->bytes[in->length] = '\0';
    if (in->bytes[0] != '\033') return text_key(in, out, 0, 0);
    if (in->length == 1) return false;
    return escape(in, byte, out);
}

bool kt_input_flush(struct kt_input *in, struct kt_event *out) {
    bool emitted = pending(in, out);
    in->length = 0; in->discard = false; in->legacy_remaining = 0;
    return emitted;
}

bool kt_event_is(const struct kt_event *event, uint32_t key, unsigned mods) {
    return event->type == KT_KEY && event->key == key && event->mods == mods;
}

bool kt_event_text(const struct kt_event *event) {
    return event->type == KT_KEY && event->key >= 0x20 && event->key < KT_KEY_UP &&
        !(event->mods & (KT_MOD_CTRL | KT_MOD_ALT));
}
