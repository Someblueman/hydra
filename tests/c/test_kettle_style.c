#include "kettle/kettle.h"
#include <stdlib.h>
#include <string.h>

static void check_at(bool passed, const char *what, int line) {
    if (passed) return;
    fprintf(stderr, "FAIL tests/c/test_kettle_style.c:%d: %s\n", line, what);
    exit(1);
}
#define CHECK(condition) check_at((condition), #condition, __LINE__)

/* ---- colour conversion -------------------------------------------------- */

/* The documented metric, written out again so the search is checked against
 * its definition and not against itself. */
static uint32_t redmean(uint32_t a, uint32_t b) {
    int32_t ar = (int32_t)(a >> 16 & 255U), br = (int32_t)(b >> 16 & 255U);
    int32_t mean = (ar + br) / 2, dr = ar - br;
    int32_t dg = (int32_t)(a >> 8 & 255U) - (int32_t)(b >> 8 & 255U);
    int32_t db = (int32_t)(a & 255U) - (int32_t)(b & 255U);
    return (uint32_t)(((512 + mean) * dr * dr) / 256 + 4 * dg * dg + ((767 - mean) * db * db) / 256);
}

static void palette(void) {
    CHECK(kt_palette_rgb(16) == 0x000000);
    CHECK(kt_palette_rgb(231) == 0xffffff);
    CHECK(kt_palette_rgb(196) == 0xff0000);
    CHECK(kt_palette_rgb(46) == 0x00ff00);
    CHECK(kt_palette_rgb(21) == 0x0000ff);
    CHECK(kt_palette_rgb(67) == 0x5f87af);
    CHECK(kt_palette_rgb(117) == 0x87d7ff);
    CHECK(kt_palette_rgb(232) == 0x080808);
    CHECK(kt_palette_rgb(244) == 0x808080);
    CHECK(kt_palette_rgb(255) == 0xeeeeee);
    CHECK(kt_palette_rgb(1) == 0xcd0000);
    CHECK(kt_palette_rgb(15) == 0xffffff);
}

/* The smallest distance from a colour to any of the 240 fixed entries. */
static uint32_t nearest_of_240(uint32_t rgb) {
    uint32_t best = UINT32_MAX;
    unsigned i;
    for (i = 16; i < 256; i++) if (redmean(rgb, kt_palette_rgb(i)) < best) best = redmean(rgb, kt_palette_rgb(i));
    return best;
}

static void to_256(void) {
    static const unsigned char levels[6] = {0, 95, 135, 175, 215, 255};
    unsigned i, r, g, b;
    /* Every fixed entry is its own nearest colour. */
    for (i = 16; i < 256; i++) CHECK(kt_rgb_to_256(kt_palette_rgb(i)) == i);
    /* Primaries, black and white land on the cube's corners. */
    CHECK(kt_rgb_to_256(0xff0000) == 196);
    CHECK(kt_rgb_to_256(0x00ff00) == 46);
    CHECK(kt_rgb_to_256(0x0000ff) == 21);
    CHECK(kt_rgb_to_256(0x000000) == 16);
    CHECK(kt_rgb_to_256(0xffffff) == 231);
    CHECK(kt_rgb_to_256(0xffff00) == 226);
    CHECK(kt_rgb_to_256(0x00ffff) == 51);
    CHECK(kt_rgb_to_256(0xff00ff) == 201);
    /* Each cube level along each axis, and a step either side of an edge
     * between two levels. */
    for (i = 0; i < 6; i++) {
        CHECK(kt_rgb_to_256((uint32_t)levels[i] << 16 | 0x00ff00) == 16 + 36 * i + 30);
        CHECK(kt_rgb_to_256((uint32_t)levels[i] << 8 | 0xff0000) == 196 + 6 * i);
        CHECK(kt_rgb_to_256((uint32_t)levels[i] | 0xff0000) == 196 + i);
    }
    CHECK(kt_rgb_to_256(0xea0000) == 160);
    CHECK(kt_rgb_to_256(0xec0000) == 196);
    CHECK(kt_rgb_to_256(0x00c200) == 34);
    CHECK(kt_rgb_to_256(0x00c400) == 40);
    /* Greys go to the nearest grey there is: a step of the ramp, one of the
     * cube's own four, or black and white beyond the ends of the ramp. */
    for (i = 0; i < 24; i++) CHECK(kt_rgb_to_256((8 + 10 * i) * 0x010101U) == 232 + i);
    CHECK(kt_rgb_to_256(0x7f7f7f) == 244);
    CHECK(kt_rgb_to_256(0x131313) == 233);
    CHECK(kt_rgb_to_256(0x0d0d0d) == 232);
    CHECK(kt_rgb_to_256(0x030303) == 16);
    CHECK(kt_rgb_to_256(0xf2f2f2) == 255);
    CHECK(kt_rgb_to_256(0xf8f8f8) == 231);
    CHECK(kt_rgb_to_256(0x606060) == 59);
    CHECK(kt_rgb_to_256(0x616161) == 241);
    /* Whatever it picks is nearest among all 240 by the documented metric. */
    for (r = 0; r < 256; r += 15) for (g = 0; g < 256; g += 15) for (b = 0; b < 256; b += 15) {
        uint32_t rgb = r << 16 | g << 8 | b;
        CHECK(redmean(rgb, kt_palette_rgb(kt_rgb_to_256(rgb))) == nearest_of_240(rgb));
    }
    /* Only the low 24 bits are a colour. */
    CHECK(kt_rgb_to_256(0xff00ff00U) == 46);
}

