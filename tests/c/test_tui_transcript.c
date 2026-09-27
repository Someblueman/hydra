/* Faithful read-only output: real provider bytes recorded from Claude Code
 * 2.1.283 and Codex CLI 0.157.1 (tests/fixtures/providers/README.md) are
 * rendered through Hydra's transcript at 80x24 and 140x40 and through the
 * attached-pane terminal model, then compared with an independent stripper
 * and with tmux 3.5a's own screen for the same bytes. */
#include "tui/transcript.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURES "tests/fixtures/providers/"
#define MARK 0x21aaU
static struct tv_cell frame[140 * 400], tall_cells[140 * 400];

struct document { char *data, *plain; size_t length; };

static char *slurp(const char *name, size_t *length) {
    FILE *f = fopen(name, "rb");
    char *data;
    long size;
    assert(f && !fseek(f, 0, SEEK_END) && (size = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET));
    data = malloc((size_t)size + 1);
    assert(data && fread(data, 1, (size_t)size, f) == (size_t)size);
    data[size] = '\0';
    fclose(f);
    *length = (size_t)size;
    return data;
}

/* End of a CSI or string sequence starting at in[i] (ESC), or i if none. */
static size_t skip_sequence(const char *in, size_t length, size_t i) {
    size_t j = i + 2;
    if (i + 1 >= length || in[i] != 27) return i;
    if (in[i + 1] == '[') {
        while (j < length && ((unsigned char)in[j] < 0x40 || (unsigned char)in[j] > 0x7e)) j++;
        return j + 1;
    }
    if (in[i + 1] != ']') return i;
    while (j < length && in[j] != 7 && in[j] != 27) j++;
    return j < length && in[j] == 27 ? j + 2 : j + 1;
}

/* Independent reference: drops CSI and string sequences, keeps UTF-8 text. */
static void load(struct document *d, const char *name) {
    size_t i = 0, used = 0;
    d->data = slurp(name, &d->length);
    d->plain = malloc(d->length + 1);
    assert(d->plain);
    while (i < d->length) {
        size_t next = skip_sequence(d->data, d->length, i);
        if (next != i) i = next;
        else d->plain[used++] = d->data[i++];
    }
    d->plain[used] = '\0';
}

static void unload(struct document *d) { free(d->data); free(d->plain); }

static const struct tv_cell *at(const struct tv_canvas *c, int x, int y) {
    return &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
}

static size_t encode(const struct tv_cell *cell, char *out) {
    size_t used = tv_utf8_encode(cell->glyph, out), i;
    for (i = 0; i < cell->combining_count; i++) used += tv_utf8_encode(cell->combining[i], out + used);
    return used;
}

/* Row text, skipping wide continuations; trailing blanks trimmed if asked. */
static void row_cells(const struct tv_canvas *c, int y, int from, char *out, size_t size, bool trim) {
    size_t used = 0, keep = 0;
    int x;
    for (x = from; x < c->width && used + 24 < size; x++) {
        if (!at(c, x, y)->width) continue;
        used += encode(at(c, x, y), out + used);
        if (at(c, x, y)->glyph != ' ' || !trim) keep = used;
    }
    out[keep] = '\0';
}

static void row_text(const struct tv_canvas *c, int y, char *out, size_t size) { row_cells(c, y, 0, out, size, true); }

static void trim_right(char *line) {
    size_t n = strlen(line);
    while (n && line[n - 1] == ' ') line[--n] = '\0';
}

/* Logical lines reassembled from wrapped rows, markers removed. */
static char *joined_rows(const struct tv_canvas *c, int rows) {
    char *joined = calloc(1, 65536), text[4096];
    size_t used = 0;
    int y;
    assert(joined);
    for (y = 0; y < rows; y++) {
        bool continuation = at(c, 0, y)->glyph == MARK;
        bool continued = y + 1 < rows && at(c, 0, y + 1)->glyph == MARK;
        row_cells(c, y, continuation ? 1 : 0, text, sizeof(text), !continued);
        if (y && !continuation) joined[used++] = '\n';
        memcpy(joined + used, text, strlen(text));
        used += strlen(text);
    }
    return joined;
}

/* Next nonblank line (trailing blanks trimmed) from a strtok_r stream. */
static char *next_line(char *start, char **save) {
    char *line = strtok_r(start, "\n", save);
    while (line) {
        trim_right(line);
        if (*line) return line;
        line = strtok_r(NULL, "\n", save);
    }
    return NULL;
}

