#ifndef KT_TERMINAL_INTERNAL_H
#define KT_TERMINAL_INTERNAL_H
#include "terminal.h"
struct kt_screen *kt_term_screen(struct kt_terminal_model *t);
struct kt_cell kt_term_blank(const struct kt_terminal_model *t);
void kt_term_erase(struct kt_terminal_model *t, int y, int first, int last);
void kt_term_scroll(struct kt_terminal_model *t, int top, int bottom, int amount);
void kt_term_index(struct kt_terminal_model *t, bool reverse);
void kt_term_glyph(struct kt_terminal_model *t, uint32_t cp);
void kt_term_csi(struct kt_terminal_model *t, unsigned char final);
void kt_term_sgr(struct kt_terminal_model *t);
void kt_term_save(struct kt_terminal_model *t);
void kt_term_restore(struct kt_terminal_model *t);
void kt_term_reply(struct kt_terminal_model *t, const char *text);
#endif