static void to_16(void) {
    unsigned i;
    for (i = 0; i < 16; i++) CHECK(kt_rgb_to_16(kt_palette_rgb(i)) == i);
    CHECK(kt_rgb_to_16(0x101010) == 0);
    CHECK(kt_rgb_to_16(0xf8f8f8) == 15);
    CHECK(kt_rgb_to_16(0xd0d0d0) == 7);
    CHECK(kt_rgb_to_16(0x808080) == 8);
    CHECK(kt_rgb_to_16(0xee0000) == 9);
    CHECK(kt_rgb_to_16(0xc00000) == 1);
    CHECK(kt_rgb_to_16(0x00d000) == 2);
    CHECK(kt_rgb_to_16(0x10f010) == 10);
    CHECK(kt_rgb_to_16(0x10f0f0) == 14);
    /* A dark, slightly blue surface is black, not blue. */
    CHECK(kt_rgb_to_16(0x16181d) == 0);
    CHECK(kt_rgb_to_16(0x1c1c1c) == 0);
}

/* ---- profile detection -------------------------------------------------- */

struct environment { const char *term, *colorterm, *no_color, *clicolor, *force, *tmux, *windows; };

static const char *lookup(void *context, const char *name) {
    const struct environment *e = context;
    if (!strcmp(name, "TERM")) return e->term;
    if (!strcmp(name, "COLORTERM")) return e->colorterm;
    if (!strcmp(name, "NO_COLOR")) return e->no_color;
    if (!strcmp(name, "CLICOLOR")) return e->clicolor;
    if (!strcmp(name, "CLICOLOR_FORCE")) return e->force;
    if (!strcmp(name, "TMUX")) return e->tmux;
    if (!strcmp(name, "WT_SESSION")) return e->windows;
    return NULL;
}