/* Nothing lost, reordered or corrupted by wrapping. */
static void same_content(const struct tv_canvas *c, int rows, const char *expected) {
    char *joined = joined_rows(c, rows), *copy = strdup(expected), *want_save = NULL, *got_save = NULL;
    char *want = next_line(copy, &want_save), *got = next_line(joined, &got_save);
    assert(copy);
    for (; want; want = next_line(NULL, &want_save), got = next_line(NULL, &got_save)) {
        if (!got || strcmp(got, want)) fprintf(stderr, "expected [%s]\n     got [%s]\n", want, got ? got : "");
        assert(got && !strcmp(got, want));
    }
    free(joined); free(copy);
}

/* Cell column of byte offset prefix in row y. */
static int column_of(const struct tv_canvas *c, int y, size_t prefix) {
    size_t bytes = 0;
    int x = 0;
    char tmp[32];
    for (; bytes < prefix; x++) if (at(c, x, y)->width) bytes += encode(at(c, x, y), tmp);
    while (!at(c, x, y)->width) x++;
    return x;
}

static int find(const struct tv_canvas *c, int rows, const char *needle, int *column) {
    char text[4096], *hit;
    int y;
    for (y = 0; y < rows; y++) {
        row_text(c, y, text, sizeof(text));
        if (!(hit = strstr(text, needle))) continue;
        *column = column_of(c, y, (size_t)(hit - text));
        return y;
    }
    return -1;
}

static void render(struct transcript *t, struct tv_canvas *c, int width, int height, const struct document *d,
                   bool color, struct transcript_layout *layout) {
    assert(tv_init(c, frame, sizeof(frame) / sizeof(*frame), width, height, true));
    memset(layout, 0, sizeof(*layout));
    layout->color = color;
    transcript_render(t, c, (struct tv_rect){0, 0, width, height}, d->data, d->length, layout);
}

static size_t count_glyph(const struct tv_canvas *c, int rows, uint32_t glyph) {
    size_t n = 0;
    int x, y;
    for (y = 0; y < rows; y++) for (x = 0; x < c->width; x++) n += at(c, x, y)->glyph == glyph;
    return n;
}

static const struct tv_cell *cell_of(const struct tv_canvas *c, int rows, const char *needle, int offset) {
    int x = 0, y = find(c, rows, needle, &x);
    if (y < 0) fprintf(stderr, "missing [%s]\n", needle);
    assert(y >= 0);
    return at(c, x + offset, y);
}

/* Claude Code's own colored diff keeps its SGR; wide/emoji cells stay intact. */
static void claude_width(struct transcript *t, const struct document *d, int width) {
    struct transcript_layout layout;
    struct tv_canvas c;
    int rows, x = 0, y;
    render(t, &c, width, 400, d, true, &layout);
    rows = (int)layout.rows;
    assert(rows < 400 && layout.hints); /* "← for agents" is a provider key hint */
    same_content(&c, rows, d->plain);
    assert(count_glyph(&c, rows, 27) == 0 && count_glyph(&c, rows, '?') == 0);
    assert(cell_of(&c, rows, "-w\xc3\xb6rld", 0)->foreground == 0xcd3131);
    assert(cell_of(&c, rows, "+world \xe2\x9c\x93", 0)->foreground == 0x0dbc79);
    assert(cell_of(&c, rows, "\xe7\x95\x8c\xe9\x9d\xa2", 0)->width == 2);
    assert(!cell_of(&c, rows, "\xe7\x95\x8c\xe9\x9d\xa2", 1)->width);
    assert(cell_of(&c, rows, "\xe7\x95\x8c\xe9\x9d\xa2", 2)->glyph == 0x9762);
    y = find(&c, rows, "LONGLINE:", &x);
    assert(y >= 0 && (width > 100 || at(&c, 0, y + 1)->glyph == MARK));
    assert(find(&c, rows, "/rc", &x) >= 0 && find(&c, rows, "8;id", &x) < 0);
}

/* NO_COLOR: explicit colors are dropped; text and +/- markers remain. */
static void claude_no_color(struct transcript *t, const struct document *d) {
    struct transcript_layout layout;
    struct tv_canvas c;
    const struct tv_cell *removed;
    render(t, &c, 80, 400, d, false, &layout);
    removed = cell_of(&c, (int)layout.rows, "-w\xc3\xb6rld", 0);
    assert(removed->foreground == TV_COLOR_DEFAULT && !(removed->attributes & TV_EXPLICIT));
}

