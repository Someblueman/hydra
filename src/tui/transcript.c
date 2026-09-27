#define _POSIX_C_SOURCE 200809L
#include "transcript.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
/* See transcript.h. The model is two rows of 512 columns without history:
 * a line longer than one row wraps into the second row, which is moved into
 * the logical line buffer, so only TRANSCRIPT_COLUMNS bounds a line. */

struct transcript *transcript_new(void) {
    struct transcript *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    if (!tv_term_init(&t->model, t->primary, t->alternate, TRANSCRIPT_MODEL_COLUMNS, 2, NULL, 0,
                      TRANSCRIPT_MODEL_COLUMNS, 2)) { free(t); return NULL; }
    return t;
}

void transcript_free(struct transcript *t) { free(t); }

void transcript_reset(struct transcript *t) {
    tv_term_reset(&t->model);
    t->diff = false; t->diff_indent = 0; t->count = 0; t->truncated = false;
}

static struct tv_screen *active(struct transcript *t) {
    return t->model.alternate_active ? &t->model.alternate : &t->model.primary;
}

static void blank_rows(struct tv_screen *s, int first, int count) {
    struct tv_canvas rows;
    if (tv_canvas_view(&rows, &s->canvas, (struct tv_rect){0, first, s->canvas.width, count})) tv_clear(&rows, TV_BASE);
}

/* Moves a filled first row into the logical line and scrolls the second up. */
static void flush_row(struct transcript *t, struct tv_screen *s, size_t *offset) {
    size_t take = TRANSCRIPT_MODEL_COLUMNS;
    if (*offset + take > TRANSCRIPT_COLUMNS) { take = TRANSCRIPT_COLUMNS - *offset; t->truncated = true; }
    memcpy(t->line + *offset, s->canvas.cells, take * sizeof(*t->line));
    *offset += take;
    memmove(s->canvas.cells, s->canvas.cells + s->canvas.stride, TRANSCRIPT_MODEL_COLUMNS * sizeof(*t->line));
    blank_rows(s, 1, 1);
    s->y = 0;
}

/* Trailing spaces are padding even with a background (a provider's shaded
 * input box): wrapping them would add rows of blank color. */
static bool content(const struct tv_cell *cell) {
    return cell->glyph != ' ' || cell->width != 1 || cell->combining_count ||
        (cell->attributes & (TV_REVERSE | TV_UNDERLINE));
}

/* Bytes in an escape string (OSC, DCS, APC, PM, SOS) through its terminator. */
static size_t string_length(const char *text, size_t length) {
    size_t i;
    for (i = 2; i < length; i++) {
        if (text[i] == 7) return i + 1;
        if (text[i] == 27 && i + 1 < length && text[i + 1] == '\\') return i + 2;
    }
    return length;
}

/* Length of the escape sequence at text (text[0] is ESC). A transcript keeps
 * rendition (SGR), in-line editing (EL, CHA, CUF, CUB) and character-set
 * designation; screen erasure, vertical movement, resets and strings would
 * damage or leak captured text, so they are dropped. */
static size_t escape_length(const char *text, size_t length, bool *keep) {
    size_t i = 2;
    *keep = false;
    if (length < 2) return length;
    if (!text[1]) return 2;
    if (strchr("]P_^X", text[1])) return string_length(text, length);
    if (strchr("()*+", text[1])) { *keep = length >= 3; return length >= 3 ? 3 : length; }
    if (text[1] != '[') return 2;
    while (i < length && (unsigned char)text[i] >= 0x20 && (unsigned char)text[i] <= 0x3f) i++;
    if (i >= length) return length;
    *keep = text[i] && strchr("mKGCD", text[i]) != NULL;
    return i + 1;
}

/* Feeds one line through the model; returns columns already moved into the
 * logical line by wrapping. */
static size_t feed_line(struct transcript *t, const char *text, size_t length) {
    size_t i, offset = 0;
    for (i = 0; i < length; i++) {
        struct tv_screen *s;
        if (text[i] == 27) {
            bool keep;
            size_t n = escape_length(text + i, length - i, &keep);
            if (keep) tv_term_feed(&t->model, text + i, n);
            i += n - 1;
            continue;
        }
        if (text[i] == '\v' || text[i] == '\f') continue;
        tv_term_feed(&t->model, text + i, 1);
        s = active(t);
        if (!s->y) continue;
        if (offset >= TRANSCRIPT_COLUMNS) { t->truncated = true; break; }
        flush_row(t, s, &offset);
    }
    return offset;
}