static void profiles(void) {
    static const struct { struct environment env; bool terminal; enum kt_profile expected; } cases[] = {
        /* TERM alone */
        {{"xterm", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_16},
        {{"vt100", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_16},
        {{"linux", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_16},
        {{"xterm-256color", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_256},
        {{"rxvt-unicode-256color", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_256},
        {{"xterm-kitty", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"alacritty", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"xterm-ghostty", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"wezterm", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"foot-extra", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"st-256color", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"xterm-direct", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        /* "st" is a terminal, not any name that contains the letters. */
        {{"nsterm", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_16},
        /* nothing to draw colour on */
        {{NULL, 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"dumb", "truecolor", 0, 0, 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"xterm-256color", "truecolor", 0, 0, 0, 0, 0}, false, KT_PROFILE_NONE},
        /* COLORTERM */
        {{"xterm-256color", "truecolor", 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"xterm", "24bit", 0, 0, 0, 0, 0}, true, KT_PROFILE_TRUECOLOR},
        {{"xterm-256color", "rxvt", 0, 0, 0, 0, 0}, true, KT_PROFILE_256},
        /* tmux converts 24-bit colour itself; GNU screen does not pass it on */
        {{"tmux-256color", 0, 0, 0, 0, "/tmp/tmux-0/default,1,0", 0}, true, KT_PROFILE_256},
        {{"tmux", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_256},
        {{"screen", 0, 0, 0, 0, 0, 0}, true, KT_PROFILE_256},
        {{"tmux-256color", "truecolor", 0, 0, 0, "/tmp/tmux-0/default,1,0", 0}, true, KT_PROFILE_TRUECOLOR},
        {{"screen-256color", "truecolor", 0, 0, 0, "/tmp/tmux-0/default,1,0", 0}, true, KT_PROFILE_TRUECOLOR},
        {{"screen-256color", "truecolor", 0, 0, 0, 0, 0}, true, KT_PROFILE_256},
        {{"screen.xterm-256color", "truecolor", 0, 0, 0, "", 0}, true, KT_PROFILE_256},
        /* NO_COLOR wins over everything; an empty value is not set */
        {{"xterm-kitty", "truecolor", "1", 0, 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"xterm-256color", 0, "yes", "1", "1", 0, 0}, true, KT_PROFILE_NONE},
        {{"xterm-256color", 0, "0", 0, 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"xterm-256color", 0, "", 0, 0, 0, 0}, true, KT_PROFILE_256},
        /* CLICOLOR_FORCE: colour without a terminal, at least sixteen */
        {{"xterm-256color", 0, 0, 0, "1", 0, 0}, false, KT_PROFILE_256},
        {{"xterm", "truecolor", 0, 0, "1", 0, 0}, false, KT_PROFILE_TRUECOLOR},
        {{NULL, 0, 0, 0, "1", 0, 0}, false, KT_PROFILE_16},
        {{"dumb", 0, 0, 0, "true", 0, 0}, true, KT_PROFILE_16},
        {{"xterm-256color", 0, 0, 0, "0", 0, 0}, false, KT_PROFILE_NONE},
        {{"xterm-256color", 0, 0, 0, "", 0, 0}, false, KT_PROFILE_NONE},
        /* CLICOLOR vouches for a terminal without TERM, and for nothing else */
        {{NULL, 0, 0, "1", 0, 0, 0}, true, KT_PROFILE_16},
        {{NULL, 0, 0, "1", 0, 0, 0}, false, KT_PROFILE_NONE},
        {{NULL, 0, 0, "0", 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"dumb", 0, 0, "1", 0, 0, 0}, true, KT_PROFILE_NONE},
        {{"xterm", 0, 0, "1", 0, 0, 0}, true, KT_PROFILE_16},
        /* Windows Terminal */
        {{"xterm-256color", 0, 0, 0, 0, 0, "f3e1"}, true, KT_PROFILE_TRUECOLOR}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct environment env = cases[i].env;
        CHECK(kt_profile_detect(lookup, &env, cases[i].terminal) == cases[i].expected);
    }
    /* No environment at all is a terminal nobody described. */
    CHECK(kt_profile_detect(NULL, NULL, true) == KT_PROFILE_NONE);
}

/* ---- theme resolution --------------------------------------------------- */

enum { TOKEN_BRAND = KT_TOKEN_USER, TOKEN_PLAIN_ONLY };

static const struct kt_theme sample = {"sample", {
    [KT_TOKEN_TEXT] = {KT_HEX(0xD9DCE3), 0, 0, 0, 0},
    [KT_TOKEN_SURFACE] = {KT_HEX(0x16181D), 0, 0, 0, 0},
    [KT_TOKEN_MUTED] = {KT_DEFAULT, 0, 0, KT_DIM, 0},
    [KT_TOKEN_SELECTION] = {KT_HEX(0x2B3550), KT_INDEX(24), KT_INDEX(4), 0, KT_REVERSE},
    [KT_TOKEN_WARNING] = {KT_INDEX(214), 0, 0, 0, KT_UNDERLINE},
    [TOKEN_BRAND] = {KT_RGB(255, 0, 128), 0, KT_INDEX(13), KT_ITALIC, 0},
    [TOKEN_PLAIN_ONLY] = {KT_INDEX(3), 0, 0, 0, KT_BOLD}
}};

static struct kt_style shown(struct kt_style style, const struct kt_theme *theme, enum kt_profile profile) {
    struct kt_paint paint;
    paint.theme = theme; paint.profile = profile;
    return kt_style_resolve(style, &paint);
}

static void resolution(void) {
    struct kt_style base = KT_BASE, selected = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_SELECTION, 0);
    struct kt_style brand = KT_STYLE(KT_TOKEN(TOKEN_BRAND), KT_DEFAULT, KT_BOLD), out;
    /* 24-bit: tokens become the theme's colours unchanged. */
    CHECK(kt_style_equal(shown(base, &sample, KT_PROFILE_TRUECOLOR), KT_STYLE(KT_HEX(0xD9DCE3), KT_HEX(0x16181D), 0)));
    /* 256: the nearest fixed entry, or the entry the theme names. */
    CHECK(kt_style_equal(shown(base, &sample, KT_PROFILE_256), KT_STYLE(KT_INDEX(253), KT_INDEX(234), 0)));
    CHECK(kt_style_equal(shown(selected, &sample, KT_PROFILE_256), KT_STYLE(KT_INDEX(253), KT_INDEX(24), 0)));
    /* 16: likewise among the terminal's own sixteen. */
    CHECK(kt_style_equal(shown(base, &sample, KT_PROFILE_16), KT_STYLE(KT_INDEX(7), KT_INDEX(0), 0)));
    CHECK(kt_style_equal(shown(selected, &sample, KT_PROFILE_16), KT_STYLE(KT_INDEX(7), KT_INDEX(4), 0)));
    /* None: no colour; what a token says without colour takes over. */
    CHECK(kt_style_equal(shown(base, &sample, KT_PROFILE_NONE), KT_STYLE(KT_DEFAULT, KT_DEFAULT, 0)));
    CHECK(kt_style_equal(shown(selected, &sample, KT_PROFILE_NONE), KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_REVERSE)));
    CHECK(kt_style_equal(shown(KT_WARNING, &sample, KT_PROFILE_NONE), KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_UNDERLINE)));
    CHECK(kt_style_equal(shown(KT_WARNING, &sample, KT_PROFILE_256), KT_STYLE(KT_INDEX(214), KT_INDEX(234), 0)));
    /* A token's own attributes join the style's at every profile. */
    CHECK(kt_style_equal(shown(KT_MUTED, &sample, KT_PROFILE_TRUECOLOR), KT_STYLE(KT_DEFAULT, KT_HEX(0x16181D), KT_DIM)));
    CHECK(kt_style_equal(shown(KT_MUTED, &sample, KT_PROFILE_NONE), KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_DIM)));
    /* Application tokens resolve like the library's. */
    CHECK(kt_style_equal(shown(brand, &sample, KT_PROFILE_TRUECOLOR), KT_STYLE(KT_RGB(255, 0, 128), KT_DEFAULT, KT_BOLD | KT_ITALIC)));
    CHECK(kt_style_equal(shown(brand, &sample, KT_PROFILE_256), KT_STYLE(KT_INDEX(198), KT_DEFAULT, KT_BOLD | KT_ITALIC)));
    CHECK(kt_style_equal(shown(brand, &sample, KT_PROFILE_16), KT_STYLE(KT_INDEX(13), KT_DEFAULT, KT_BOLD | KT_ITALIC)));
    CHECK(kt_style_equal(shown(brand, &sample, KT_PROFILE_NONE), KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_BOLD | KT_ITALIC)));
    out = shown(KT_STYLE(KT_TOKEN(TOKEN_PLAIN_ONLY), KT_DEFAULT, 0), &sample, KT_PROFILE_16);
    CHECK(kt_style_equal(out, KT_STYLE(KT_INDEX(3), KT_DEFAULT, 0)));
    out = shown(KT_STYLE(KT_TOKEN(TOKEN_PLAIN_ONLY), KT_DEFAULT, 0), &sample, KT_PROFILE_NONE);
    CHECK(kt_style_equal(out, KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_BOLD)));
    /* A token the theme does not define, one past the table, and KT_KEEP
     * outside a drawing call are all the terminal's default. */
    out = shown(KT_STYLE(KT_COLOR_DANGER, KT_TOKEN(200), 0), &sample, KT_PROFILE_TRUECOLOR);
    CHECK(kt_style_equal(out, KT_STYLE(KT_DEFAULT, KT_DEFAULT, 0)));
    out = shown(KT_STYLE(KT_KEEP, KT_KEEP, KT_BOLD), &sample, KT_PROFILE_TRUECOLOR);
    CHECK(kt_style_equal(out, KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_BOLD)));
    /* Concrete colours need no theme: 24-bit is kept, cut to the palette or
     * dropped; an index survives wherever the terminal has that entry. */
    out = shown(KT_STYLE(KT_RGB(255, 0, 0), KT_INDEX(196), 0), &sample, KT_PROFILE_TRUECOLOR);
    CHECK(kt_style_equal(out, KT_STYLE(KT_RGB(255, 0, 0), KT_INDEX(196), 0)));
    out = shown(KT_STYLE(KT_RGB(255, 0, 0), KT_INDEX(196), 0), &sample, KT_PROFILE_256);
    CHECK(kt_style_equal(out, KT_STYLE(KT_INDEX(196), KT_INDEX(196), 0)));
    out = shown(KT_STYLE(KT_RGB(255, 0, 0), KT_INDEX(196), 0), &sample, KT_PROFILE_16);
    CHECK(kt_style_equal(out, KT_STYLE(KT_INDEX(9), KT_INDEX(9), 0)));
    out = shown(KT_STYLE(KT_INDEX(1), KT_INDEX(12), 0), &sample, KT_PROFILE_16);
    CHECK(kt_style_equal(out, KT_STYLE(KT_INDEX(1), KT_INDEX(12), 0)));
    out = shown(KT_STYLE(KT_RGB(255, 0, 0), KT_INDEX(1), KT_UNDERLINE), &sample, KT_PROFILE_NONE);
    CHECK(kt_style_equal(out, KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_UNDERLINE)));
    /* No paint, no theme and an undetected profile are all safe. */
    CHECK(kt_style_equal(kt_style_resolve(KT_SELECTED, NULL), KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_REVERSE)));
    CHECK(kt_style_equal(shown(KT_SELECTED, NULL, KT_PROFILE_16), KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_REVERSE)));
    CHECK(kt_style_equal(shown(base, &sample, KT_PROFILE_AUTO), KT_STYLE(KT_DEFAULT, KT_DEFAULT, 0)));
    /* Attribute bits that are not attributes never reach the terminal. */
    CHECK(shown(KT_STYLE(KT_DEFAULT, KT_DEFAULT, 0xff00U | KT_BOLD), &sample, KT_PROFILE_256).attrs == KT_BOLD);
}

