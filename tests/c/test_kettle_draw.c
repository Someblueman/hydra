#include "kettle/kettle.h"
#include "kettle/layout.h"
#include <stdlib.h>
#include <string.h>

static void check_at(bool passed, const char *what, int line) {
    if (passed) return;
    fprintf(stderr, "FAIL tests/c/test_kettle_draw.c:%d: %s\n", line, what);
    exit(1);
}
#define CHECK(condition) check_at((condition), #condition, __LINE__)

#define COLUMNS 24
#define ROWS 6
static struct kt_cell cells[COLUMNS * ROWS];
static struct kt_canvas canvas;

static void fresh(bool unicode) { CHECK(kt_init(&canvas, cells, COLUMNS * ROWS, COLUMNS, ROWS, unicode)); }
static const struct kt_cell *cell(int x, int y) { return &cells[y * COLUMNS + x]; }

/* A row as UTF-8, trailing blanks removed, continuation cells skipped. */
static const char *row(int y) {
    static char text[COLUMNS * 16 + 1];
    size_t used = 0, last = 0;
    int x;
    for (x = 0; x < COLUMNS; x++) {
        const struct kt_cell *c = cell(x, y);
        unsigned i;
        if (!c->width) continue;
        used += kt_utf8_encode(c->glyph, text + used);
        for (i = 0; i < c->combining_count; i++) used += kt_utf8_encode(c->combining[i], text + used);
        if (c->glyph != ' ' || c->combining_count) last = used;
    }
    text[last] = '\0';
    return text;
}

/* No wide glyph is ever left without its second column, or the reverse. */
static void whole_cells(int y) {
    int x;
    for (x = 0; x < COLUMNS; x++) {
        if (cell(x, y)->width == 2) CHECK(x + 1 < COLUMNS && cell(x + 1, y)->width == 0);
        if (cell(x, y)->width == 0) CHECK(x > 0 && cell(x - 1, y)->width == 2);
    }
}

/* ---- width -------------------------------------------------------------- */

static void widths(void) {
    CHECK(kt_text_width(NULL) == 0);
    CHECK(kt_text_width("") == 0);
    CHECK(kt_text_width("abc") == 3);
    CHECK(kt_text_width("caf\xc3\xa9") == 4);                       /* precomposed */
    CHECK(kt_text_width("cafe\xcc\x81") == 4);                      /* combining acute joins */
    CHECK(kt_text_width("\xe7\x95\x8c\xe9\x9d\xa2") == 4);           /* two wide cells */
    CHECK(kt_text_width("a\xe7\x95\x8c" "b") == 4);
    CHECK(kt_text_width("\xf0\x9f\x9a\x80") == 2);                   /* emoji */
    CHECK(kt_text_width("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb") == 2);   /* ZWJ sequence: one cluster */
    CHECK(kt_text_width("\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd") == 2);   /* skin tone joins */
    CHECK(kt_text_width("\xe2\x9c\x94\xef\xb8\x8f") == 2);           /* VS16 widens a narrow symbol */
    CHECK(kt_text_width("\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa") == 2);   /* flag: two indicators, one cell pair */
    CHECK(kt_text_width("a\xe2\x80\x8b" "b") == 2);                  /* zero-width space dropped */
    CHECK(kt_text_width("a\x01" "b") == 3);
    CHECK(kt_text_width("a\xff" "b") == 3);   /* shown as '?' */
    CHECK(kt_text_width("co\xc2\xadop") == 5);                       /* soft hyphen shows as '-' */
    CHECK(kt_text_width("\xcc\x81" "a") == 1);                       /* a mark with nothing to join */
    /* What is measured is what is drawn. */
    fresh(true);
    kt_text(&canvas, (struct kt_rect){0, 0, COLUMNS, 1}, "a\xe7\x95\x8c" "e\xcc\x81\xe2\x9c\x94\xef\xb8\x8f!", KT_BASE);
    CHECK(cell(kt_text_width("a\xe7\x95\x8c" "e\xcc\x81\xe2\x9c\x94\xef\xb8\x8f"), 0)->glyph == '!');
}

/* ---- aligned text and the ellipsis -------------------------------------- */

/* "ab" + wide + e-with-combining-acute + wide + "z": columns 1 1 2 1 2 1. */
#define MIXED "ab\xe7\x95\x8c" "e\xcc\x81\xe9\x9d\xa2z"
#define DOTS "\xe2\x80\xa6"

