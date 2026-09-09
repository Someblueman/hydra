#include "terminal_internal.h"

static uint32_t indexed(unsigned value) {
    static const uint32_t ansi[] = {0x000000,0xcd3131,0x0dbc79,0xe5e510,0x2472c8,0xbc3fbc,0x11a8cd,0xe5e5e5,
                                    0x666666,0xf14c4c,0x23d18b,0xf5f543,0x3b8eea,0xd670d6,0x29b8db,0xffffff};
    static const unsigned levels[] = {0,95,135,175,215,255};
    if (value < 16) return ansi[value];
    if (value < 232) {
        unsigned v = value - 16;
        return (levels[v / 36] << 16) | (levels[(v / 6) % 6] << 8) | levels[v % 6];
    }
    value = 8 + (value - 232) * 10;
    return (value << 16) | (value << 8) | value;
}

void tv_term_sgr(struct tv_terminal_model *t) {
    unsigned i;
    for (i = 0; i < t->parameter_count; i++) {
        unsigned p = t->parameters[i];
        if (!p) {
            t->pen.attributes = TV_EXPLICIT;
            t->pen.foreground = t->pen.background = TV_COLOR_DEFAULT;
        } else if (p == 1) t->pen.attributes |= TV_BOLD;
        else if (p == 2) t->pen.attributes |= TV_DIM;
        else if (p == 3) t->pen.attributes |= TV_ITALIC;
        else if (p == 4) t->pen.attributes |= TV_UNDERLINE;
        else if (p == 7) t->pen.attributes |= TV_REVERSE;
        else if (p == 22) t->pen.attributes &= ~(unsigned)(TV_BOLD | TV_DIM);
        else if (p == 23) t->pen.attributes &= ~(unsigned)TV_ITALIC;
        else if (p == 24) t->pen.attributes &= ~(unsigned)TV_UNDERLINE;
        else if (p == 27) t->pen.attributes &= ~(unsigned)TV_REVERSE;
        else if (p == 39) t->pen.foreground = TV_COLOR_DEFAULT;
        else if (p == 49) t->pen.background = TV_COLOR_DEFAULT;
        else if (p >= 30 && p <= 37) t->pen.foreground = indexed(p - 30);
        else if (p >= 40 && p <= 47) t->pen.background = indexed(p - 40);
        else if (p >= 90 && p <= 97) t->pen.foreground = indexed(p - 90 + 8);
        else if (p >= 100 && p <= 107) t->pen.background = indexed(p - 100 + 8);
        else if (p == 38 || p == 48) {
            uint32_t color = TV_COLOR_DEFAULT;
            if (i + 2 < t->parameter_count && t->parameters[i+1] == 5 && t->parameters[i+2] < 256) {
                color = indexed(t->parameters[i+2]); i += 2;
            } else if (i + 4 < t->parameter_count && t->parameters[i+1] == 2 &&
                       t->parameters[i+2] < 256 && t->parameters[i+3] < 256 && t->parameters[i+4] < 256) {
                color = (t->parameters[i+2] << 16) | (t->parameters[i+3] << 8) | t->parameters[i+4]; i += 4;
            } else { if (t->unsupported != UINT64_MAX) t->unsupported++; return; }
            if (p == 38) t->pen.foreground = color; else t->pen.background = color;
        } else if (t->unsupported != UINT64_MAX) t->unsupported++;
    }
}
