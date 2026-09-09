#ifndef TV_TERMINAL_INTERNAL_H
#define TV_TERMINAL_INTERNAL_H
#include "terminal.h"
struct tv_screen *tv_term_screen(struct tv_terminal_model *t);
struct tv_cell tv_term_blank(const struct tv_terminal_model *t);
void tv_term_erase(struct tv_terminal_model *t, int y, int first, int last);
void tv_term_scroll(struct tv_terminal_model *t, int top, int bottom, int amount);
void tv_term_index(struct tv_terminal_model *t, bool reverse);
void tv_term_glyph(struct tv_terminal_model *t, uint32_t cp);
void tv_term_csi(struct tv_terminal_model *t, unsigned char final);
void tv_term_sgr(struct tv_terminal_model *t);
void tv_term_save(struct tv_terminal_model *t);
void tv_term_restore(struct tv_terminal_model *t);
void tv_term_reply(struct tv_terminal_model *t, const char *text);
#endif