/* Every built-in theme tells its parts apart at every profile: text from its
 * surface, the selection from unselected text, muted text from a title. */
static void builtin_themes(void) {
    static const struct kt_theme *const themes[] = {&kt_theme_dark, &kt_theme_light, &kt_theme_terminal};
    static const enum kt_profile profiles[] = {KT_PROFILE_NONE, KT_PROFILE_16, KT_PROFILE_256, KT_PROFILE_TRUECOLOR};
    size_t t, p;
    for (t = 0; t < 3; t++) for (p = 0; p < 4; p++) {
        struct kt_style base = shown(KT_BASE, themes[t], profiles[p]);
        struct kt_style selected = shown(KT_SELECTED, themes[t], profiles[p]);
        struct kt_style accent_pill = shown(KT_STYLE(KT_COLOR_ON_ACCENT, KT_COLOR_ACCENT, 0), themes[t], profiles[p]);
        CHECK(!kt_style_equal(base, selected));
        CHECK(!kt_style_equal(shown(KT_MUTED, themes[t], profiles[p]), shown(KT_TITLE, themes[t], profiles[p])));
        /* Text never has its own background's colour, unless both are left
         * to the terminal, which keeps its own two apart. */
        CHECK(base.fg != base.bg || base.fg == KT_DEFAULT);
        CHECK(selected.fg != selected.bg || selected.fg == KT_DEFAULT);
        CHECK(accent_pill.fg != accent_pill.bg || accent_pill.fg == KT_DEFAULT);
        /* A selection never rests on colour alone when there is none. */
        if (profiles[p] == KT_PROFILE_NONE) CHECK(selected.attrs & KT_REVERSE);
    }
    /* The terminal theme uses nothing but the terminal's sixteen colours and
     * its default background, whatever the profile. */
    for (t = 0; t < KT_TOKEN_USER; t++) {
        struct kt_style s = shown(KT_STYLE(KT_TOKEN(t), KT_TOKEN(t), 0), &kt_theme_terminal, KT_PROFILE_TRUECOLOR);
        CHECK(s.fg == KT_DEFAULT || (KT_COLOR_KIND(s.fg) == KT_KIND_INDEX && KT_COLOR_VALUE(s.fg) < 16));
    }
    CHECK(shown(KT_BASE, &kt_theme_terminal, KT_PROFILE_TRUECOLOR).bg == KT_DEFAULT);
    CHECK(!strcmp(kt_theme_dark.name, "dark"));
    CHECK(!strcmp(kt_theme_light.name, "light"));
    CHECK(!strcmp(kt_theme_terminal.name, "terminal"));
}

