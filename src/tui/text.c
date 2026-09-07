#define _POSIX_C_SOURCE 200809L
#include "text.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Model parsing, adapter refresh, and pane preview. */

void copy_text(char *dst, size_t size, const char *src) {
    size_t length = 0U;
    if (size == 0U) return;
    if (src != NULL) {
        while (length + 1U < size && src[length] != '\0') length++;
        memmove(dst, src, length);
    }
    dst[length] = '\0';
}

bool parse_unsigned(const char *value, unsigned *result) {
    char *end = NULL;
    unsigned long parsed;
    if (value == NULL) return false;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > 1000000UL) return false;
    *result = (unsigned)parsed;
    return true;
}

