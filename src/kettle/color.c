#include "style.h"
#include <string.h>

/* Colour arithmetic, profile detection and the SGR a style needs. Nothing
 * here touches a terminal or the environment: detection reads through a
 * caller-supplied lookup, and SGR is written to a caller's buffer. */

bool kt_style_equal(struct kt_style a, struct kt_style b) {
    return a.fg == b.fg && a.bg == b.bg && a.attrs == b.attrs;
}

/* ---- palettes ----------------------------------------------------------- */

static const unsigned char cube_levels[6] = {0, 95, 135, 175, 215, 255};

uint32_t kt_palette_rgb(unsigned index) {
    static const uint32_t sixteen[16] = {
        0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
        0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff};
    index &= 255U;
    if (index < 16) return sixteen[index];
    if (index < 232) {
        unsigned v = index - 16;
        return (uint32_t)cube_levels[v / 36] << 16 | (uint32_t)cube_levels[(v / 6) % 6] << 8 | cube_levels[v % 6];
    }
    index = 8 + (index - 232) * 10;
    return (uint32_t)index << 16 | (uint32_t)index << 8 | index;
}

/* Squared distance weighted by how sensitive the eye is to each channel at
 * this level of red ("redmean"); integer arithmetic throughout. */
static uint32_t distance(uint32_t a, uint32_t b) {
    int32_t ar = (int32_t)(a >> 16 & 255U), br = (int32_t)(b >> 16 & 255U);
    int32_t mean = (ar + br) / 2, dr = ar - br;
    int32_t dg = (int32_t)(a >> 8 & 255U) - (int32_t)(b >> 8 & 255U);
    int32_t db = (int32_t)(a & 255U) - (int32_t)(b & 255U);
    return (uint32_t)(((512 + mean) * dr * dr) / 256 + 4 * dg * dg + ((767 - mean) * db * db) / 256);
}

/* The cube level at or below a channel value, never the last one. */
static unsigned level_below(unsigned value) {
    unsigned level = 0;
    while (level < 4 && cube_levels[level + 1] <= value) level++;
    return level;
}

struct nearest { uint32_t rgb, distance; unsigned index; };

static void consider(struct nearest *best, unsigned index) {
    uint32_t d = distance(best->rgb, kt_palette_rgb(index));
    if (d < best->distance) { best->distance = d; best->index = index; }
}

/* The nearest cube entry lies among the eight around the colour; the grey
 * ramp is short enough to try whole. */
unsigned kt_rgb_to_256(uint32_t rgb) {
    struct nearest best = {0, UINT32_MAX, 16};
    unsigned r, g, b, corner, grey;
    best.rgb = rgb &= UINT32_C(0xffffff);
    r = level_below(rgb >> 16 & 255U); g = level_below(rgb >> 8 & 255U); b = level_below(rgb & 255U);
    for (corner = 0; corner < 8; corner++)
        consider(&best, 16 + 36 * (r + (corner >> 2 & 1U)) + 6 * (g + (corner >> 1 & 1U)) + b + (corner & 1U));
    for (grey = 232; grey < 256; grey++) consider(&best, grey);
    return best.index;
}

unsigned kt_rgb_to_16(uint32_t rgb) {
    struct nearest best = {0, UINT32_MAX, 0};
    unsigned index;
    best.rgb = rgb & UINT32_C(0xffffff);
    for (index = 0; index < 16; index++) consider(&best, index);
    return best.index;
}

/* ---- profile ------------------------------------------------------------ */

struct environment { const char *(*lookup)(void *context, const char *name); void *context; };

static const char *variable(const struct environment *env, const char *name) {
    const char *value = env->lookup ? env->lookup(env->context, name) : NULL;
    return value ? value : "";
}

static bool prefixed(const char *text, const char *prefix) {
    return !strncmp(text, prefix, strlen(prefix));
}

static bool suffixed(const char *text, const char *suffix) {
    size_t length = strlen(text), tail = strlen(suffix);
    return length >= tail && !strcmp(text + length - tail, suffix);
}

/* Set to anything but nothing, "0" or "false". */
static bool enabled(const struct environment *env, const char *name) {
    const char *value = variable(env, name);
    return value[0] && strcmp(value, "0") && strcmp(value, "false");
}