/* ---- KT_KEEP ------------------------------------------------------------ */

static const struct kt_cell *cell_at(const struct kt_canvas *c, int x, int y) {
    return &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
}

static void keep(void) {
    struct kt_cell cells[8 * 2];
    struct kt_canvas c;
    struct kt_style raised = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_RAISED, 0);
    CHECK(kt_init(&c, cells, 16, 8, 2, true));
    /* A fresh canvas is the base style. */
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 0, 0)), KT_BASE));
    kt_fill(&c, (struct kt_rect){0, 0, 4, 1}, raised);
    /* Ink keeps the surface it lands on, wherever that is. */
    kt_text(&c, (struct kt_rect){2, 0, 4, 1}, "abcd", KT_INK(KT_COLOR_ACCENT, KT_BOLD));
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 3, 0)), KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_RAISED, KT_BOLD)));
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 4, 0)), KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SURFACE, KT_BOLD)));
    CHECK(cell_at(&c, 3, 0)->glyph == 'b');
    CHECK(cell_at(&c, 4, 0)->glyph == 'c');
    /* Keeping the foreground changes the surface under existing ink only. */
    kt_put(&c, 3, 0, 'B', KT_STYLE(KT_KEEP, KT_COLOR_SELECTION, 0));
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 3, 0)), KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SELECTION, 0)));
    /* Both halves of a wide glyph keep what was under each of them. */
    kt_put(&c, 3, 0, 0x754cU, KT_INK(KT_COLOR_WARNING, 0));
    CHECK(cell_at(&c, 3, 0)->width == 2);
    CHECK(cell_at(&c, 4, 0)->width == 0);
    CHECK(cell_at(&c, 3, 0)->background == KT_COLOR_SELECTION);
    CHECK(cell_at(&c, 4, 0)->background == KT_COLOR_SURFACE);
    CHECK(cell_at(&c, 3, 0)->foreground == KT_COLOR_WARNING);
    CHECK(cell_at(&c, 4, 0)->foreground == KT_COLOR_WARNING);
    /* A style that names both colours replaces whatever was there. */
    kt_put(&c, 0, 0, 'x', KT_BASE);
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 0, 0)), KT_BASE));
    /* Filling with a kept foreground repaints the surface and blanks the text. */
    kt_fill(&c, (struct kt_rect){2, 0, 1, 1}, KT_STYLE(KT_KEEP, KT_COLOR_SUNKEN, 0));
    CHECK(cell_at(&c, 2, 0)->glyph == ' ');
    CHECK(cell_at(&c, 2, 0)->foreground == KT_COLOR_ACCENT);
    CHECK(cell_at(&c, 2, 0)->background == KT_COLOR_SUNKEN);
    CHECK(!cell_at(&c, 2, 0)->attributes);
    /* Restyling changes the look and leaves the text; attributes are added. */
    kt_text(&c, (struct kt_rect){0, 1, 8, 1}, "row", KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, KT_UNDERLINE));
    kt_restyle(&c, (struct kt_rect){0, 1, 8, 1}, KT_STYLE(KT_KEEP, KT_COLOR_SELECTION, KT_BOLD));
    CHECK(cell_at(&c, 1, 1)->glyph == 'o');
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 1, 1)), KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SELECTION, KT_UNDERLINE | KT_BOLD)));
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 7, 1)), KT_STYLE(KT_COLOR_TEXT, KT_COLOR_SELECTION, KT_BOLD)));
    /* With no cell to keep from, clearing treats KT_KEEP as the default. */
    kt_clear(&c, KT_STYLE(KT_KEEP, KT_COLOR_RAISED, 0));
    CHECK(kt_style_equal(kt_cell_style(cell_at(&c, 5, 1)), KT_STYLE(KT_DEFAULT, KT_COLOR_RAISED, 0)));
    /* The zero style is the terminal's default, and a style is its parts. */
    CHECK(kt_style_equal((struct kt_style){0, 0, 0}, KT_STYLE(KT_DEFAULT, KT_DEFAULT, 0)));
    CHECK(!kt_style_equal(KT_BASE, KT_STRONG));
    CHECK(!kt_style_equal(KT_BASE, KT_MUTED));
    CHECK(kt_style_equal(KT_TITLE, KT_FOCUS));
    CHECK(KT_COLOR_KIND(KT_KEEP) == KT_KIND_KEEP);
    CHECK(KT_COLOR_KIND(KT_RGB(1, 2, 3)) == KT_KIND_RGB);
    CHECK(KT_COLOR_VALUE(KT_RGB(1, 2, 3)) == 0x010203);
    CHECK(KT_COLOR_KIND(KT_INDEX(300)) == KT_KIND_INDEX);
    CHECK(KT_COLOR_VALUE(KT_INDEX(300)) == 44);
    CHECK(KT_COLOR_KIND(KT_COLOR_TEXT) == KT_KIND_TOKEN);
}

