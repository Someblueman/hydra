#ifndef KT_STYLE_H
#define KT_STYLE_H
/* Colours, styles, themes and colour profiles.
 *
 * A cell's look is a style: a foreground, a background and attributes. A
 * colour is a reference that is resolved when a frame is presented: against
 * the theme in effect, then to what the terminal can show. Cells therefore
 * never hold escape sequences, and a theme or profile change needs no redraw
 * by the application.
 *
 * Portable and allocation-free, like the rest of the core. Included by
 * kettle.h. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- colours ------------------------------------------------------------ */

/* One of: the terminal's own colour (KT_DEFAULT, which is zero), a 24-bit
 * value, an ANSI palette index, or a theme token. KT_KEEP is only for
 * drawing: it leaves the colour a cell already has, so text can go over a
 * filled surface without naming the surface again. */
typedef uint32_t kt_color;
enum kt_color_kind { KT_KIND_DEFAULT, KT_KIND_RGB, KT_KIND_INDEX, KT_KIND_TOKEN, KT_KIND_KEEP };
#define KT_DEFAULT ((kt_color)0)
#define KT_HEX(rgb) ((kt_color)(UINT32_C(0x01000000) | ((uint32_t)(rgb) & UINT32_C(0xffffff))))
#define KT_RGB(r, g, b) KT_HEX(((uint32_t)(r) & 255U) << 16 | ((uint32_t)(g) & 255U) << 8 | ((uint32_t)(b) & 255U))
/* Palette entry 0-255, for content that arrived as one. Entries 0-15 are the
 * terminal's own sixteen colours and are never replaced by a fixed value. */
#define KT_INDEX(n) ((kt_color)(UINT32_C(0x02000000) | ((uint32_t)(n) & 255U)))
#define KT_TOKEN(token) ((kt_color)(UINT32_C(0x03000000) | ((uint32_t)(token) & 255U)))
#define KT_KEEP ((kt_color)UINT32_C(0x04000000))
#define KT_COLOR_KIND(color) ((enum kt_color_kind)((kt_color)(color) >> 24))
/* The 0xRRGGBB value, palette index or token number inside a colour. */
#define KT_COLOR_VALUE(color) ((kt_color)(color) & UINT32_C(0xffffff))

/* ---- styles ------------------------------------------------------------- */

enum kt_attributes { KT_BOLD = 1, KT_DIM = 2, KT_ITALIC = 4, KT_UNDERLINE = 8, KT_REVERSE = 16 };

/* A value: build one where it is used, pass it to any drawing function. The
 * zero style is the terminal's default colours with no attributes. */
struct kt_style { kt_color fg, bg; unsigned attrs; };
#define KT_STYLE(fg, bg, attrs) ((struct kt_style){(fg), (bg), (unsigned)(attrs)})
/* Text over whatever surface is already there. */
#define KT_INK(fg, attrs) KT_STYLE((fg), KT_KEEP, (attrs))
bool kt_style_equal(struct kt_style a, struct kt_style b);

/* ---- themes ------------------------------------------------------------- */

/* The semantic vocabulary. An application names what a thing is; the theme
 * says what that looks like. Applications number their own tokens from
 * KT_TOKEN_USER and supply a theme that defines them. */
enum kt_token {
    KT_TOKEN_TEXT,          /* body text                                      */
    KT_TOKEN_MUTED,         /* secondary text: labels, hints                  */
    KT_TOKEN_FAINT,         /* tertiary text: rules, placeholders, disabled   */
    KT_TOKEN_SURFACE,       /* the application's background                   */
    KT_TOKEN_RAISED,        /* a panel that sits above the surface            */
    KT_TOKEN_SUNKEN,        /* a well below it: inputs, code                  */
    KT_TOKEN_SELECTION,     /* background of the selected item                */
    KT_TOKEN_ON_SELECTION,  /* text on it                                     */
    KT_TOKEN_BORDER,        /* box and divider lines                          */
    KT_TOKEN_ACCENT,        /* titles, focus, the one thing to look at        */
    KT_TOKEN_ON_ACCENT,     /* text on an accent background                   */
    KT_TOKEN_SUCCESS,
    KT_TOKEN_WARNING,
    KT_TOKEN_DANGER,
    KT_TOKEN_INFO,
    KT_TOKEN_USER = 16,
    KT_TOKEN_COUNT = 32
};
#define KT_COLOR_TEXT KT_TOKEN(KT_TOKEN_TEXT)
#define KT_COLOR_MUTED KT_TOKEN(KT_TOKEN_MUTED)
#define KT_COLOR_FAINT KT_TOKEN(KT_TOKEN_FAINT)
#define KT_COLOR_SURFACE KT_TOKEN(KT_TOKEN_SURFACE)
#define KT_COLOR_RAISED KT_TOKEN(KT_TOKEN_RAISED)
#define KT_COLOR_SUNKEN KT_TOKEN(KT_TOKEN_SUNKEN)
#define KT_COLOR_SELECTION KT_TOKEN(KT_TOKEN_SELECTION)
#define KT_COLOR_ON_SELECTION KT_TOKEN(KT_TOKEN_ON_SELECTION)
#define KT_COLOR_BORDER KT_TOKEN(KT_TOKEN_BORDER)
#define KT_COLOR_ACCENT KT_TOKEN(KT_TOKEN_ACCENT)
#define KT_COLOR_ON_ACCENT KT_TOKEN(KT_TOKEN_ON_ACCENT)
#define KT_COLOR_SUCCESS KT_TOKEN(KT_TOKEN_SUCCESS)
#define KT_COLOR_WARNING KT_TOKEN(KT_TOKEN_WARNING)
#define KT_COLOR_DANGER KT_TOKEN(KT_TOKEN_DANGER)
#define KT_COLOR_INFO KT_TOKEN(KT_TOKEN_INFO)

