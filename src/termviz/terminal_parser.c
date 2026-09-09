#include "terminal_internal.h"
#include <string.h>

enum { GROUND, ESCAPE, CSI, STRING, STRING_ESCAPE, CHARSET };

static void control(struct tv_terminal_model *t, unsigned char b) {
    struct tv_screen *s = tv_term_screen(t);
    if (b == '\r') { s->x = 0; s->wrap_pending = false; }
    else if (b == '\n' || b == '\v' || b == '\f') tv_term_index(t, false);
    else if (b == '\b') { if (s->x) s->x--; s->wrap_pending = false; }
    else if (b == '\t') {
        s->x = (s->x / 8 + 1) * 8;
        if (s->x >= s->canvas.width) s->x = s->canvas.width - 1;
        s->wrap_pending = false;
    }
}

static void consume(struct tv_terminal_model *t, unsigned char b) {
    struct tv_screen *s = tv_term_screen(t);
    if (t->parser_state == STRING || t->parser_state == STRING_ESCAPE) {
        if (b == 7 || (t->parser_state == STRING_ESCAPE && b == '\\')) t->parser_state = GROUND;
        else t->parser_state = b == 27 ? STRING_ESCAPE : STRING;
        return;
    }
    if (t->utf8_length) {
        if ((b & 0xc0U) == 0x80U) {
            uint32_t cp;
            size_t used;
            t->utf8[t->utf8_length++] = b;
            used = tv_utf8_decode((const char *)t->utf8, t->utf8_length, &cp);
            if (used || t->utf8_length == sizeof(t->utf8)) {
                tv_term_glyph(t, used == t->utf8_length ? cp : '?'); t->utf8_length = 0;
            }
            return;
        }
        tv_term_glyph(t, '?'); t->utf8_length = 0;
    }
    if (b == 24 || b == 26) { t->parser_state = GROUND; return; }
    if (b == 27) { t->parser_state = ESCAPE; return; }
    if (b < 32) { control(t, b); return; }
    if (b == 127) return;
    if (t->parser_state == CHARSET) { t->parser_state = GROUND; return; }
    if (t->parser_state == ESCAPE) {
        t->parser_state = GROUND;
        switch (b) {
        case '[':
            memset(t->parameters, 0, sizeof(t->parameters));
            memset(t->parameter_present, 0, sizeof(t->parameter_present));
            t->parameter_count = 1; t->private_mode = t->invalid_sequence = false;
            t->parser_state = CSI; break;
        case ']': case 'P': case '_': case '^': case 'X': t->parser_state = STRING; break;
        case '(': case ')': case '*': case '+': case '%': case '#': t->parser_state = CHARSET; break;
        case '7': tv_term_save(t); break;
        case '8': tv_term_restore(t); break;
        case 'D': tv_term_index(t, false); break;
        case 'E': s->x = 0; tv_term_index(t, false); break;
        case 'M': tv_term_index(t, true); break;
        case 'c': tv_term_reset(t); break;
        case '=': case '>': break; /* numeric keypad has no separate demo key source */
        default: if (t->unsupported != UINT64_MAX) t->unsupported++; break;
        }
        return;
    }
    if (t->parser_state == CSI) {
        unsigned i = t->parameter_count - 1;
        if (b == '?' && i == 0 && !t->parameter_present[0] && !t->private_mode) t->private_mode = true;
        else if (b >= '0' && b <= '9') {
            if (t->parameters[i] > 6553 || (t->parameters[i] == 6553 && b > '5')) t->invalid_sequence = true;
            else { t->parameters[i] = t->parameters[i] * 10 + (unsigned)(b - '0'); t->parameter_present[i] = true; }
        } else if (b == ';') {
            if (t->parameter_count == 16) t->invalid_sequence = true;
            else t->parameter_count++;
        } else if (b >= 0x40 && b <= 0x7e) { tv_term_csi(t, b); t->parser_state = GROUND; }
        else t->invalid_sequence = true;
        return;
    }
    if (b >= 0xc2 && b <= 0xf4) { t->utf8[0] = b; t->utf8_length = 1; }
    else tv_term_glyph(t, b < 0x80 ? b : '?');
}

void tv_term_feed(struct tv_terminal_model *t, const void *data, size_t length) {
    const unsigned char *bytes = data;
    size_t i;
    if (!data) return;
    for (i = 0; i < length; i++) consume(t, bytes[i]);
}

void tv_term_finish(struct tv_terminal_model *t) {
    if (t->utf8_length) tv_term_glyph(t, '?');
    t->utf8_length = 0; t->parser_state = GROUND;
}
