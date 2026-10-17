#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* Shared text layout for the detail-style views: labelled rows that wrap
 * under their value, names that end in an ellipsis instead of a cut word,
 * paths that keep their head and tail, and the lead block every screen
 * opens with (what it is, its state, and what the user can do). */

static const char *ellipsis(const struct app *app) { return app->ascii ? "..." : "\xe2\x80\xa6"; }
static size_t ellipsis_columns(const struct app *app) { return app->ascii ? 3U : 1U; }

/* Byte offset of the first column boundary at or before width scalars. */
static size_t scalar_prefix(const char *text, size_t width) {
    size_t i = 0U, seen = 0U;
    while (text[i]) {
        size_t next = i + 1U;
        while (text[next] && ((unsigned char)text[next] & 0xC0U) == 0x80U) next++;
        if (seen == width) return i;
        seen++; i = next;
    }
    return i;
}

static size_t scalar_count(const char *text) {
    size_t n = 0U;
    for (; *text; text++) if (((unsigned char)*text & 0xC0U) != 0x80U) n++;
    return n;
}

/* text within width columns: whole, or its start and an ellipsis. */
void fit_text(const struct app *app, const char *text, size_t width, char *out, size_t size) {
    size_t length = scalar_count(text), keep, dots = ellipsis_columns(app);
    if (length <= width) { snprintf(out, size, "%s", text); return; }
    if (width <= dots) { keep = scalar_prefix(text, width); snprintf(out, size, "%.*s", (int)keep, text); return; }
    keep = scalar_prefix(text, width - dots);
    snprintf(out, size, "%.*s%s", (int)keep, text, ellipsis(app));
}

/* A path or URL within width columns: its first segment and the end of the
 * path stay readable, the middle is elided. */
void fit_path(const struct app *app, const char *path, size_t width, char *out, size_t size) {
    size_t length = scalar_count(path), head, tail_from, dots = ellipsis_columns(app);
    if (length <= width) { snprintf(out, size, "%s", path); return; }
    if (width < 8U + dots) { fit_text(app, path, width, out, size); return; }
    head = scalar_prefix(path, width / 3U);
    tail_from = scalar_prefix(path, length - (width - width / 3U - dots));
    snprintf(out, size, "%.*s%s%s", (int)head, path, ellipsis(app), path + tail_from);
}

/* Bytes of text that fit width columns, broken at a space when one is near. */
static size_t wrap_chunk(const char *text, size_t length, size_t width) {
    size_t cut;
    if (length <= width) return length;
    for (cut = width; cut > width / 2U && cut < length; cut--) if (text[cut] == ' ') return cut;
    cut = width < length ? width : length;
    while (cut > 1U && ((unsigned char)text[cut] & 0xC0U) == 0x80U) cut--;
    return cut;
}

/* "Label       text" with the text wrapped under itself; the label column
 * is label_width wide. The first *skip rows are counted off instead of
 * drawn (a scrolled detail); rows beyond the content area are not drawn. */
void labelled_skip(struct app *app, int label_width, const char *label, const char *text, enum tv_style tone, size_t *skip) {
    size_t length = strlen(text), offset = 0U, width;
    int saved_x = app->content_x, saved_width = app->content_width;
    bool first = true;
    app->content_x += label_width; app->content_width -= label_width;
    width = app->content_width > 0 ? (size_t)app->content_width : 1U;
    do {
        char piece[1024];
        size_t start = offset, chunk = wrap_chunk(text + offset, length - offset, width);
        offset += chunk;
        offset += strspn(text + offset, " ");
        if (*skip) { (*skip)--; first = false; continue; }
        if (app->line >= app->limit) break;
        snprintf(piece, sizeof(piece), "%.*s", (int)(chunk < sizeof(piece) ? chunk : sizeof(piece) - 1U), text + start);
        if (first) column(app, -label_width, label_width, TV_MUTED, label);
        column(app, 0, app->content_width, tone, piece);
        app->line++;
        first = false;
    } while (offset < length);
    app->content_x = saved_x; app->content_width = saved_width;
}

void labelled_at(struct app *app, int label_width, const char *label, const char *text, enum tv_style tone) {
    size_t skip = 0U;
    if (app->line >= app->limit) return;
    labelled_skip(app, label_width, label, text, tone, &skip);
}

/* Wrapped prose after skipping *skip rows, as labelled_skip does. */
void paragraph_skip(struct app *app, const char *text, enum tv_style tone, size_t *skip) {
    size_t length = strlen(text), offset = 0U, width = app->content_width > 0 ? (size_t)app->content_width : 1U;
    int saved = app->tone;
    app->tone = (int)tone;
    while (offset < length) {
        size_t chunk = wrap_chunk(text + offset, length - offset, width);
        if (*skip) (*skip)--;
        else if (app->line < app->limit) linef(app, "%.*s", (int)chunk, text + offset);
        offset += chunk;
        offset += strspn(text + offset, " ");
    }
    app->tone = saved;
}

void labelled(struct app *app, const char *label, const char *text, enum tv_style tone) {
    labelled_at(app, DETAIL_LABEL, label, text, tone);
}

/* Two labelled values per row where the view is wide enough to keep both
 * readable; one per row otherwise, so neither is cut. */
void labelled_pair(struct app *app, const char *left_label, const char *left, enum tv_style left_tone,
                   const char *right_label, const char *right, enum tv_style right_tone) {
    if (app->content_width >= 110 && (int)strlen(left) < app->content_width / 2 - 13 &&
        (int)strlen(right) < app->content_width / 2 - 12) {
        pair(app, left_label, left, left_tone, right_label, right, right_tone); return;
    }
    labelled(app, left_label, left, left_tone);
    labelled(app, right_label, right, right_tone);
}

/* A muted eyebrow above a strong title that wraps between words: the first
 * rows of a detail screen say what is being looked at. */
void lead_block(struct app *app, const char *eyebrow, const char *title) {
    if (eyebrow && eyebrow[0]) { style(app, TONE_MUTED); linef(app, "%s", eyebrow); style(app, TONE_BASE); }
    paragraph(app, title, TV_STRONG);
}

/* "Label  value" where the label is a key the user can press, drawn as the
 * NEXT groups do: key, two spaces, what it does. */
void key_row(struct app *app, const char *key, const char *action) {
    char text[512];
    snprintf(text, sizeof(text), "%s  %s", key, action);
    paragraph(app, text, TV_BASE);
}