/* ---- SGR ---------------------------------------------------------------- */

static const char *sgr(const struct kt_style *from, struct kt_style to) {
    static char out[KT_SGR_MAX];
    size_t length = kt_sgr(out, from, to);
    CHECK(length == strlen(out));
    CHECK(length < KT_SGR_MAX);
    return out;
}

static void sequences(void) {
    const struct kt_style plain = {KT_DEFAULT, KT_DEFAULT, 0};
    struct kt_style a, b;
    /* An unknown rendition always starts from a reset. */
    CHECK(!strcmp(sgr(NULL, plain), "\033[0m"));
    CHECK(!strcmp(sgr(NULL, KT_STYLE(KT_INDEX(1), KT_DEFAULT, KT_BOLD)), "\033[0;1;31m"));
    CHECK(!strcmp(sgr(NULL, KT_STYLE(KT_INDEX(252), KT_INDEX(234), 0)), "\033[0;38;5;252;48;5;234m"));
    /* Nothing changes, nothing is written. */
    CHECK(!strcmp(sgr(&plain, plain), ""));
    a = KT_STYLE(KT_RGB(1, 2, 3), KT_INDEX(4), KT_BOLD | KT_UNDERLINE);
    CHECK(!strcmp(sgr(&a, a), ""));
    /* Each kind of colour, foreground and background. */
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_INDEX(2), KT_DEFAULT, 0)), "\033[32m"));
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_INDEX(9), KT_DEFAULT, 0)), "\033[91m"));
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_DEFAULT, KT_INDEX(7), 0)), "\033[47m"));
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_DEFAULT, KT_INDEX(12), 0)), "\033[104m"));
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_INDEX(16), KT_INDEX(255), 0)), "\033[38;5;16;48;5;255m"));
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_RGB(217, 220, 227), KT_RGB(22, 24, 29), 0)), "\033[38;2;217;220;227;48;2;22;24;29m"));
    /* Each attribute on, in a fixed order. */
    CHECK(!strcmp(sgr(&plain, KT_STYLE(KT_DEFAULT, KT_DEFAULT, KT_BOLD | KT_DIM | KT_ITALIC | KT_UNDERLINE | KT_REVERSE)), "\033[1;2;3;4;7m"));
    /* Only what differs: a colour, an attribute, one of two colours. */
    a = KT_STYLE(KT_INDEX(252), KT_INDEX(234), 0);
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(245), KT_INDEX(234), 0)), "\033[38;5;245m"));
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(252), KT_INDEX(234), KT_BOLD)), "\033[1m"));
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(16), KT_INDEX(117), 0)), "\033[38;5;16;48;5;117m"));
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(252), KT_DEFAULT, 0)), "\033[49m"));
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_DEFAULT, KT_INDEX(234), 0)), "\033[39m"));
    /* Attributes off. Bold and dim share one code, so the survivor is set again. */
    a = KT_STYLE(KT_INDEX(252), KT_INDEX(234), KT_BOLD);
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(252), KT_INDEX(234), 0)), "\033[22m"));
    a.attrs = KT_BOLD | KT_DIM;
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(252), KT_INDEX(234), KT_DIM)), "\033[22;2m"));
    a.attrs = KT_ITALIC | KT_UNDERLINE | KT_REVERSE;
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(252), KT_INDEX(234), KT_UNDERLINE)), "\033[23;27m"));
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(252), KT_INDEX(234), KT_ITALIC | KT_REVERSE)), "\033[24m"));
    /* Back to the default is always the bare reset. */
    CHECK(!strcmp(sgr(&a, plain), "\033[0m"));
    b = KT_STYLE(KT_INDEX(3), KT_DEFAULT, 0);
    CHECK(!strcmp(sgr(&b, plain), "\033[0m"));
    /* A reset and the target, when that is shorter than undoing piece by piece. */
    a = KT_STYLE(KT_INDEX(1), KT_INDEX(4), KT_BOLD | KT_ITALIC | KT_UNDERLINE | KT_REVERSE);
    CHECK(!strcmp(sgr(&a, KT_STYLE(KT_INDEX(2), KT_DEFAULT, 0)), "\033[0;32m"));
    /* The longest sequence there is fits its buffer. */
    a = KT_STYLE(KT_RGB(255, 255, 255), KT_RGB(255, 255, 255), KT_BOLD | KT_DIM | KT_ITALIC | KT_UNDERLINE | KT_REVERSE);
    CHECK(!strcmp(sgr(NULL, a), "\033[0;1;2;3;4;7;38;2;255;255;255;48;2;255;255;255m"));
    b = KT_STYLE(KT_RGB(254, 254, 254), KT_RGB(253, 253, 253), KT_DIM);
    CHECK(strlen(sgr(&a, b)) < KT_SGR_MAX);
    CHECK(strlen(sgr(&b, a)) < KT_SGR_MAX);
    /* With no colour to show only attributes are ever written. */
    {
        const struct kt_paint none = {&kt_theme_dark, KT_PROFILE_NONE};
        struct kt_style base = kt_style_resolve(KT_BASE, &none), title = kt_style_resolve(KT_TITLE, &none);
        struct kt_style chosen = kt_style_resolve(KT_SELECTED, &none), warning = kt_style_resolve(KT_WARNING, &none);
        CHECK(!strcmp(sgr(&plain, base), ""));
        CHECK(!strcmp(sgr(&base, warning), ""));
        CHECK(!strcmp(sgr(&base, title), "\033[1m"));
        CHECK(!strcmp(sgr(&title, base), "\033[0m"));
        CHECK(!strcmp(sgr(&base, chosen), "\033[7m"));
        CHECK(!strcmp(sgr(&title, chosen), "\033[0;7m"));
    }
}

int main(void) {
    palette();
    to_256();
    to_16();
    profiles();
    resolution();
    builtin_themes();
    keep();
    sequences();
    puts("style: palette, nearest-colour conversion, profile detection, theme resolution and fallbacks, "
         "built-in themes at every profile, KT_KEEP, minimal SGR passed");
    return 0;
}