static void ellipsis(void) {
    static const struct { int room, used; const char *text; } cuts[] = {
        {1, 1, DOTS}, {2, 2, "a" DOTS}, {3, 3, "ab" DOTS},
        /* the wide cell does not fit beside the ellipsis: a column stays empty */
        {4, 3, "ab" DOTS}, {5, 5, "ab\xe7\x95\x8c" DOTS},
        /* the mark stays on its base, on whichever side of the cut that is */
        {6, 6, "ab\xe7\x95\x8c" "e\xcc\x81" DOTS}, {7, 6, "ab\xe7\x95\x8c" "e\xcc\x81" DOTS},
        {8, 8, MIXED}, {9, 8, MIXED}
    };
    size_t i;
    CHECK(kt_text_width(MIXED) == 8);
    for (i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        fresh(true);
        CHECK(kt_text_aligned(&canvas, (struct kt_rect){2, 1, cuts[i].room, 1}, MIXED, KT_ALIGN_LEFT, KT_STRONG) == cuts[i].used);
        CHECK(!strcmp(row(1) + 2, cuts[i].text));
        CHECK(row(1)[0] == ' ');
        CHECK(row(1)[1] == ' ');
        whole_cells(1);
        /* Nothing is drawn outside the area, in either direction. */
        CHECK(kt_style_equal(kt_cell_style(cell(1, 1)), KT_BASE));
        CHECK(kt_style_equal(kt_cell_style(cell(2 + cuts[i].room, 1)), KT_BASE));
        CHECK(cell(2 + cuts[i].room, 1)->glyph == ' ');
        CHECK(kt_style_equal(kt_cell_style(cell(2, 1)), KT_STRONG));
    }
    /* No room at all draws nothing. */
    fresh(true);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){2, 1, 0, 1}, MIXED, KT_ALIGN_LEFT, KT_BASE) == 0);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){2, 1, -3, 1}, MIXED, KT_ALIGN_LEFT, KT_BASE) == 0);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){2, 1, 5, 0}, MIXED, KT_ALIGN_LEFT, KT_BASE) == 0);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){2, 9, 5, 1}, MIXED, KT_ALIGN_LEFT, KT_BASE) == 0);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){2, 1, 5, 1}, NULL, KT_ALIGN_LEFT, KT_BASE) == 0);
    CHECK(!strcmp(row(1), ""));
    /* A ZWJ sequence and a flag are cut whole or not at all. */
    fresh(true);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 0, 4, 1}, "ab\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb" "c", KT_ALIGN_LEFT, KT_BASE) == 3);
    CHECK(!strcmp(row(0), "ab" DOTS));
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 1, 4, 1}, "a\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa" "cd", KT_ALIGN_LEFT, KT_BASE) == 4);
    CHECK(!strcmp(row(1), "a\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa" DOTS));
    whole_cells(0); whole_cells(1);
    /* ASCII canvases use three dots, or as many as fit. */
    fresh(false);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 0, 8, 1}, "abcdefghij", KT_ALIGN_LEFT, KT_BASE) == 8);
    CHECK(!strcmp(row(0), "abcde..."));
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 1, 4, 1}, "abcdefghij", KT_ALIGN_LEFT, KT_BASE) == 4);
    CHECK(!strcmp(row(1), "a..."));
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 2, 3, 1}, "abcdefghij", KT_ALIGN_LEFT, KT_BASE) == 3);
    CHECK(!strcmp(row(2), "..."));
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 3, 2, 1}, "abcdefghij", KT_ALIGN_LEFT, KT_BASE) == 2);
    CHECK(!strcmp(row(3), ".."));
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 4, 10, 1}, "abcdefghij", KT_ALIGN_LEFT, KT_BASE) == 10);
    CHECK(!strcmp(row(4), "abcdefghij"));
    /* A wide glyph keeps its two columns as an approximation, so the cut
     * falls in the same place. */
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){0, 5, 6, 1}, "a\xe7\x95\x8c" "bcdef", KT_ALIGN_LEFT, KT_BASE) == 6);
    CHECK(!strcmp(row(5), "a? ..."));
}

static void alignment(void) {
    fresh(true);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){4, 0, 10, 1}, "hi", KT_ALIGN_LEFT, KT_BASE) == 2);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){4, 1, 10, 1}, "hi", KT_ALIGN_CENTER, KT_BASE) == 2);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){4, 2, 10, 1}, "hi", KT_ALIGN_RIGHT, KT_BASE) == 2);
    CHECK(cell(4, 0)->glyph == 'h');
    CHECK(cell(8, 1)->glyph == 'h');
    CHECK(cell(12, 2)->glyph == 'h');
    CHECK(cell(13, 2)->glyph == 'i');
    /* An odd remainder leaves the extra column on the right. */
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){4, 3, 9, 1}, "hi", KT_ALIGN_CENTER, KT_BASE) == 2);
    CHECK(cell(7, 3)->glyph == 'h');
    /* Cut text fills the area whatever its alignment; a column the cut
     * could not use goes to the side the alignment leaves free. */
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){4, 4, 4, 1}, MIXED, KT_ALIGN_RIGHT, KT_BASE) == 3);
    CHECK(!strcmp(row(4) + 5, "ab" DOTS));
    CHECK(cell(4, 4)->glyph == ' ');
    /* Alignment is within what is on the canvas. */
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){COLUMNS - 4, 5, 10, 1}, "hi", KT_ALIGN_RIGHT, KT_BASE) == 2);
    CHECK(cell(COLUMNS - 2, 5)->glyph == 'h');
    CHECK(cell(COLUMNS - 1, 5)->glyph == 'i');
    fresh(true);
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){-3, 0, 8, 1}, "hi", KT_ALIGN_LEFT, KT_BASE) == 2);
    CHECK(cell(0, 0)->glyph == 'h');
    CHECK(kt_text_aligned(&canvas, (struct kt_rect){-3, 1, 8, 1}, "abcdefgh", KT_ALIGN_LEFT, KT_BASE) == 5);
    CHECK(!strcmp(row(1), "abcd" DOTS));
}

