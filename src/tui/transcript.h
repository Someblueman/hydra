#ifndef HYDRA_TUI_TRANSCRIPT_H
#define HYDRA_TUI_TRANSCRIPT_H
#include "../termviz/termviz.h"
#include "../termviz/terminal.h"
/* Read-only transcripts of captured terminal output (tmux capture-pane -e,
 * provider streams, command logs). Each logical line is interpreted by a
 * termviz terminal model, so UTF-8 widths, joined emoji, SGR colors, CR/BS/TAB
 * and DEC graphics follow the attached pane exactly, while cursor movement,
 * OSC/DCS payloads and other control sequences never reach the outer terminal.
 * Lines wrap at the view width; each continuation row starts with a marker.
 * Unified diffs without their own colors get theme tones; with color disabled
 * their +/- markers remain the distinction. */
#define TRANSCRIPT_COLUMNS 4096
#define TRANSCRIPT_MODEL_COLUMNS 512

struct transcript {
    struct tv_terminal_model model;
    struct tv_cell primary[TRANSCRIPT_MODEL_COLUMNS * 2], alternate[TRANSCRIPT_MODEL_COLUMNS * 2];
    struct tv_cell line[TRANSCRIPT_COLUMNS];
    size_t count;
    bool truncated, diff;
    size_t diff_indent;
};

struct transcript_layout {
    /* In: first row when not following; follow shows the newest rows. */
    size_t scroll;
    bool follow, color;
    /* Out: total wrapped rows, clamped scroll, and whether provider key hints
     * (for example "esc to interrupt") appear in the text. */
    size_t rows, drawn;
    bool hints;
};

struct transcript *transcript_new(void);
void transcript_free(struct transcript *t);
/* Restores default rendition and character sets before a new document. */
void transcript_reset(struct transcript *t);
/* Interprets one logical line (no LF); returns the columns it occupies. */
size_t transcript_parse(struct transcript *t, const char *text, size_t length);
/* Wrapped rows of the last parsed line at the given width (at least 1). */
size_t transcript_rows(const struct transcript *t, int width);
/* Draws wrapped rows [first, first + height) of the last parsed line into
 * rows y.. of the area; cells without their own style take tone. */
int transcript_draw(const struct transcript *t, struct tv_canvas *c, struct tv_rect area,
                    size_t first, enum tv_style tone, bool color);
/* Lays out a whole document into area: trailing blank lines are omitted,
 * unified diffs are toned and provider hints are detected. */
void transcript_render(struct transcript *t, struct tv_canvas *c, struct tv_rect area,
                       const char *text, size_t length, struct transcript_layout *layout);
#endif
