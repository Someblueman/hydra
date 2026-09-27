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

static bool regional(uint32_t cp) { return cp >= 0x1f1e6U && cp <= 0x1f1ffU; }

static bool control_scalar(uint32_t cp) {
    return cp < 32 || (cp >= 0x7f && cp <= 0x9f) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || (cp & 0xffffU) >= 0xfffeU;
}

/* Zero-width scalars: marks attach, VS16 widens a lone narrow symbol. */
static enum tv_join zero_width_join(const struct tv_cell *previous, uint32_t cp) {
    if (cp == 0xfe0fU && previous->width == 1 && previous->glyph > 0x7fU && !previous->combining_count)
        return TV_JOIN_WIDEN;
    return TV_JOIN_ATTACH;
}

/* Emoji modifiers join a wide base; a second regional indicator joins a lone
 * first one, widening it when the base is narrow. */
static enum tv_join emoji_join(const struct tv_cell *previous, uint32_t cp) {
    if (cp >= 0x1f3fbU && cp <= 0x1f3ffU && previous->width == 2) return TV_JOIN_ATTACH;
    if (!regional(cp) || !regional(previous->glyph) || previous->combining_count) return TV_JOIN_NONE;
    return previous->width == 1 ? TV_JOIN_WIDEN : TV_JOIN_ATTACH;
}

/* Cell clustering beyond combining marks, matching tmux 3.5 and common
 * terminals: a ZWJ joins the next scalar, emoji modifiers and a second
 * regional indicator join their base, and VS16 or a flag pair widens a
 * narrow base to two cells. Invisible format characters are dropped rather
 * than shown as '?'. */
enum tv_join tv_cell_join(const struct tv_cell *previous, uint32_t cp) {
    int width = tv_codepoint_width(cp);
    bool present = previous && previous->width;
    uint32_t last;
    if (width < 0) {
        if (control_scalar(cp)) return TV_JOIN_NONE;
        return cp == 0x200dU && present ? TV_JOIN_ATTACH : TV_JOIN_DROP;
    }
    if (!present) return width ? TV_JOIN_NONE : TV_JOIN_DROP;
    last = previous->combining_count ? previous->combining[previous->combining_count - 1] : previous->glyph;
    if (last == 0x200dU) return TV_JOIN_ATTACH;
    return width ? emoji_join(previous, cp) : zero_width_join(previous, cp);
}

/* Box drawing U+2500-U+257F: lines keep their direction, joints become '+'. */
static uint32_t box_fallback(uint32_t cp) {
    uint32_t i = cp - 0x2500U;
    if (i < 0x0cU || (i >= 0x4cU && i < 0x50U)) return (i & 2U) ? '|' : '-';
    if (i == 0x50U) return '=';
    if (i == 0x51U) return '|';
    if (i == 0x71U) return '/';
    if (i == 0x72U) return '\\';
    if (i == 0x73U) return 'X';
    if (i >= 0x74U) return (i & 1U) ? '|' : '-';
    return '+';
}

/* Readable ASCII approximations for ASCII mode, so box drawing, arrows,
 * bullets, quotes and accented Latin letters do not collapse into '?'. */
uint32_t tv_ascii_fallback(uint32_t cp) {
    static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    static const struct { uint32_t first, last; char ascii; } map[] = {
        {0xa0, 0xa0, ' '}, {0xab, 0xab, '"'}, {0xb7, 0xb7, '.'}, {0xbb, 0xbb, '"'},
        {0x2010, 0x2015, '-'}, {0x2018, 0x201b, '\''}, {0x201c, 0x201f, '"'},
        {0x2022, 0x2023, '*'}, {0x2024, 0x2026, '.'}, {0x2039, 0x2039, '<'}, {0x203a, 0x203a, '>'},
        {0x2190, 0x2190, '<'}, {0x2191, 0x2191, '^'}, {0x2192, 0x2192, '>'}, {0x2193, 0x2193, 'v'},
        {0x2194, 0x21ff, '>'}, {0x2212, 0x2212, '-'}, {0x2219, 0x2219, '*'}, {0x23bf, 0x23bf, '|'},
        {0x23f5, 0x23fa, '*'}, {0x2580, 0x259f, '#'}, {0x25a0, 0x25ff, '*'}, {0x2713, 0x2714, '*'},
        {0x2717, 0x2718, 'x'}, {0x2722, 0x274b, '*'}, {0x276f, 0x276f, '>'}, {0x27e8, 0x27e8, '<'},
        {0x27e9, 0x27e9, '>'}, {0x2800, 0x28ff, '.'}
    };
    size_t i;
    if (cp < 0x7fU) return cp;
    if (cp >= 0xc0U && cp <= 0xffU) return (uint32_t)(unsigned char)latin[cp - 0xc0U];
    if (cp >= 0x2500U && cp <= 0x257fU) return box_fallback(cp);
    for (i = 0; i < sizeof(map) / sizeof(*map); i++)
        if (cp >= map[i].first && cp <= map[i].last) return (uint32_t)(unsigned char)map[i].ascii;
    return '?';
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
