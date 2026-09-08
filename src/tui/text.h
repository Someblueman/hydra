#ifndef HYDRA_TUI_TEXT_H
#define HYDRA_TUI_TEXT_H
#include <stdbool.h>
#include <stddef.h>
/* Borrowed input strings; writes only to caller-provided output. */
void copy_text(char *dst, size_t size, const char *src);
bool parse_unsigned(const char *value, unsigned *result);

#endif
