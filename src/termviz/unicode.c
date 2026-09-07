#include "termviz.h"

struct interval { uint32_t first, last; };
#include "unicode_tables.inc"

static bool member(uint32_t cp, const struct interval *set, size_t count) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < set[mid].first) hi = mid;
        else if (cp > set[mid].last) lo = mid + 1;
        else return true;
    }
    return false;
}

int tv_codepoint_width(uint32_t cp) {
    if (cp < 32 || (cp >= 0x7f && cp <= 0x9f) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || (cp & 0xffffU) >= 0xfffeU ||
        member(cp, format, sizeof(format) / sizeof(*format))) return -1;
    if (member(cp, combining, sizeof(combining) / sizeof(*combining))) return 0;
    return member(cp, wide, sizeof(wide) / sizeof(*wide)) ? 2 : 1;
}

size_t tv_utf8_decode(const char *input, size_t size, uint32_t *cp) {
    const unsigned char *s = (const unsigned char *)input;
    size_t need, i;
    uint32_t value;
    if (!size || !input || !cp) return 0;
    *cp = '?';
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) { need = 2; value = s[0] & 31U; }
    else if (s[0] >= 0xe0 && s[0] <= 0xef) { need = 3; value = s[0] & 15U; }
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) { need = 4; value = s[0] & 7U; }
    else return 1;
    if (size < need) return 0;
    for (i = 1; i < need; i++) {
        if ((s[i] & 0xc0U) != 0x80U) return 1;
        value = (value << 6) | (s[i] & 63U);
    }
    if ((need == 3 && value < 0x800) || (need == 4 && value < 0x10000) ||
        value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 1;
    *cp = value;
    return need;
}

size_t tv_utf8_encode(uint32_t cp, char out[4]) {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = '?';
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xc0U | (cp >> 6)); out[1] = (char)(0x80U | (cp & 63U)); return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0U | (cp >> 12)); out[1] = (char)(0x80U | ((cp >> 6) & 63U));
        out[2] = (char)(0x80U | (cp & 63U)); return 3;
    }
    out[0] = (char)(0xf0U | (cp >> 18)); out[1] = (char)(0x80U | ((cp >> 12) & 63U));
    out[2] = (char)(0x80U | ((cp >> 6) & 63U)); out[3] = (char)(0x80U | (cp & 63U)); return 4;
}