/* An 80x24 or 140x40 viewport following the newest rows shows exactly the
 * last rows of the full layout. */
static void viewport(struct transcript *t, const struct document *d, int w, int h) {
    struct tv_canvas c, tall;
    struct transcript_layout layout, full;
    char want[4096], got[4096];
    int row;
    assert(tv_init(&tall, tall_cells, sizeof(tall_cells) / sizeof(*tall_cells), w, 400, true));
    render(t, &tall, w, 400, d, true, &full);
    assert(tv_init(&tall, tall_cells, sizeof(tall_cells) / sizeof(*tall_cells), w, 400, true));
    transcript_render(t, &tall, (struct tv_rect){0, 0, w, 400}, d->data, d->length, &full);
    render(t, &c, w, h, d, true, &layout);
    layout.follow = true;
    transcript_render(t, &c, (struct tv_rect){0, 0, w, h}, d->data, d->length, &layout);
    assert(layout.drawn == (size_t)h && layout.scroll == full.rows - (size_t)h);
    for (row = 0; row < h; row++) {
        row_text(&c, row, got, sizeof(got));
        row_text(&tall, (int)layout.scroll + row, want, sizeof(want));
        assert(!strcmp(got, want));
    }
}

static void claude_capture(struct transcript *t) {
    struct document d;
    load(&d, FIXTURES "claude-capture.ansi");
    claude_width(t, &d, 80);
    claude_width(t, &d, 140);
    claude_width(t, &d, 80); /* resize back: no state carried between layouts */
    claude_no_color(t, &d);
    viewport(t, &d, 80, 24);
    viewport(t, &d, 140, 40);
    unload(&d);
}

/* Codex prints its diff uncolored: Hydra tones it by the theme instead. */
static void codex_width(struct transcript *t, const struct document *d, int width) {
    struct transcript_layout layout;
    struct tv_canvas c;
    int rows, x = 0, y;
    render(t, &c, width, 400, d, true, &layout);
    rows = (int)layout.rows;
    same_content(&c, rows, d->plain);
    assert(!layout.hints && count_glyph(&c, rows, '?') == 0);
    assert(cell_of(&c, rows, "-w\xc3\xb6rld", 0)->style == TV_WARNING && !cell_of(&c, rows, "-w\xc3\xb6rld", 0)->attributes);
    assert(cell_of(&c, rows, "+world", 0)->style == TV_SUCCESS);
    assert(cell_of(&c, rows, "@@ -1,3 +1,3 @@", 0)->style == TV_BORDER);
    assert(cell_of(&c, rows, "--- a/greeting.txt", 0)->style == TV_STRONG);
    assert(cell_of(&c, rows, " hello", 1)->style == TV_BASE);
    assert((cell_of(&c, rows, "workdir:", 0)->attributes & TV_BOLD) && cell_of(&c, rows, "workdir:", 0)->style == TV_BASE);
    y = find(&c, rows, "LONGLINE:", &x);
    assert(y >= 0 && at(&c, 0, y + 1)->glyph == MARK);
}

static void codex_capture(struct transcript *t) {
    struct document d, raw;
    struct transcript_layout layout;
    struct tv_canvas c;
    load(&d, FIXTURES "codex-capture.ansi");
    codex_width(t, &d, 80);
    codex_width(t, &d, 140);
    viewport(t, &d, 80, 24);
    viewport(t, &d, 140, 40);
    /* The provider's own CRLF byte stream reads the same as tmux's capture. */
    load(&raw, FIXTURES "codex-exec.raw");
    render(t, &c, 80, 400, &raw, true, &layout);
    assert(strstr(d.plain, "CODEX_EXIT=")); /* printed by the recording wrapper after codex */
    *strstr(d.plain, "CODEX_EXIT=") = '\0';
    same_content(&c, (int)layout.rows, d.plain);
    unload(&raw);
    unload(&d);
}

/* Row y of the reference screen (empty rows included), trimmed. */
static char *reference_row(char *line, char *want, size_t size) {
    char *end = line ? strchr(line, '\n') : NULL;
    want[0] = '\0';
    if (!line) return NULL;
    snprintf(want, size, "%.*s", (int)(end ? end - line : (long)strlen(line)), line);
    trim_right(want);
    return end ? end + 1 : NULL;
}

