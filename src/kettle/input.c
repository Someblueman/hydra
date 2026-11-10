#include "input.h"
#include <string.h>

void kt_input_init(struct kt_input *in) { memset(in, 0, sizeof(*in)); }

static bool event(struct kt_input *in, struct kt_event *out, uint32_t key) {
    memset(out, 0, sizeof(*out));
    out->type = in->paste ? KT_PASTE : KT_KEY;
    out->key = key; out->length = in->length;
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

bool kt_input_feed(struct kt_input *in, unsigned char byte, struct kt_event *out) {
    uint32_t cp;
    if (in->legacy_remaining) { in->legacy_remaining--; return false; }
    if (in->discard) {
        if (byte >= 0x40 && byte <= 0x7e) { in->discard = false; in->length = 0; }
        return false;
    }
    if (in->length == sizeof(in->bytes) - 1) {
        in->discard = !(byte >= 0x40 && byte <= 0x7e); in->length = 0; return false;
    }
    in->bytes[in->length++] = (char)byte;
    in->bytes[in->length] = '\0';
    if ((unsigned char)in->bytes[0] >= 0x80) {
        size_t used = kt_utf8_decode(in->bytes, in->length, &cp);
        if (!used) return false;
        return event(in, out, cp);
    }
    if (in->bytes[0] != '\033') return event(in, out, byte);
    if (in->length == 1) return false;
    if (in->bytes[1] != '[' && in->bytes[1] != 'O') return event(in, out, KT_KEY_SEQUENCE);
    if (in->length < 3 || byte < 0x40 || byte > 0x7e) return false;
    if (in->length == 3 && in->bytes[1] == '[' && byte == 'M' && !in->paste) {
        in->legacy_remaining = 3; in->length = 0; return false;
    }
    event(in, out, KT_KEY_SEQUENCE);
    if (!strcmp(out->bytes, "\033[200~")) { in->paste = true; out->type = KT_PASTE_BEGIN; }
    else if (!strcmp(out->bytes, "\033[201~")) { in->paste = false; out->type = KT_PASTE_END; }
    else if (in->paste) return true;
    else if (out->bytes[2] == '<') return mouse(out);
    else if (out->length == 3) {
        switch (byte) {
        case 'A': out->key = KT_KEY_UP; break;
        case 'B': out->key = KT_KEY_DOWN; break;
        case 'C': out->key = KT_KEY_RIGHT; break;
        case 'D': out->key = KT_KEY_LEFT; break;
        case 'H': out->key = KT_KEY_HOME; break;
        case 'F': out->key = KT_KEY_END; break;
        default: break;
        }
    } else if (!strcmp(out->bytes, "\033[5~")) out->key = KT_KEY_PAGE_UP;
    else if (!strcmp(out->bytes, "\033[6~")) out->key = KT_KEY_PAGE_DOWN;
    return true;
}

bool kt_input_flush(struct kt_input *in, struct kt_event *out) {
    bool lone = in->length == 1 && in->bytes[0] == '\033';
    bool utf8 = in->length && (unsigned char)in->bytes[0] >= 0x80;
    if (lone || utf8) return event(in, out, lone ? 27 : '?');
    in->length = 0; in->discard = false; in->legacy_remaining = 0; return false;
}