static void spans(void) {
    const struct kt_style a = KT_STRONG, b = KT_MUTED, c = KT_WARNING;
    const struct kt_span line[] = {{"ab", KT_STRONG}, {" cd ", KT_MUTED}, {"e\xe7\x95\x8c", KT_WARNING}};
    fresh(true);
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 0, 12, 1}, line, 3) == 9);
    CHECK(!strcmp(row(0), " ab cd e\xe7\x95\x8c"));
    CHECK(kt_style_equal(kt_cell_style(cell(2, 0)), a));
    CHECK(kt_style_equal(kt_cell_style(cell(3, 0)), b));
    CHECK(kt_style_equal(kt_cell_style(cell(7, 0)), c));
    CHECK(kt_style_equal(kt_cell_style(cell(9, 0)), c));
    /* Exactly enough room is enough. */
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 1, 9, 1}, line, 3) == 9);
    CHECK(!strcmp(row(1), " ab cd e\xe7\x95\x8c"));
    /* One column short: the wide cell goes, the ellipsis takes the cut run's style. */
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 2, 8, 1}, line, 3) == 8);
    CHECK(!strcmp(row(2), " ab cd e" DOTS));
    CHECK(kt_style_equal(kt_cell_style(cell(8, 2)), c));
    /* The cut falls in the middle run. */
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 3, 5, 1}, line, 3) == 5);
    CHECK(!strcmp(row(3), " ab c" DOTS));
    CHECK(kt_style_equal(kt_cell_style(cell(5, 3)), b));
    /* The cut falls exactly between two runs: the next run owns the ellipsis. */
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 4, 3, 1}, line, 3) == 3);
    CHECK(!strcmp(row(4), " ab" DOTS));
    CHECK(kt_style_equal(kt_cell_style(cell(3, 4)), b));
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 5, 1, 1}, line, 3) == 1);
    CHECK(!strcmp(row(5), " " DOTS));
    CHECK(kt_style_equal(kt_cell_style(cell(1, 5)), a));
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 5, 0, 1}, line, 3) == 0);
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 5, 9, 1}, NULL, 3) == 0);
    CHECK(kt_spans(&canvas, (struct kt_rect){1, 5, 9, 1}, line, 0) == 0);
    /* An empty or missing run is no run. */
    fresh(true);
    {
        const struct kt_span gaps[] = {{NULL, KT_BASE}, {"x", KT_BASE}, {"", KT_BASE}, {"y", KT_BASE}};
        CHECK(kt_spans(&canvas, (struct kt_rect){0, 0, 5, 1}, gaps, 4) == 2);
        CHECK(!strcmp(row(0), "xy"));
    }
}

/* ---- fill --------------------------------------------------------------- */

static void fill(void) {
    struct kt_style raised = KT_STYLE(KT_COLOR_TEXT, KT_COLOR_RAISED, 0);
    int x, y, filled = 0;
    fresh(true);
    kt_text(&canvas, (struct kt_rect){0, 1, COLUMNS, 1}, "0123456789", KT_STRONG);
    kt_fill(&canvas, (struct kt_rect){2, 1, 3, 2}, raised);
    for (y = 0; y < ROWS; y++) for (x = 0; x < COLUMNS; x++) {
        bool inside = x >= 2 && x < 5 && y >= 1 && y < 3;
        CHECK(kt_style_equal(kt_cell_style(cell(x, y)), raised) == inside);
        if (inside) { CHECK(cell(x, y)->glyph == ' '); filled++; }
    }
    CHECK(filled == 6);
    CHECK(!strcmp(row(1), "01   56789"));
    /* Clipped on every side, and harmless when nothing is on the canvas. */
    kt_fill(&canvas, (struct kt_rect){-5, -5, 7, 7}, raised);
    CHECK(kt_style_equal(kt_cell_style(cell(0, 0)), raised));
    CHECK(kt_style_equal(kt_cell_style(cell(1, 1)), raised));
    kt_fill(&canvas, (struct kt_rect){COLUMNS - 1, ROWS - 1, 1000000, 1000000}, raised);
    CHECK(kt_style_equal(kt_cell_style(cell(COLUMNS - 1, ROWS - 1)), raised));
    kt_fill(&canvas, (struct kt_rect){COLUMNS, 0, 3, 3}, raised);
    kt_fill(&canvas, (struct kt_rect){0, ROWS, 3, 3}, raised);
    kt_fill(&canvas, (struct kt_rect){3, 3, 0, 5}, raised);
    kt_fill(&canvas, (struct kt_rect){3, 3, 5, -1}, raised);
    kt_fill(&canvas, (struct kt_rect){2147483000, 2147483000, 2147483000, 2147483000}, raised);
    CHECK(kt_style_equal(kt_cell_style(cell(8, 3)), KT_BASE));
    /* An edge through a wide glyph takes the whole glyph, never half. */
    fresh(true);
    kt_text(&canvas, (struct kt_rect){0, 0, COLUMNS, 1}, "\xe7\x95\x8c\xe7\x95\x8c\xe7\x95\x8c", KT_BASE);
    kt_fill(&canvas, (struct kt_rect){1, 0, 4, 1}, raised);
    whole_cells(0);
    CHECK(cell(0, 0)->glyph == ' ');
    CHECK(cell(0, 0)->width == 1);
    CHECK(cell(5, 0)->glyph == ' ');
    CHECK(cell(5, 0)->width == 1);
}