/* Terminals whose TERM alone promises 24-bit colour. */
static bool truecolor_term(const char *term) {
    static const char *const names[] = {"alacritty", "contour", "foot", "ghostty", "kitty", "rio", "wezterm"};
    size_t i;
    if (!strcmp(term, "st") || prefixed(term, "st-") || suffixed(term, "direct")) return true;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (strstr(term, names[i])) return true;
    return false;
}

static bool truecolor_claimed(const struct environment *env) {
    const char *value = variable(env, "COLORTERM");
    return !strcmp(value, "truecolor") || !strcmp(value, "24bit");
}

/* What TERM and its companions say the terminal can show. */
static enum kt_profile claimed(const struct environment *env) {
    const char *term = variable(env, "TERM");
    bool multiplexed = prefixed(term, "tmux") || prefixed(term, "screen");
    /* GNU screen drops 24-bit colour; tmux, which may also call itself
     * "screen", understands it and sets TMUX. */
    bool passes_truecolor = !multiplexed || prefixed(term, "tmux") || variable(env, "TMUX")[0];
    if (!term[0] || !strcmp(term, "dumb")) return KT_PROFILE_NONE;
    if (truecolor_term(term) || variable(env, "WT_SESSION")[0]) return KT_PROFILE_TRUECOLOR;
    if (truecolor_claimed(env) && passes_truecolor) return KT_PROFILE_TRUECOLOR;
    return multiplexed || suffixed(term, "256color") ? KT_PROFILE_256 : KT_PROFILE_16;
}

enum kt_profile kt_profile_detect(const char *(*lookup)(void *context, const char *name),
                                  void *context, bool terminal) {
    struct environment env;
    enum kt_profile from_term;
    env.lookup = lookup; env.context = context;
    from_term = claimed(&env);
    if (variable(&env, "NO_COLOR")[0]) return KT_PROFILE_NONE;
    if (enabled(&env, "CLICOLOR_FORCE")) return from_term > KT_PROFILE_16 ? from_term : KT_PROFILE_16;
    if (!terminal) return KT_PROFILE_NONE;
    /* CLICOLOR vouches for a terminal that sets no TERM at all. */
    if (enabled(&env, "CLICOLOR") && !variable(&env, "TERM")[0]) return KT_PROFILE_16;
    return from_term;
}

/* ---- resolution --------------------------------------------------------- */

/* A concrete colour (default, RGB or index) as the profile can show it. */
static kt_color shown(kt_color color, enum kt_profile profile) {
    uint32_t value = KT_COLOR_VALUE(color);
    if (profile <= KT_PROFILE_NONE) return KT_DEFAULT;
    if (KT_COLOR_KIND(color) == KT_KIND_RGB) {
        if (profile == KT_PROFILE_TRUECOLOR) return color;
        return KT_INDEX(profile == KT_PROFILE_256 ? kt_rgb_to_256(value) : kt_rgb_to_16(value));
    }
    if (KT_COLOR_KIND(color) != KT_KIND_INDEX) return KT_DEFAULT;
    if (profile == KT_PROFILE_16 && (value & 255U) > 15) return KT_INDEX(kt_rgb_to_16(kt_palette_rgb(value)));
    return KT_INDEX(value);
}

static kt_color token_shown(const struct kt_theme_color *entry, enum kt_profile profile, unsigned *attrs) {
    *attrs |= entry->attrs;
    if (profile <= KT_PROFILE_NONE) { *attrs |= entry->plain; return KT_DEFAULT; }
    if (profile == KT_PROFILE_256 && entry->ansi256) return shown(entry->ansi256, profile);
    if (profile == KT_PROFILE_16 && entry->ansi16) return shown(entry->ansi16, profile);
    return shown(entry->color, profile);
}

static kt_color resolved(kt_color color, const struct kt_paint *paint, unsigned *attrs) {
    const struct kt_theme *theme = paint->theme ? paint->theme : &kt_theme_terminal;
    if (KT_COLOR_KIND(color) != KT_KIND_TOKEN) return shown(color, paint->profile);
    if (KT_COLOR_VALUE(color) >= KT_TOKEN_COUNT) return KT_DEFAULT;
    return token_shown(&theme->tokens[KT_COLOR_VALUE(color)], paint->profile, attrs);
}

