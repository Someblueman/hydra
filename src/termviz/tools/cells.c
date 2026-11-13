/* termviz-cells: replay a captured terminal byte stream through the product's
 * own terminal interpreter (src/termviz/terminal*.c, the model the native PTY
 * tests observe) and print the resulting screen with exact colours and
 * attributes as JSON, or as plain text. Used by scripts/visual_review and the
 * U10 loopback driver; it is not installed and never touches a terminal.
 *
 * usage: termviz-cells --size COLSxROWS [--text] FILE
 * JSON: {"columns","rows","alternate","cursor":{x,y,visible},"unsupported",
 *        "cells":[[[glyph, fg, bg, attributes, width], ...], ...]}
 * fg/bg are "rrggbb" or null (the terminal's default colour). */
#include "../terminal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_size(const char *text, int *columns, int *rows) {
    char *end;
    long c, r;
    errno = 0;
    c = strtol(text, &end, 10);
    if (errno || end == text || *end != 'x' || c < 1 || c > 512) return -1;
    r = strtol(end + 1, &end, 10);
    if (errno || *end || r < 1 || r > 256) return -1;
    *columns = (int)c;
    *rows = (int)r;
    return 0;
}

static unsigned char *read_all(const char *path, size_t *length) {
    FILE *file = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    unsigned char *data = NULL, *grown;
    size_t capacity = 0, used = 0, got;
    if (!file) return NULL;
    do {
        if (used == capacity) {
            capacity = capacity ? capacity * 2 : 65536;
            grown = realloc(data, capacity);
            if (!grown) { free(data); if (file != stdin) fclose(file); return NULL; }
            data = grown;
        }
        got = fread(data + used, 1, capacity - used, file);
        used += got;
    } while (got);
    if (file != stdin) fclose(file);
    *length = used;
    return data;
}

static void print_glyph(uint32_t glyph) {
    char utf8[4];
    size_t n, i;
    if (glyph < 0x20 || glyph == 0x7f) { printf("\"?\""); return; }
    if (glyph == '"' || glyph == '\\') { printf("\"\\%c\"", (char)glyph); return; }
    n = tv_utf8_encode(glyph, utf8);
    putchar('"');
    for (i = 0; i < n; i++) putchar(utf8[i]);
    putchar('"');
}

static void print_color(uint32_t color) {
    if (color == TV_COLOR_DEFAULT) printf("null");
    else printf("\"%06x\"", (unsigned)(color & 0xffffffu));
}

static void print_cell(const struct tv_cell *cell) {
    putchar('[');
    if (cell->width == 0) printf("\"\"");
    else print_glyph(cell->glyph);
    putchar(',');
    print_color(cell->foreground);
    putchar(',');
    print_color(cell->background);
    printf(",%u,%u]", cell->attributes & ~(unsigned)TV_EXPLICIT, (unsigned)cell->width);
}

static void print_json(const struct tv_terminal_model *t) {
    const struct tv_screen *screen = t->alternate_active ? &t->alternate : &t->primary;
    const struct tv_canvas *c = &screen->canvas;
    int x, y;
    printf("{\"columns\":%d,\"rows\":%d,\"alternate\":%s,\"cursor\":{\"x\":%d,\"y\":%d,\"visible\":%s},"
           "\"unsupported\":%llu,\"cells\":[", c->width, c->height, t->alternate_active ? "true" : "false",
           screen->x, screen->y, t->cursor_visible ? "true" : "false", (unsigned long long)t->unsupported);
    for (y = 0; y < c->height; y++) {
        printf("%s[", y ? "," : "");
        for (x = 0; x < c->width; x++) {
            if (x) putchar(',');
            print_cell(&c->cells[(size_t)y * (size_t)c->stride + (size_t)x]);
        }
        putchar(']');
    }
    printf("]}\n");
}

static void print_text(const struct tv_terminal_model *t) {
    const struct tv_canvas *c = t->alternate_active ? &t->alternate.canvas : &t->primary.canvas;
    int x, y;
    char utf8[4];
    for (y = 0; y < c->height; y++) {
        for (x = 0; x < c->width; x++) {
            const struct tv_cell *cell = &c->cells[(size_t)y * (size_t)c->stride + (size_t)x];
            size_t n;
            if (cell->width == 0) continue;
            if (cell->glyph < 0x20 || cell->glyph == 0x7f) { putchar('?'); continue; }
            n = tv_utf8_encode(cell->glyph, utf8);
            fwrite(utf8, 1, n, stdout);
        }
        putchar('\n');
    }
}

static int usage(void) {
    fputs("usage: termviz-cells --size COLSxROWS [--text] FILE|-\n", stderr);
    return 2;
}

int main(int argc, char **argv) {
    struct tv_terminal_model term;
    struct tv_cell *primary, *alternate;
    unsigned char *bytes;
    size_t length, cells;
    int columns = 0, rows = 0, text = 0, index;
    const char *path = NULL;
    for (index = 1; index < argc; index++) {
        if (!strcmp(argv[index], "--size") && index + 1 < argc) {
            if (parse_size(argv[++index], &columns, &rows)) return usage();
        } else if (!strcmp(argv[index], "--text")) text = 1;
        else if (!path) path = argv[index];
        else return usage();
    }
    if (!path || !columns) return usage();
    cells = (size_t)columns * (size_t)rows;
    primary = calloc(cells, sizeof(*primary));
    alternate = calloc(cells, sizeof(*alternate));
    bytes = read_all(path, &length);
    if (!primary || !alternate || !bytes) {
        fprintf(stderr, "termviz-cells: cannot read %s\n", path);
        free(primary); free(alternate); free(bytes);
        return 1;
    }
    if (!tv_term_init(&term, primary, alternate, columns, rows, NULL, 0, columns, rows)) return usage();
    tv_term_feed(&term, bytes, length);
    tv_term_finish(&term);
    if (text) print_text(&term);
    else print_json(&term);
    free(primary); free(alternate); free(bytes);
    return ferror(stdout) ? 1 : 0;
}