/* ---- borders ------------------------------------------------------------ */

static void border_shape(const struct kt_border *border, bool unicode, const char *top, const char *middle,
                         const char *bottom) {
    fresh(unicode);
    kt_box(&canvas, (struct kt_rect){1, 1, 5, 3}, border, KT_BORDER);
    CHECK(!strcmp(row(0), ""));
    CHECK(!strcmp(row(4), ""));
    CHECK(!strcmp(row(1), top));
    CHECK(!strcmp(row(2), middle));
    CHECK(!strcmp(row(3), bottom));
    /* The inside is not touched. */
    CHECK(kt_style_equal(kt_cell_style(cell(3, 2)), KT_BASE));
}

static void borders(void) {
    struct kt_rect inner;
    border_shape(&kt_border_light, true, " \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90",
                 " \xe2\x94\x82   \xe2\x94\x82", " \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98");
    border_shape(&kt_border_rounded, true, " \xe2\x95\xad\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae",
                 " \xe2\x94\x82   \xe2\x94\x82", " \xe2\x95\xb0\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x95\xaf");
    border_shape(&kt_border_heavy, true, " \xe2\x94\x8f\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x93",
                 " \xe2\x94\x83   \xe2\x94\x83", " \xe2\x94\x97\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x9b");
    border_shape(&kt_border_double, true, " \xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97",
                 " \xe2\x95\x91   \xe2\x95\x91", " \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d");
    border_shape(&kt_border_bar, true, " \xe2\x96\x8c", " \xe2\x96\x8c", " \xe2\x96\x8c");
    border_shape(&kt_border_none, true, "", "", "");
    border_shape(&kt_border_light, false, " +---+", " |   |", " +---+");
    border_shape(&kt_border_rounded, false, " +---+", " |   |", " +---+");
    border_shape(&kt_border_heavy, false, " #####", " #   #", " #####");
    border_shape(&kt_border_double, false, " +===+", " |   |", " +===+");
    border_shape(&kt_border_bar, false, " |", " |", " |");
    border_shape(&kt_border_none, false, "", "", "");
    /* The room a border leaves. */
    inner = kt_box_inner((struct kt_rect){1, 1, 5, 3}, &kt_border_rounded);
    CHECK(inner.x == 2);
    CHECK(inner.y == 2);
    CHECK(inner.width == 3);
    CHECK(inner.height == 1);
    inner = kt_box_inner((struct kt_rect){1, 1, 5, 3}, &kt_border_bar);
    CHECK(inner.x == 2);
    CHECK(inner.y == 1);
    CHECK(inner.width == 4);
    CHECK(inner.height == 3);
    inner = kt_box_inner((struct kt_rect){1, 1, 5, 3}, &kt_border_none);
    CHECK(inner.x == 1);
    CHECK(inner.y == 1);
    CHECK(inner.width == 5);
    CHECK(inner.height == 3);
    inner = kt_box_inner((struct kt_rect){1, 1, 1, 1}, &kt_border_light);
    CHECK(inner.width == 0);
    CHECK(inner.height == 0);
    /* Clipped by the canvas, degenerate sizes, and a custom two-sided border. */
    fresh(true);
    kt_box(&canvas, (struct kt_rect){-2, -1, 6, 4}, &kt_border_light, KT_BORDER);
    CHECK(!strcmp(row(0), "   \xe2\x94\x82"));
    CHECK(!strcmp(row(2), "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98"));
    kt_box(&canvas, (struct kt_rect){COLUMNS - 2, ROWS - 2, 9, 9}, &kt_border_light, KT_BORDER);
    CHECK(cell(COLUMNS - 2, ROWS - 2)->glyph == 0x250c);
    CHECK(cell(COLUMNS - 1, ROWS - 2)->glyph == 0x2500);
    CHECK(cell(COLUMNS - 2, ROWS - 1)->glyph == 0x2502);
    CHECK(cell(COLUMNS - 1, ROWS - 1)->glyph == ' ');
    kt_box(&canvas, (struct kt_rect){3, 3, 0, 4}, &kt_border_light, KT_BORDER);
    kt_box(&canvas, (struct kt_rect){3, 3, 4, 0}, &kt_border_light, KT_BORDER);
    kt_box(&canvas, (struct kt_rect){3, 3, 4, 2}, NULL, KT_BORDER);
    CHECK(cell(3, 3)->glyph == ' ');
    kt_box(&canvas, (struct kt_rect){8, 3, 4, 1}, &kt_border_light, KT_BORDER);
    CHECK(!strcmp(row(3) + 8, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90"));
    CHECK(cell(8, 4)->glyph == ' ');
    {
        static const struct kt_border rails = {{0x2500, 0x2500, 0, 0, 0x250c, 0x2510, 0x2514, 0x2518}, {'-', '-', 0, 0, '+', '+', '+', '+'}};
        fresh(true);
        kt_box(&canvas, (struct kt_rect){0, 0, 4, 3}, &rails, KT_BORDER);
        /* Without sides there is nothing for a corner to join. */
        CHECK(!strcmp(row(0), "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"));
        CHECK(!strcmp(row(1), ""));
        CHECK(!strcmp(row(2), "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"));
    }
}

static void titles(void) {
    const struct kt_rect box = {0, 0, 16, 3};
    fresh(true);
    kt_box(&canvas, box, &kt_border_rounded, KT_BORDER);
    kt_box_title(&canvas, box, "Heads", KT_ALIGN_LEFT, KT_STRONG);
    CHECK(!strncmp(row(0), "\xe2\x95\xad\xe2\x94\x80 Heads \xe2\x94\x80", 16));
    CHECK(kt_style_equal(kt_cell_style(cell(2, 0)), KT_STRONG));
    CHECK(kt_style_equal(kt_cell_style(cell(8, 0)), KT_STRONG));
    CHECK(kt_style_equal(kt_cell_style(cell(1, 0)), KT_BORDER));
    CHECK(kt_style_equal(kt_cell_style(cell(9, 0)), KT_BORDER));
    kt_box(&canvas, box, &kt_border_rounded, KT_BORDER);
    kt_box_title(&canvas, box, "Heads", KT_ALIGN_RIGHT, KT_STRONG);
    CHECK(cell(7, 0)->glyph == ' ');
    CHECK(cell(8, 0)->glyph == 'H');
    CHECK(cell(13, 0)->glyph == ' ');
    CHECK(cell(14, 0)->glyph == 0x2500);
    CHECK(cell(6, 0)->glyph == 0x2500);
    kt_box(&canvas, box, &kt_border_rounded, KT_BORDER);
    kt_box_title(&canvas, box, "Head", KT_ALIGN_CENTER, KT_STRONG);
    CHECK(cell(5, 0)->glyph == ' ');
    CHECK(cell(6, 0)->glyph == 'H');
    CHECK(cell(9, 0)->glyph == 'd');
    CHECK(cell(10, 0)->glyph == ' ');
    CHECK(cell(4, 0)->glyph == 0x2500);
    CHECK(cell(11, 0)->glyph == 0x2500);
    /* Too long for the edge: cut with an ellipsis, corners and one cell of
     * edge on each side kept. */
    kt_box(&canvas, box, &kt_border_rounded, KT_BORDER);
    kt_box_title(&canvas, box, "A much longer title", KT_ALIGN_LEFT, KT_STRONG);
    CHECK(!strcmp(row(0), "\xe2\x95\xad\xe2\x94\x80 A much lo" DOTS " \xe2\x94\x80\xe2\x95\xae"));
    /* The narrowest box that can carry a title shows one column of it. */
    fresh(true);
    kt_box(&canvas, (struct kt_rect){0, 0, 7, 2}, &kt_border_light, KT_BORDER);
    kt_box_title(&canvas, (struct kt_rect){0, 0, 7, 2}, "Heads", KT_ALIGN_LEFT, KT_STRONG);
    CHECK(!strcmp(row(0), "\xe2\x94\x8c\xe2\x94\x80 " DOTS " \xe2\x94\x80\xe2\x94\x90"));
    kt_box(&canvas, (struct kt_rect){0, 2, 6, 2}, &kt_border_light, KT_BORDER);
    kt_box_title(&canvas, (struct kt_rect){0, 2, 6, 2}, "Heads", KT_ALIGN_LEFT, KT_STRONG);
    kt_box_title(&canvas, (struct kt_rect){0, 2, 12, 2}, "", KT_ALIGN_LEFT, KT_STRONG);
    kt_box_title(&canvas, (struct kt_rect){0, 2, 12, 2}, NULL, KT_ALIGN_LEFT, KT_STRONG);
    CHECK(!strcmp(row(2), "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90"));
    fresh(false);
    kt_box(&canvas, (struct kt_rect){0, 0, 12, 2}, &kt_border_double, KT_BORDER);
    kt_box_title(&canvas, (struct kt_rect){0, 0, 12, 2}, "A longer title", KT_ALIGN_LEFT, KT_STRONG);
    CHECK(!strcmp(row(0), "+= A l... =+"));
}

static void rules(void) {
#define LINE "\xe2\x94\x80"
    fresh(true);
    kt_rule(&canvas, (struct kt_rect){1, 0, 12, 1}, "Project", KT_MUTED);
    CHECK(!strcmp(row(0), " Project " LINE LINE LINE LINE));
    CHECK(kt_style_equal(kt_cell_style(cell(1, 0)), KT_MUTED));
    CHECK(kt_style_equal(kt_cell_style(cell(12, 0)), KT_MUTED));
    CHECK(kt_style_equal(kt_cell_style(cell(8, 0)), KT_BASE));
    CHECK(cell(13, 0)->glyph == ' ');
    kt_rule_styled(&canvas, (struct kt_rect){1, 1, 12, 1}, "Log", KT_ALIGN_RIGHT, KT_STRONG, KT_BORDER);
    CHECK(!strcmp(row(1), " " LINE LINE LINE LINE LINE LINE LINE LINE " Log"));
    CHECK(kt_style_equal(kt_cell_style(cell(1, 1)), KT_BORDER));
    CHECK(kt_style_equal(kt_cell_style(cell(10, 1)), KT_STRONG));
    kt_rule_styled(&canvas, (struct kt_rect){1, 2, 12, 1}, "Mid", KT_ALIGN_CENTER, KT_STRONG, KT_BORDER);
    CHECK(!strcmp(row(2), " " LINE LINE LINE " Mid " LINE LINE LINE LINE));
    kt_rule(&canvas, (struct kt_rect){1, 3, 5, 1}, NULL, KT_BORDER);
    CHECK(!strcmp(row(3), " " LINE LINE LINE LINE LINE));
    kt_rule(&canvas, (struct kt_rect){1, 4, 5, 1}, "", KT_BORDER);
    CHECK(!strcmp(row(4), " " LINE LINE LINE LINE LINE));
    /* The title wins over the line; when even it does not fit, it is cut. */
    fresh(true);
    kt_rule(&canvas, (struct kt_rect){0, 0, 8, 1}, "Project", KT_MUTED);
    CHECK(!strcmp(row(0), "Project"));
    kt_rule(&canvas, (struct kt_rect){0, 1, 9, 1}, "Project", KT_MUTED);
    CHECK(!strcmp(row(1), "Project " LINE));
    kt_rule(&canvas, (struct kt_rect){0, 2, 5, 1}, "Project", KT_MUTED);
    CHECK(!strcmp(row(2), "Proj" DOTS));
    kt_rule(&canvas, (struct kt_rect){0, 3, 0, 1}, "Project", KT_MUTED);
    kt_rule(&canvas, (struct kt_rect){0, 9, 5, 1}, "Project", KT_MUTED);
    CHECK(!strcmp(row(3), ""));
    fresh(false);
    kt_rule(&canvas, (struct kt_rect){0, 0, 12, 1}, "Project", KT_MUTED);
    CHECK(!strcmp(row(0), "Project ----"));
#undef LINE
}

/* ---- layout ------------------------------------------------------------- */

static bool same(struct kt_rect r, int x, int y, int width, int height) {
    return r.x == x && r.y == y && r.width == width && r.height == height;
}

static void rectangles(void) {
    const struct kt_rect r = {10, 5, 40, 12};
    struct kt_rect rest, strip;
    CHECK(same(kt_rect_inset(r, 1, 2, 3, 4), 14, 6, 34, 8));
    CHECK(same(kt_rect_pad(r, 2, 1), 12, 6, 36, 10));
    CHECK(same(kt_rect_inset(r, 0, 0, 0, 0), 10, 5, 40, 12));
    /* Insets that meet leave nothing, never less than nothing. */
    CHECK(same(kt_rect_inset(r, 6, 20, 6, 20), 30, 11, 0, 0));
    CHECK(same(kt_rect_inset(r, 100, 0, 0, 100), 50, 17, 0, 0));
    CHECK(same(kt_rect_inset(r, 3, 0, 100, 0), 10, 8, 40, 0));
    CHECK(same(kt_rect_inset(r, -5, -5, -5, -5), 10, 5, 40, 12));
    CHECK(same(kt_rect_pad((struct kt_rect){3, 3, -4, -4}, 1, 1), 3, 3, 0, 0));
    /* Cutting gives both parts. */
    strip = kt_rect_cut(r, KT_SIDE_TOP, 2, &rest);
    CHECK(same(strip, 10, 5, 40, 2));
    CHECK(same(rest, 10, 7, 40, 10));
    strip = kt_rect_cut(r, KT_SIDE_BOTTOM, 2, &rest);
    CHECK(same(strip, 10, 15, 40, 2));
    CHECK(same(rest, 10, 5, 40, 10));
    strip = kt_rect_cut(r, KT_SIDE_LEFT, 15, &rest);
    CHECK(same(strip, 10, 5, 15, 12));
    CHECK(same(rest, 25, 5, 25, 12));
    strip = kt_rect_cut(r, KT_SIDE_RIGHT, 15, &rest);
    CHECK(same(strip, 35, 5, 15, 12));
    CHECK(same(rest, 10, 5, 25, 12));
    /* More than there is takes everything; less than nothing takes nothing. */
    strip = kt_rect_cut(r, KT_SIDE_TOP, 99, &rest);
    CHECK(same(strip, 10, 5, 40, 12));
    CHECK(same(rest, 10, 17, 40, 0));
    strip = kt_rect_cut(r, KT_SIDE_RIGHT, 99, &rest);
    CHECK(same(strip, 10, 5, 40, 12));
    CHECK(same(rest, 10, 5, 0, 12));
    strip = kt_rect_cut(r, KT_SIDE_LEFT, -3, &rest);
    CHECK(same(strip, 10, 5, 0, 12));
    CHECK(same(rest, 10, 5, 40, 12));
    strip = kt_rect_cut(r, KT_SIDE_BOTTOM, 0, NULL);
    CHECK(same(strip, 10, 17, 40, 0));
    /* Centring, clamped to what there is. */
    CHECK(same(kt_rect_center(r, 20, 4), 20, 9, 20, 4));
    CHECK(same(kt_rect_center(r, 21, 5), 19, 8, 21, 5));
    CHECK(same(kt_rect_center(r, 400, 400), 10, 5, 40, 12));
    CHECK(same(kt_rect_center(r, -1, 0), 30, 11, 0, 0));
}

/* One division, checked against the sizes it must give. */
static void split_is(int line, int length, int gap, const struct kt_constraint *parts, size_t count,
                     const int *want, size_t wanted) {
    int got[8];
    size_t i;
    check_at(count == wanted && count <= 8, "one expected size for each part", line);
    kt_split(length, gap, parts, count, got);
    for (i = 0; i < count; i++) check_at(got[i] == want[i], "split sizes", line);
}
#define SPLIT(length, gap, expected, ...) \
    split_is(__LINE__, (length), (gap), (const struct kt_constraint[]){__VA_ARGS__}, \
             sizeof((const struct kt_constraint[]){__VA_ARGS__}) / sizeof(struct kt_constraint), \
             (const int[])expected, sizeof((const int[])expected) / sizeof(int))
#define SIZES(...) {__VA_ARGS__}

static void splits(void) {
    /* Each kind on its own. */
    SPLIT(100, 0, SIZES(20, 80), KT_FIXED(20), KT_FLEX(1));
    SPLIT(100, 0, SIZES(25, 75), KT_PERCENT(25), KT_FLEX(1));
    SPLIT(100, 0, SIZES(20, 20, 60), KT_FIXED(20), KT_FLEX(1), KT_FLEX(3));
    SPLIT(100, 0, SIZES(55, 45), KT_MIN(10), KT_FLEX(1));
    /* A minimum is granted first and then shares like weight one. */
    SPLIT(100, 0, SIZES(25, 43, 32), KT_PERCENT(25), KT_MIN(10), KT_FLEX(1));
    /* Without anything that grows, what is left stays unused at the end. */
    SPLIT(10, 0, SIZES(3, 3), KT_FIXED(3), KT_FIXED(3));
    SPLIT(10, 0, SIZES(5, 2), KT_PERCENT(50), KT_PERCENT(20));
    /* Gaps come out first. */
    SPLIT(10, 1, SIZES(3, 3, 2), KT_FLEX(1), KT_FLEX(1), KT_FLEX(1));
    SPLIT(11, 2, SIZES(4, 5), KT_FIXED(4), KT_FLEX(1));
    /* Cells that do not divide go to the earliest growing parts. */
    SPLIT(11, 0, SIZES(4, 4, 3), KT_FLEX(1), KT_FLEX(1), KT_FLEX(1));
    SPLIT(10, 0, SIZES(4, 6), KT_FLEX(1), KT_FLEX(2));
    SPLIT(7, 0, SIZES(2, 5), KT_FLEX(2), KT_FLEX(5));
    /* Not enough room: earlier parts are served first, later ones starve. */
    SPLIT(10, 0, SIZES(6, 4, 0), KT_FIXED(6), KT_FIXED(6), KT_FLEX(1));
    SPLIT(10, 0, SIZES(10, 0, 0), KT_FIXED(12), KT_MIN(4), KT_FIXED(1));
    SPLIT(10, 0, SIZES(8, 2, 0), KT_MIN(8), KT_MIN(8), KT_MIN(8));
    SPLIT(10, 0, SIZES(10, 0), KT_PERCENT(150), KT_FLEX(1));
    SPLIT(5, 1, SIZES(3, 0, 0), KT_FIXED(4), KT_FIXED(4), KT_FIXED(4));
    /* No room at all, in every way there is to have none. */
    SPLIT(0, 0, SIZES(0, 0, 0), KT_FIXED(4), KT_MIN(2), KT_FLEX(1));
    SPLIT(-7, 0, SIZES(0, 0), KT_FIXED(4), KT_FLEX(1));
    SPLIT(3, 2, SIZES(0, 0, 0), KT_FLEX(1), KT_FLEX(1), KT_FLEX(1));
    SPLIT(4, 2, SIZES(0, 0, 0), KT_FLEX(1), KT_FLEX(1), KT_FLEX(1));
    SPLIT(5, 2, SIZES(1, 0, 0), KT_FLEX(1), KT_FLEX(1), KT_FLEX(1));
    /* Nonsense values count as zero, a zero weight as one. */
    SPLIT(10, -3, SIZES(0, 5, 5), KT_FIXED(-4), KT_FLEX(0), KT_FLEX(-2));
    SPLIT(10, 0, SIZES(0, 10), KT_PERCENT(-50), KT_MIN(-1));
    /* One part, and none. */
    SPLIT(9, 3, SIZES(9), KT_FLEX(1));
    SPLIT(9, 3, SIZES(4), KT_FIXED(4));
    kt_split(10, 0, NULL, 0, NULL);
    {
        int untouched[2] = {-1, -1};
        const struct kt_constraint one[] = {KT_FLEX(1)};
        kt_split(10, 0, one, 0, untouched);
        kt_split(10, 0, NULL, 2, untouched);
        CHECK(untouched[0] == -1);
        CHECK(untouched[1] == -1);
    }
}

/* Three parts in `length` cells with two gaps: no size is negative, the
 * parts never add up to more than there is, and anything that grows uses
 * all of it. */
static void split_within(int length, int gap, const struct kt_constraint *parts, bool grows) {
    int room = length > 2 * gap ? length - 2 * gap : 0, got[3], total;
    kt_split(length, gap, parts, 3, got);
    CHECK(got[0] >= 0);
    CHECK(got[1] >= 0);
    CHECK(got[2] >= 0);
    total = got[0] + got[1] + got[2];
    CHECK(total <= room);
    CHECK(!grows || total == room);
}

/* Every combination of three constraints, at every length around the point
 * where they stop fitting. */
static void split_bounds(void) {
    static const struct kt_constraint menu[7] = {KT_FIXED(7), KT_PERCENT(40), KT_MIN(5), KT_FLEX(1), KT_FLEX(3),
                                                 KT_PERCENT(90), KT_FIXED(0)};
    static const bool grows[7] = {false, false, true, true, true, false, false};
    int length, gap, pick;
    for (length = -2; length < 40; length++) for (gap = 0; gap < 3; gap++) for (pick = 0; pick < 343; pick++) {
        int a = pick % 7, b = pick / 7 % 7, c = pick / 49;
        struct kt_constraint parts[3];
        parts[0] = menu[a]; parts[1] = menu[b]; parts[2] = menu[c];
        split_within(length, gap, parts, grows[a] || grows[b] || grows[c]);
    }
}

static void rect_splits(void) {
    const struct kt_rect r = {2, 1, 30, 10};
    const struct kt_constraint columns[] = {KT_FIXED(8), KT_FLEX(1), KT_PERCENT(25)};
    const struct kt_constraint rows[] = {KT_FIXED(1), KT_FLEX(1), KT_FIXED(2)};
    const struct kt_constraint greedy[] = {KT_FIXED(20), KT_FIXED(20), KT_FLEX(1)};
    struct kt_rect parts[3];
    kt_rect_columns(r, 1, columns, 3, parts);
    /* 28 cells after two gaps: 8, 7 (25% of 28) and the 13 between. */
    CHECK(same(parts[0], 2, 1, 8, 10));
    CHECK(same(parts[1], 11, 1, 13, 10));
    CHECK(same(parts[2], 25, 1, 7, 10));
    kt_rect_rows(r, 0, rows, 3, parts);
    CHECK(same(parts[0], 2, 1, 30, 1));
    CHECK(same(parts[1], 2, 2, 30, 7));
    CHECK(same(parts[2], 2, 9, 30, 2));
    /* A part with no room is empty and sits at the far edge, inside the rectangle. */
    kt_rect_columns(r, 2, greedy, 3, parts);
    CHECK(same(parts[0], 2, 1, 20, 10));
    CHECK(same(parts[1], 24, 1, 6, 10));
    CHECK(same(parts[2], 32, 1, 0, 10));
    kt_rect_columns((struct kt_rect){5, 5, 0, 4}, 1, columns, 3, parts);
    CHECK(same(parts[0], 5, 5, 0, 4));
    CHECK(same(parts[1], 5, 5, 0, 4));
    CHECK(same(parts[2], 5, 5, 0, 4));
    kt_rect_rows((struct kt_rect){5, 5, 4, -3}, 0, rows, 3, parts);
    CHECK(same(parts[0], 5, 5, 4, 0));
    CHECK(same(parts[2], 5, 5, 4, 0));
    kt_rect_columns(r, 1, columns, 0, parts);
    kt_rect_columns(r, 1, columns, 3, NULL);
}

int main(void) {
    widths();
    ellipsis();
    alignment();
    spans();
    fill();
    borders();
    titles();
    rules();
    rectangles();
    splits();
    split_bounds();
    rect_splits();
    puts("draw/layout: text width, ellipsis at every cut around wide and joined cells, alignment, spans, fill, "
         "borders and ASCII fallbacks, titles, rules, rectangle cuts and constraint splits passed");
    return 0;
}