size_t transcript_parse(struct transcript *t, const char *text, size_t length) {
    struct tv_screen *s;
    size_t offset, used;
    blank_rows(&t->model.primary, 0, 2); blank_rows(&t->model.alternate, 0, 2);
    t->model.primary.x = t->model.primary.y = t->model.alternate.x = t->model.alternate.y = 0;
    t->model.primary.wrap_pending = t->model.alternate.wrap_pending = false;
    t->truncated = false;
    offset = feed_line(t, text, length);
    tv_term_finish(&t->model);
    s = active(t);
    used = TRANSCRIPT_MODEL_COLUMNS;
    if (offset + used > TRANSCRIPT_COLUMNS) { used = TRANSCRIPT_COLUMNS - offset; t->truncated = true; }
    memcpy(t->line + offset, s->canvas.cells, used * sizeof(*t->line));
    for (t->count = offset + used; t->count && !content(&t->line[t->count - 1]); t->count--) {}
    return t->count;
}

/* End of the row that starts at start: at most room cells, never splitting a
 * wide cell from its continuation. */
static size_t segment_end(const struct transcript *t, size_t start, size_t room) {
    size_t end = start + room < t->count ? start + room : t->count;
    if (end < t->count && end > start + 1 && !t->line[end].width) end--;
    return end;
}

static size_t row_room(int width, bool continuation) {
    size_t room = width > 2 ? (size_t)width : 2;
    return continuation ? room - 1 : room;
}

size_t transcript_rows(const struct transcript *t, int width) {
    size_t rows = 1, start = segment_end(t, 0, row_room(width, false));
    while (start < t->count) { rows++; start = segment_end(t, start, row_room(width, true)); }
    return rows;
}

/* Captured rendition is kept (colors only when enabled); unstyled cells take
 * the line tone so diffs and headings follow the theme. */
static struct tv_cell styled_cell(const struct tv_cell *source, enum tv_style tone, bool color, bool unicode) {
    struct tv_cell cell = *source;
    unsigned emphasis = cell.attributes & (unsigned)~TV_EXPLICIT;
    if (!color) cell.foreground = cell.background = TV_COLOR_DEFAULT;
    cell.style = tone;
    cell.attributes = emphasis || cell.foreground != TV_COLOR_DEFAULT || cell.background != TV_COLOR_DEFAULT ?
        emphasis | TV_EXPLICIT : 0;
    if (!unicode && (cell.glyph > 126 || !cell.width || cell.combining_count)) {
        cell.glyph = cell.width ? tv_ascii_fallback(cell.glyph) : ' ';
        cell.width = 1; cell.combining_count = 0;
    }
    return cell;
}

static void draw_segment(const struct transcript *t, struct tv_canvas *c, int x, int y, size_t start, size_t end,
                         enum tv_style tone, bool color) {
    size_t i;
    for (i = start; i < end && x + (int)(i - start) < c->width; i++)
        c->cells[(size_t)y * (size_t)c->stride + (size_t)x + (i - start)] = styled_cell(&t->line[i], tone, color, c->unicode);
}

/* One wrapped row: continuation marker, cells, and a truncation marker on
 * the last row of a line longer than TRANSCRIPT_COLUMNS. */
static void draw_row(const struct transcript *t, struct tv_canvas *c, struct tv_rect row, size_t start, size_t end,
                     bool continuation, enum tv_style tone, bool color) {
    struct tv_canvas line;
    if (tv_canvas_view(&line, c, row)) tv_clear(&line, TV_BASE);
    if (continuation) tv_put(c, row.x, row.y, c->unicode ? 0x21aaU : '>', TV_MUTED);
    draw_segment(t, c, row.x + (continuation ? 1 : 0), row.y, start, end, tone, color);
    if (end >= t->count && t->truncated) tv_put(c, row.x + row.width - 1, row.y, c->unicode ? 0x2026U : '~', TV_MUTED);
}

int transcript_draw(const struct transcript *t, struct tv_canvas *c, struct tv_rect area,
                    size_t first, enum tv_style tone, bool color) {
    size_t row = 0, start = 0;
    int y = area.y, bottom = area.y + area.height < c->height ? area.y + area.height : c->height;
    if (area.width < 2 || area.x < 0 || area.x + area.width > c->width) return 0;
    do {
        size_t end = segment_end(t, start, row_room(area.width, row > 0));
        if (row >= first && y < bottom) draw_row(t, c, (struct tv_rect){area.x, y++, area.width, 1}, start, end, row > 0, tone, color);
        start = end; row++;
    } while (start < t->count);
    return y - area.y;
}

/* ASCII text of the line from column first, for prefix classification. */
static size_t line_text(const struct transcript *t, size_t first, char *out, size_t size) {
    size_t used = 0, i;
    for (i = first; i < t->count && used + 1 < size; i++) {
        if (!t->line[i].width) continue;
        out[used++] = t->line[i].glyph < 128 ? (char)t->line[i].glyph : '?';
    }
    out[used] = '\0';
    return used;
}

static size_t indent(const struct transcript *t) {
    size_t i = 0;
    while (i < t->count && t->line[i].glyph == ' ') i++;
    return i;
}

static bool starts(const char *text, const char *prefix) { return !strncmp(text, prefix, strlen(prefix)); }