/* The attached pane's terminal model reproduces tmux 3.5a's screen for the
 * raw interactive Claude Code stream (cursor addressing, kitty/xterm keyboard
 * negotiation, OSC titles and hyperlinks, synchronized output). */
static void claude_raw_rows(const struct tv_canvas *screen) {
    size_t length;
    char *text = slurp(FIXTURES "claude-interactive.screen", &length), *line = text, want[4096], got[4096];
    int y;
    for (y = 0; y < 40; y++) {
        line = reference_row(line, want, sizeof(want));
        row_text(screen, y, got, sizeof(got));
        if (strcmp(got, want)) fprintf(stderr, "row %d\n want [%s]\n  got [%s]\n", y, want, got);
        assert(!strcmp(got, want));
    }
    free(text);
}

/* tmux's colored capture of the same screen gives the expected colors. */
static void claude_raw_colors(const struct tv_canvas *screen) {
    static struct tv_cell cells[100 * 400];
    struct transcript *t = transcript_new();
    struct transcript_layout layout;
    struct tv_canvas reference;
    struct document d;
    assert(t && tv_init(&reference, cells, sizeof(cells) / sizeof(*cells), 100, 400, true));
    load(&d, FIXTURES "claude-interactive.screen.ansi");
    memset(&layout, 0, sizeof(layout)); layout.color = true;
    transcript_render(t, &reference, (struct tv_rect){0, 0, 100, 400}, d.data, d.length, &layout);
    assert(cell_of(screen, 40, "-w\xc3\xb6rld", 0)->foreground == 0xcd3131);
    assert(cell_of(&reference, (int)layout.rows, "-w\xc3\xb6rld", 0)->foreground == 0xcd3131);
    assert(cell_of(screen, 40, "+world", 0)->foreground == cell_of(&reference, (int)layout.rows, "+world", 0)->foreground);
    assert(cell_of(screen, 40, "Baked for", 0)->foreground == cell_of(&reference, (int)layout.rows, "Baked for", 0)->foreground);
    transcript_free(t);
    unload(&d);
}

static void claude_raw_screen(void) {
    static struct tv_cell primary[512 * 40], alternate[512 * 40], history[512 * 64], out[100 * 40];
    struct tv_terminal_model model;
    struct tv_canvas screen;
    struct document d;
    load(&d, FIXTURES "claude-interactive.raw");
    assert(tv_term_init(&model, primary, alternate, 512, 40, history, 64, 100, 40));
    tv_term_feed(&model, d.data, d.length);
    tv_term_finish(&model);
    assert(tv_init(&screen, out, 100 * 40, 100, 40, true));
    tv_term_draw(&model, &screen, 0, false);
    claude_raw_rows(&screen);
    claude_raw_colors(&screen);
    unload(&d);
}

/* Destructive and leaking sequences never survive into a transcript. */
static void hostile(struct transcript *t) {
    const char *text = "keep \033[2J this\033]52;c;c2VjcmV0\007 line\033[H\033[31m red\033[0m\r\n"
                       "progress 10%\r\033[Kdone\ttab\n\033P+q\033\\tail\033(0lqk\033(B";
    struct document d = {(char *)text, NULL, strlen(text)};
    struct transcript_layout layout;
    struct tv_canvas c;
    char row[256];
    render(t, &c, 40, 10, &d, true, &layout);
    row_text(&c, 0, row, sizeof(row));
    assert(!strcmp(row, "keep  this line red") && layout.rows == 3);
    row_text(&c, 1, row, sizeof(row));
    assert(!strcmp(row, "done    tab"));
    row_text(&c, 2, row, sizeof(row));
    assert(!strcmp(row, "tail\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90"));
    /* ASCII surfaces get approximations in the same columns. */
    assert(tv_init(&c, frame, sizeof(frame) / sizeof(*frame), 40, 10, false));
    memset(&layout, 0, sizeof(layout));
    transcript_render(t, &c, (struct tv_rect){0, 0, 40, 10}, "\xe2\x94\x8c\xe7\x95\x8c\xe2\x86\x92x", 10, &layout);
    row_text(&c, 0, row, sizeof(row));
    assert(!strcmp(row, "+? >x"));
}

int main(void) {
    struct transcript *t = transcript_new();
    assert(t);
    claude_capture(t);
    codex_capture(t);
    claude_raw_screen();
    hostile(t);
    transcript_free(t);
    puts("transcript: real Claude/Codex output at 80x24 and 140x40, colors, diffs, wrapping, tmux-equal raw screen passed");
    return 0;
}
