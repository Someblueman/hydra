#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Independent observer of emitted ANSI; never calls the product terminal parser. */
static unsigned palette(int n) {
    static const unsigned basic[] = {0,        0xcd3131, 0x0dbc79, 0xe5e510, 0x2472c8, 0xbc3fbc,
                                     0x11a8cd, 0xe5e5e5, 0x666666, 0xf14c4c, 0x23d18b, 0xf5f543,
                                     0x3b8eea, 0xd670d6, 0x29b8db, 0xffffff};
    static const unsigned levels[] = {0, 95, 135, 175, 215, 255};
    CHECK(n >= 0 && n < 256, "palette index");
    if (n < 16)
        return basic[n];
    if (n >= 232) {
        unsigned v = 8 + (unsigned)(n - 232) * 10;
        return v * 0x010101;
    }
    n -= 16;
    return levels[n / 36] * 65536 + levels[n / 6 % 6] * 256 + levels[n % 6];
}
static void clear_cells(struct tv_screen *s) {
    int i;
    for (i = 0; i < s->cols * s->rows; i++) {
        struct tv_cell *c = &s->cells[i];
        memset(c, 0, sizeof(*c));
        c->text[0] = ' ';
        c->fg = s->fg;
        c->bg = s->bg;
        c->width = 1;
    }
}
void tv_screen_reset(struct tv_screen *s, int cols, int rows, bool await) {
    /* A resize changes the grid, not the byte stream already being decoded. */
    unsigned utf_value = s->utf_value;
    int utf_remaining = s->utf_remaining;
    size_t escape_size = s->escape_size;
    char escape[sizeof(s->escape)];
    memcpy(escape, s->escape, sizeof(escape));
    free(s->cells);
    free(s->text);
    memset(s, 0, sizeof(*s));
    CHECK(cols > 0 && cols <= 512 && rows > 0 && rows <= 256, "screen dimensions");
    s->cols = cols;
    s->rows = rows;
    s->awaiting_clear = await;
    if (await) {
        s->utf_value = utf_value;
        s->utf_remaining = utf_remaining;
        s->escape_size = escape_size;
        memcpy(s->escape, escape, sizeof(escape));
    }
    s->fg = 0xdddddd;
    s->bg = 0x161616;
    s->cells = calloc((size_t)cols * (size_t)rows, sizeof(*s->cells));
    s->text = calloc((size_t)cols * (size_t)rows * 32 + (size_t)rows + 1, 1);
    CHECK(s->cells && s->text, "screen allocation");
    clear_cells(s);
}
static void sgr(struct tv_screen *s, const int *v, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        int x = v[i];
        if (x == 0) {
            s->fg = 0xdddddd;
            s->bg = 0x161616;
            s->bold = false;
            s->reverse = false;
        } else if (x == 1)
            s->bold = true;
        else if (x == 7)
            s->reverse = true;
        else if ((x == 38 || x == 48) && i + 2 < n) {
            unsigned color;
            if (v[i + 1] == 5) {
                color = palette(v[i + 2]);
                i += 2;
            } else {
                CHECK(v[i + 1] == 2 && i + 4 < n, "presenter color");
                color = (unsigned)v[i + 2] * 65536 + (unsigned)v[i + 3] * 256 + (unsigned)v[i + 4];
                i += 4;
            }
            if (x == 38)
                s->fg = color;
            else
                s->bg = color;
        } else if (x >= 30 && x <= 37)
            s->fg = palette(x - 30);
        else if (x >= 40 && x <= 47)
            s->bg = palette(x - 40);
    }
}
static void escape_end(struct tv_screen *s, unsigned code) {
    int values[64] = {0};
    size_t n = 1, i;
    if (s->escape[1] != '[' || s->escape[2] == '?')
        return;
    for (i = 2; i + 1 < s->escape_size; i++) {
        char ch = s->escape[i];
        if (ch == ';') {
            CHECK(n < 64, "CSI parameter bound");
            n++;
        } else if (ch >= '0' && ch <= '9') {
            CHECK(values[n - 1] < 100000, "CSI integer bound");
            values[n - 1] = values[n - 1] * 10 + ch - '0';
        } else
            return;
    }
    if (code == 'H' || code == 'f') {
        s->y = (values[0] ? values[0] : 1) - 1;
        s->x = (n > 1 && values[1] ? values[1] : 1) - 1;
    } else if (code == 'J' && values[0] == 2) {
        s->awaiting_clear = false;
        s->clears++;
        clear_cells(s);
    } else if (code == 'm')
        sgr(s, values, n);
}
static void character(struct tv_screen *s, unsigned code) {
    char utf[MB_LEN_MAX + 1];
    mbstate_t state;
    size_t len;
    int width;
    if (s->escape_size) {
        CHECK(code < 128 && s->escape_size < 256, "bounded presenter escape");
        s->escape[s->escape_size++] = (char)code;
        if (s->escape_size >= 3 && code >= '@' && code <= '~') {
            escape_end(s, code);
            s->escape_size = 0;
        }
        return;
    }
    if (code == 27) {
        s->escape[0] = 27;
        s->escape_size = 1;
        return;
    }
    if (s->awaiting_clear)
        return;
    if (code == '\r') {
        s->x = 0;
        return;
    }
    if (code == '\n') {
        s->y++;
        return;
    }
    if (code < 32)
        return;
    memset(&state, 0, sizeof(state));
    len = wcrtomb(utf, (wchar_t)code, &state);
    if (len == (size_t)-1) {
        utf[0] = '?';
        len = 1;
    }
    utf[len] = 0;
    width = wcwidth((wchar_t)code);
    if (width < 0)
        width = 1;
    if (width == 0) {
        int x = s->x - 1;
        if (s->y >= 0 && s->y < s->rows && x >= 0 && x < s->cols) {
            struct tv_cell *c;
            if (!s->cells[s->y * s->cols + x].width && x)
                x--;
            c = &s->cells[s->y * s->cols + x];
            CHECK(strlen(c->text) + len < sizeof(c->text), "combining cell bound");
            memcpy(c->text + strlen(c->text), utf, len + 1);
        }
        return;
    }
    if (s->x < 0 || s->x + width > s->cols || s->y < 0 || s->y >= s->rows)
        s->overflow++;
    else {
        struct tv_cell *c = &s->cells[s->y * s->cols + s->x];
        memcpy(c->text, utf, len + 1);
        c->fg = s->reverse ? s->bg : s->fg;
        c->bg = s->reverse ? s->fg : s->bg;
        c->bold = s->bold;
        c->width = width;
        if (width == 2) {
            c[1] = *c;
            c[1].text[0] = 0;
            c[1].width = 0;
        }
    }
    s->x += width;
}
void tv_screen_feed(struct tv_screen *s, const unsigned char *bytes, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned ch = bytes[i];
        if (s->utf_remaining) {
            CHECK((ch & 0xc0) == 0x80, "presenter UTF-8 continuation");
            s->utf_value = s->utf_value * 64 + (ch & 63);
            if (--s->utf_remaining == 0)
                character(s, s->utf_value);
        } else if (ch < 128)
            character(s, ch);
        else if ((ch & 0xe0) == 0xc0) {
            s->utf_value = ch & 31;
            s->utf_remaining = 1;
        } else if ((ch & 0xf0) == 0xe0) {
            s->utf_value = ch & 15;
            s->utf_remaining = 2;
        } else if ((ch & 0xf8) == 0xf0) {
            s->utf_value = ch & 7;
            s->utf_remaining = 3;
        } else
            CHECK(false, "presenter UTF-8 lead");
    }
}
const char *tv_text(struct tv_session *s) {
    size_t at = 0;
    int x, y;
    for (y = 0; y < s->screen.rows; y++) {
        for (x = 0; x < s->screen.cols; x++) {
            const char *v = s->screen.cells[y * s->screen.cols + x].text;
            size_t n = strlen(v);
            memcpy(s->screen.text + at, v, n);
            at += n;
        }
        if (y + 1 < s->screen.rows)
            s->screen.text[at++] = '\n';
    }
    s->screen.text[at] = 0;
    return s->screen.text;
}
const char *tv_row(struct tv_session *s, int row, char *out, size_t capacity) {
    int x;
    size_t at = 0;
    CHECK(row >= 0 && row < s->screen.rows, "row");
    for (x = 0; x < s->screen.cols; x++) {
        const char *v = s->screen.cells[row * s->screen.cols + x].text;
        size_t n = strlen(v);
        CHECK(at + n < capacity, "row capacity");
        memcpy(out + at, v, n);
        at += n;
    }
    out[at] = 0;
    return out;
}
bool tv_contains(struct tv_session *s, const char *marker) {
    return strstr(tv_text(s), marker) != NULL;
}
static void html_text(FILE *f, const char *text) {
    for (; *text; text++)
        switch (*text) {
        case '&':
            fputs("&amp;", f);
            break;
        case '<':
            fputs("&lt;", f);
            break;
        case '>':
            fputs("&gt;", f);
            break;
        case '"':
            fputs("&quot;", f);
            break;
        default:
            fputc(*text, f);
        }
}
void tv_save(struct tv_session *s, const char *path) {
    FILE *f = fopen(path, "w");
    int x, y;
    CHECK(f, "HTML evidence open");
    fputs("<!doctype html><meta charset=\"utf-8\"><title>Actual native PTY "
          "output</title><style>body{background:#161616;padding:20px;color:#ddd}pre{font:14px/1.3 "
          "monospace}span{display:inline-block}</style><p>Actual PTY output / independently "
          "reconstructed terminal cells</p><pre>",
          f);
    for (y = 0; y < s->screen.rows; y++) {
        for (x = 0; x < s->screen.cols; x++) {
            struct tv_cell *c = &s->screen.cells[y * s->screen.cols + x];
            if (!c->width)
                continue;
            fprintf(f, "<span style=\"color:#%06x;background:#%06x;font-weight:%d;width:%dch\">",
                    c->fg, c->bg, c->bold ? 700 : 400, c->width);
            html_text(f, c->text);
            fputs("</span>", f);
        }
        fputc('\n', f);
    }
    fputs("</pre>\n", f);
    CHECK(!fclose(f), "HTML evidence close");
}
