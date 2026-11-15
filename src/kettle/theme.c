#include "style.h"

/* Built-in themes. Colours are 24-bit; a 256-colour terminal gets the nearest
 * fixed palette entry, a 16-colour one the nearest of its own sixteen. Where
 * that would lose the point of a token (a selection that ends up the colour
 * of the surface), the entry names the palette entry to use instead. `plain`
 * says how a token shows when there is no colour at all. */

const struct kt_theme kt_theme_dark = {"dark", {
    [KT_TOKEN_TEXT] = {KT_HEX(0xD9DCE3), 0, 0, 0, 0},
    [KT_TOKEN_MUTED] = {KT_HEX(0x969DAB), 0, 0, 0, KT_DIM},
    [KT_TOKEN_FAINT] = {KT_HEX(0x6B7382), 0, KT_INDEX(8), 0, KT_DIM},
    [KT_TOKEN_SURFACE] = {KT_HEX(0x16181D), 0, KT_INDEX(0), 0, 0},
    [KT_TOKEN_RAISED] = {KT_HEX(0x1F232B), 0, KT_INDEX(0), 0, 0},
    [KT_TOKEN_SUNKEN] = {KT_HEX(0x101216), 0, KT_INDEX(0), 0, 0},
    [KT_TOKEN_SELECTION] = {KT_HEX(0x2B3550), KT_INDEX(24), KT_INDEX(4), 0, KT_REVERSE},
    [KT_TOKEN_ON_SELECTION] = {KT_HEX(0xD9DCE3), KT_INDEX(255), KT_INDEX(15), 0, 0},
    [KT_TOKEN_BORDER] = {KT_HEX(0x3A4150), 0, KT_INDEX(8), 0, 0},
    [KT_TOKEN_ACCENT] = {KT_HEX(0x7AA2F7), 0, KT_INDEX(12), 0, 0},
    [KT_TOKEN_ON_ACCENT] = {KT_HEX(0x0E1423), 0, KT_INDEX(0), 0, 0},
    [KT_TOKEN_SUCCESS] = {KT_HEX(0x73D0A0), 0, KT_INDEX(10), 0, 0},
    [KT_TOKEN_WARNING] = {KT_HEX(0xE8B95B), 0, KT_INDEX(11), 0, 0},
    [KT_TOKEN_DANGER] = {KT_HEX(0xF47C7C), 0, KT_INDEX(9), 0, 0},
    [KT_TOKEN_INFO] = {KT_HEX(0x6FC3F0), 0, KT_INDEX(14), 0, 0}
}};

const struct kt_theme kt_theme_light = {"light", {
    [KT_TOKEN_TEXT] = {KT_HEX(0x1F2430), 0, KT_INDEX(0), 0, 0},
    [KT_TOKEN_MUTED] = {KT_HEX(0x596170), 0, KT_INDEX(8), 0, KT_DIM},
    [KT_TOKEN_FAINT] = {KT_HEX(0x808896), 0, KT_INDEX(8), 0, KT_DIM},
    [KT_TOKEN_SURFACE] = {KT_HEX(0xFBFBFC), 0, KT_INDEX(15), 0, 0},
    [KT_TOKEN_RAISED] = {KT_HEX(0xF1F2F5), 0, KT_INDEX(15), 0, 0},
    [KT_TOKEN_SUNKEN] = {KT_HEX(0xE8EAEE), 0, KT_INDEX(7), 0, 0},
    [KT_TOKEN_SELECTION] = {KT_HEX(0xDCE6FF), KT_INDEX(153), KT_INDEX(4), 0, KT_REVERSE},
    [KT_TOKEN_ON_SELECTION] = {KT_HEX(0x1F2430), KT_INDEX(16), KT_INDEX(15), 0, 0},
    [KT_TOKEN_BORDER] = {KT_HEX(0xD3D7DE), 0, KT_INDEX(7), 0, 0},
    [KT_TOKEN_ACCENT] = {KT_HEX(0x2F55D4), 0, KT_INDEX(4), 0, 0},
    [KT_TOKEN_ON_ACCENT] = {KT_HEX(0xFFFFFF), 0, KT_INDEX(15), 0, 0},
    [KT_TOKEN_SUCCESS] = {KT_HEX(0x126B3A), 0, KT_INDEX(2), 0, 0},
    [KT_TOKEN_WARNING] = {KT_HEX(0x805400), 0, KT_INDEX(3), 0, 0},
    [KT_TOKEN_DANGER] = {KT_HEX(0xB4232F), 0, KT_INDEX(1), 0, 0},
    [KT_TOKEN_INFO] = {KT_HEX(0x0F5E91), 0, KT_INDEX(6), 0, 0}
}};

/* Only the terminal's own sixteen colours, on its own background: whatever
 * palette the user chose stays in charge. Muted text is dim and the
 * selection is reverse video, which every palette can show. */
const struct kt_theme kt_theme_terminal = {"terminal", {
    [KT_TOKEN_MUTED] = {KT_DEFAULT, 0, 0, KT_DIM, 0},
    [KT_TOKEN_FAINT] = {KT_DEFAULT, 0, 0, KT_DIM, 0},
    [KT_TOKEN_SELECTION] = {KT_DEFAULT, 0, 0, KT_REVERSE, 0},
    [KT_TOKEN_BORDER] = {KT_INDEX(6), 0, 0, 0, 0},
    [KT_TOKEN_ACCENT] = {KT_INDEX(6), 0, 0, 0, 0},
    [KT_TOKEN_ON_ACCENT] = {KT_INDEX(0), 0, 0, 0, 0},
    [KT_TOKEN_SUCCESS] = {KT_INDEX(2), 0, 0, 0, 0},
    [KT_TOKEN_WARNING] = {KT_INDEX(3), 0, 0, 0, 0},
    [KT_TOKEN_DANGER] = {KT_INDEX(1), 0, 0, 0, 0},
    [KT_TOKEN_INFO] = {KT_INDEX(4), 0, 0, 0, 0}
}};