struct kt_style kt_style_resolve(struct kt_style style, const struct kt_paint *paint) {
    static const struct kt_paint none = {NULL, KT_PROFILE_NONE};
    struct kt_style out;
    if (!paint) paint = &none;
    out.attrs = style.attrs;
    out.fg = resolved(style.fg, paint, &out.attrs);
    out.bg = resolved(style.bg, paint, &out.attrs);
    out.attrs &= (unsigned)(KT_BOLD | KT_DIM | KT_ITALIC | KT_UNDERLINE | KT_REVERSE);
    return out;
}

/* ---- SGR ---------------------------------------------------------------- */

struct sgr { char text[KT_SGR_MAX]; size_t length; };

static void parameter(struct sgr *s, unsigned value) {
    char digits[4];
    size_t count = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
    if (s->length + count + 2 >= sizeof(s->text)) return;
    s->text[s->length] = s->length ? ';' : '[';
    s->length++;
    while (count) s->text[s->length++] = digits[--count];
}

static void color_parameters(struct sgr *s, kt_color color, bool foreground) {
    uint32_t value = KT_COLOR_VALUE(color);
    unsigned base = foreground ? 30U : 40U;
    if (KT_COLOR_KIND(color) == KT_KIND_RGB) {
        parameter(s, base + 8); parameter(s, 2);
        parameter(s, value >> 16 & 255U); parameter(s, value >> 8 & 255U); parameter(s, value & 255U);
    } else if (KT_COLOR_KIND(color) != KT_KIND_INDEX) parameter(s, base + 9);
    else if (value < 8) parameter(s, base + value);
    else if (value < 16) parameter(s, base + 60 + value - 8);
    else { parameter(s, base + 8); parameter(s, 5); parameter(s, value); }
}

static void attribute_parameters(struct sgr *s, unsigned attrs) {
    static const unsigned char codes[5] = {1, 2, 3, 4, 7};
    unsigned bit;
    for (bit = 0; bit < 5; bit++) if (attrs & (1U << bit)) parameter(s, codes[bit]);
}

/* Everything the target needs, from a clean slate. */
static void from_reset(struct sgr *s, struct kt_style to) {
    parameter(s, 0);
    attribute_parameters(s, to.attrs);
    if (to.fg != KT_DEFAULT) color_parameters(s, to.fg, true);
    if (to.bg != KT_DEFAULT) color_parameters(s, to.bg, false);
}

/* Only what differs. Bold and dim share one "off" code, so dropping either
 * sets the survivor again. */
static void from_style(struct sgr *s, struct kt_style from, struct kt_style to) {
    unsigned removed = from.attrs & ~to.attrs, added = to.attrs & ~from.attrs;
    if (removed & (unsigned)(KT_BOLD | KT_DIM)) {
        parameter(s, 22);
        added |= to.attrs & (unsigned)(KT_BOLD | KT_DIM);
    }
    if (removed & KT_ITALIC) parameter(s, 23);
    if (removed & KT_UNDERLINE) parameter(s, 24);
    if (removed & KT_REVERSE) parameter(s, 27);
    attribute_parameters(s, added);
    if (from.fg != to.fg) color_parameters(s, to.fg, true);
    if (from.bg != to.bg) color_parameters(s, to.bg, false);
}

size_t kt_sgr(char out[KT_SGR_MAX], const struct kt_style *from, struct kt_style to) {
    struct sgr reset = {{0}, 0}, change = {{0}, 0};
    const struct sgr *chosen = &reset;
    out[0] = '\0';
    if (from && kt_style_equal(*from, to)) return 0;
    from_reset(&reset, to);
    if (from) {
        from_style(&change, *from, to);
        if (change.length <= reset.length) chosen = &change;
    }
    out[0] = '\033';
    memcpy(out + 1, chosen->text, chosen->length);
    out[chosen->length + 1] = 'm';
    out[chosen->length + 2] = '\0';
    return chosen->length + 2;
}