/* What one token means. `color` is KT_DEFAULT, KT_HEX/KT_RGB or KT_INDEX.
 * `ansi256` and `ansi16` are optional KT_INDEX values used at those profiles
 * where the automatic conversion of `color` would pick badly; zero converts.
 * `attrs` are attributes a cell gains wherever it uses the token, for themes
 * that say "muted" with dim or "selected" with reverse instead of a colour.
 * `plain` are attributes it gains only when no colour is shown, so that
 * meaning carried by a colour alone survives a monochrome terminal. */
struct kt_theme_color { kt_color color, ansi256, ansi16; unsigned char attrs, plain; };
/* Themes are data: declare one `static const` and name the tokens it sets
 * with designated initializers. A token it leaves out is the terminal's
 * default colour. The runtime keeps the pointer; it must outlive the program. */
struct kt_theme { const char *name; struct kt_theme_color tokens[KT_TOKEN_COUNT]; };
/* 24-bit palettes for a dark and a light background, and one that uses only
 * the terminal's own sixteen colours and default background. */
extern const struct kt_theme kt_theme_dark, kt_theme_light, kt_theme_terminal;

/* Stock styles in terms of tokens, used by the widgets in this library and
 * free for applications. Each sets foreground, background and attributes, so
 * drawing one over anything replaces it completely. */
#define KT_BASE KT_STYLE(KT_COLOR_TEXT, KT_COLOR_SURFACE, 0)
#define KT_BORDER KT_STYLE(KT_COLOR_BORDER, KT_COLOR_SURFACE, 0)
#define KT_TITLE KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SURFACE, KT_BOLD)
#define KT_SELECTED KT_STYLE(KT_COLOR_ON_SELECTION, KT_COLOR_SELECTION, 0)
#define KT_WARNING KT_STYLE(KT_COLOR_WARNING, KT_COLOR_SURFACE, 0)
#define KT_STRONG KT_STYLE(KT_COLOR_TEXT, KT_COLOR_SURFACE, KT_BOLD)
#define KT_SUCCESS KT_STYLE(KT_COLOR_SUCCESS, KT_COLOR_SURFACE, 0)
#define KT_MUTED KT_STYLE(KT_COLOR_MUTED, KT_COLOR_SURFACE, 0)
#define KT_FOCUS KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SURFACE, KT_BOLD)

/* ---- colour profiles ---------------------------------------------------- */

/* What the terminal can show. NONE shows no colour; attributes still go out. */
enum kt_profile { KT_PROFILE_AUTO, KT_PROFILE_NONE, KT_PROFILE_16, KT_PROFILE_256, KT_PROFILE_TRUECOLOR };

/* The profile an environment asks for. `lookup` returns a variable's value or
 * NULL; `terminal` says whether output goes to a terminal. Never returns AUTO.
 *   - NO_COLOR set and not empty: NONE, whatever else is set
 *   - output that is not a terminal, no TERM, or TERM=dumb: NONE, unless
 *     CLICOLOR_FORCE asks for colour anyway (then at least 16), or CLICOLOR
 *     vouches for a terminal that sets no TERM at all (16)
 *   - COLORTERM=truecolor or 24bit, or a TERM known for it (alacritty, foot,
 *     ghostty, kitty, wezterm, ..., *-direct): TRUECOLOR
 *   - TERM ending in 256color, or tmux/screen: 256; any other TERM: 16
 *   - inside GNU screen COLORTERM is ignored, because screen does not pass
 *     24-bit colour on. Inside tmux it is honoured: tmux understands 24-bit
 *     colour and converts it itself for an outer terminal that lacks it. */
enum kt_profile kt_profile_detect(const char *(*lookup)(void *context, const char *name),
                                  void *context, bool terminal);

/* ---- conversion --------------------------------------------------------- */

/* The xterm palette: 0xRRGGBB of entry 0-255. Entries 0-15 are xterm's
 * defaults, a stand-in for colours every terminal defines for itself. */
uint32_t kt_palette_rgb(unsigned index);
/* The nearest entry of the 240 fixed ones (16-255: the 6x6x6 cube and the
 * grey ramp), and of the sixteen, to a 0xRRGGBB value. Nearest by a weighted
 * distance that follows the eye's sensitivity (the "redmean" approximation),
 * not by cutting channels down. */
unsigned kt_rgb_to_256(uint32_t rgb);
unsigned kt_rgb_to_16(uint32_t rgb);

/* How frames are painted: against which theme, for which terminal. A NULL
 * theme is kt_theme_terminal; KT_PROFILE_AUTO is treated as NONE. */
struct kt_paint { const struct kt_theme *theme; enum kt_profile profile; };

/* What the terminal is told for a style: tokens looked up in the theme (their
 * attributes added), colours converted to the profile. The result's fg and bg
 * are KT_DEFAULT, KT_INDEX or, at TRUECOLOR only, an RGB value. KT_KEEP and
 * tokens outside the theme resolve to KT_DEFAULT. */
struct kt_style kt_style_resolve(struct kt_style style, const struct kt_paint *paint);

/* Writes the SGR sequence that changes the terminal's rendition from one
 * resolved style to another: only what differs, or a reset followed by the
 * target when that is shorter. `from` NULL means the rendition is unknown and
 * always starts from a reset. Writes nothing when nothing differs. Returns
 * the length; `out` holds at least KT_SGR_MAX bytes and is NUL-terminated. */
#define KT_SGR_MAX 64
size_t kt_sgr(char out[KT_SGR_MAX], const struct kt_style *from, struct kt_style to);

#endif