static bool diff_header(const char *text) {
    return starts(text, "diff --git ") || starts(text, "--- a/") || starts(text, "+++ b/") ||
        starts(text, "--- /dev/null") || starts(text, "+++ /dev/null");
}

/* Enters diff context on a header, hunk or ```diff fence; returns its tone. */
static bool diff_entry(struct transcript *t, const char *text, size_t at, enum tv_style *tone) {
    if (starts(text, "```diff")) { t->diff = true; t->diff_indent = at; *tone = TV_MUTED; return true; }
    if (t->diff && starts(text, "```")) { t->diff = false; *tone = TV_MUTED; return true; }
    if (diff_header(text) || (t->diff && (starts(text, "index ") || starts(text, "new file mode") ||
        starts(text, "deleted file mode")))) { t->diff = true; t->diff_indent = at; *tone = TV_STRONG; return true; }
    if (starts(text, "@@ ") && strstr(text + 3, "@@")) { t->diff = true; t->diff_indent = at; *tone = TV_BORDER; return true; }
    return false;
}

/* Theme tone of a unified-diff line in context; leaves context otherwise. */
static enum tv_style diff_tone(struct transcript *t, enum tv_style tone) {
    char text[64];
    size_t at = indent(t);
    enum tv_style entry;
    line_text(t, at, text, sizeof(text));
    if (diff_entry(t, text, at, &entry)) return entry;
    if (!t->diff || !t->count) return tone;
    if (at < t->diff_indent && at < t->count) { t->diff = false; return tone; }
    line_text(t, t->diff_indent, text, sizeof(text));
    if (text[0] == '+') return TV_SUCCESS;
    if (text[0] == '-') return TV_WARNING;
    if (text[0] != ' ' && text[0] != '\\') t->diff = false;
    return tone;
}

/* Provider key hints ("esc to interrupt", "? for shortcuts", "ctrl+o")
 * describe live input, not this read-only view. */
static bool hint_line(const struct transcript *t) {
    static const char *hints[] = {"esc to ", "? for shortcuts", "ctrl+", "ctrl-", "shift+tab", "for agents",
                                  "enter to ", "tab to ", "to interrupt", "to cycle"};
    char text[TRANSCRIPT_MODEL_COLUMNS];
    size_t i;
    line_text(t, 0, text, sizeof(text));
    for (i = 0; text[i]; i++) text[i] = (char)tolower((unsigned char)text[i]);
    for (i = 0; i < sizeof(hints) / sizeof(*hints); i++) if (strstr(text, hints[i])) return true;
    return false;
}

static size_t line_length(const char *text, size_t length) {
    const char *end = memchr(text, '\n', length);
    return end ? (size_t)(end - text) : length;
}

/* Bytes up to the end of the last line with visible content. */
static size_t visible_length(struct transcript *t, const char *text, size_t length) {
    size_t at = 0, keep = 0;
    transcript_reset(t);
    while (at < length) {
        size_t n = line_length(text + at, length - at);
        if (transcript_parse(t, text + at, n)) keep = at + n;
        at += n + 1;
    }
    return keep;
}

/* Walks the document once; draws rows from first when drawing is requested. */
static size_t transcript_walk(struct transcript *t, struct tv_canvas *c, struct tv_rect area, const char *text,
                              size_t length, size_t first, struct transcript_layout *layout) {
    size_t at = 0, row = 0;
    int y = area.y;
    transcript_reset(t);
    while (at < length || (!length && !row)) {
        size_t n = line_length(text + at, length - at), rows;
        enum tv_style tone;
        transcript_parse(t, text + at, n);
        tone = diff_tone(t, TV_BASE);
        rows = transcript_rows(t, area.width);
        if (!layout->hints && hint_line(t)) layout->hints = true;
        if (c && row + rows > first && y < area.y + area.height) {
            size_t skip = first > row ? first - row : 0;
            y += transcript_draw(t, c, (struct tv_rect){area.x, y, area.width, area.y + area.height - y}, skip, tone, layout->color);
        }
        row += rows; at += n + 1;
    }
    layout->drawn = (size_t)(y - area.y);
    return row;
}

void transcript_render(struct transcript *t, struct tv_canvas *c, struct tv_rect area,
                       const char *text, size_t length, struct transcript_layout *layout) {
    size_t room = area.height > 0 ? (size_t)area.height : 0, last;
    layout->hints = false; layout->drawn = 0;
    if (!room || area.width < 2) { layout->rows = 0; return; }
    length = visible_length(t, text, length);
    layout->rows = transcript_walk(t, NULL, area, text, length, 0, layout);
    last = layout->rows > room ? layout->rows - room : 0;
    if (layout->back > last) layout->back = last;
    if (layout->follow) layout->scroll = last - layout->back;
    else if (layout->scroll > last) layout->scroll = last;
    (void)transcript_walk(t, c, area, text, length, layout->scroll, layout);
}
