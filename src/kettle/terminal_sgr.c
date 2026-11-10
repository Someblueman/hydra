#include "terminal_internal.h"

void kt_term_sgr(struct kt_terminal_model *t) {
    unsigned i;
    for (i = 0; i < t->parameter_count; i++) {
        unsigned p = t->parameters[i];
        if (!p) {
            t->pen.attributes = 0;
            t->pen.foreground = t->pen.background = KT_DEFAULT;
        } else if (p == 1) t->pen.attributes |= KT_BOLD;
        else if (p == 2) t->pen.attributes |= KT_DIM;
        else if (p == 3) t->pen.attributes |= KT_ITALIC;
        else if (p == 4) t->pen.attributes |= KT_UNDERLINE;
        else if (p == 7) t->pen.attributes |= KT_REVERSE;
        else if (p == 22) t->pen.attributes &= ~(unsigned)(KT_BOLD | KT_DIM);
        else if (p == 23) t->pen.attributes &= ~(unsigned)KT_ITALIC;
        else if (p == 24) t->pen.attributes &= ~(unsigned)KT_UNDERLINE;
        else if (p == 27) t->pen.attributes &= ~(unsigned)KT_REVERSE;
        else if (p == 39) t->pen.foreground = KT_DEFAULT;
        else if (p == 49) t->pen.background = KT_DEFAULT;
        else if (p >= 30 && p <= 37) t->pen.foreground = KT_INDEX(p - 30);
        else if (p >= 40 && p <= 47) t->pen.background = KT_INDEX(p - 40);
        else if (p >= 90 && p <= 97) t->pen.foreground = KT_INDEX(p - 90 + 8);
        else if (p >= 100 && p <= 107) t->pen.background = KT_INDEX(p - 100 + 8);
        else if (p == 38 || p == 48) {
            kt_color color = KT_DEFAULT;
            if (i + 2 < t->parameter_count && t->parameters[i+1] == 5 && t->parameters[i+2] < 256) {
                color = KT_INDEX(t->parameters[i+2]); i += 2;
            } else if (i + 4 < t->parameter_count && t->parameters[i+1] == 2 &&
                       t->parameters[i+2] < 256 && t->parameters[i+3] < 256 && t->parameters[i+4] < 256) {
                color = KT_RGB(t->parameters[i+2], t->parameters[i+3], t->parameters[i+4]); i += 4;
            } else { if (t->unsupported != UINT64_MAX) t->unsupported++; return; }
            if (p == 38) t->pen.foreground = color; else t->pen.background = color;
        } else if (t->unsupported != UINT64_MAX) t->unsupported++;
    }
}
