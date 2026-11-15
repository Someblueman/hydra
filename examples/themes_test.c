/* Tests for themes.c with the headless driver. The application is compiled
 * into this file so its static update/view are reachable. */
#define main themes_main
#include "themes.c"
#undef main
#include "testing.h"
#include <assert.h>

static bool has(const char *text, const char *part) { return strstr(text, part) != NULL; }

/* Cursor addresses in a frame: one for each run of cells it rewrites. */
static size_t addresses(const char *frame) {
    size_t count = 0;
    for (; *frame; frame++) count += *frame == 'H';
    return count;
}

int main(void) {
    struct model model = {0, 0};
    struct kt_options options = {0};
    struct kt_test *test;
    const char *bytes;
    options.theme = &kt_theme_dark;
    test = kt_test_new(&themes_app, &model, 64, 9, &options);
    assert(test);

    /* The view is the same text in every theme and profile. */
    assert(!strcmp(kt_test_row(test, 0), " themes  dark"));
    assert(has(kt_test_row(test, 2), "  Tasks \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"));
    assert(has(kt_test_row(test, 3), "  \xe2\x96\x8c feature-live"));
    /* The long name is cut between cells and ends with an ellipsis; the
     * columns after it stay where they are. */
    assert(has(kt_test_row(test, 5), "    fix-\xe7\x95\x8c\xe9\x9d\xa2-rendering-of-wide-text\xe2\x80\xa6  stopped"));
    assert(has(kt_test_row(test, 5), "  shell"));
    assert(has(kt_test_row(test, 8), " ready   up/down select  t theme  q quit"));

    /* Dark, 24-bit: the surface, the raised panel and the selection are
     * three backgrounds; the selected row keeps its own colours for text
     * drawn over it. */
    bytes = kt_test_output(test, NULL);
    assert(has(bytes, "\033[1;1H\033[0;38;2;217;220;227;48;2;22;24;29m "));
    assert(has(bytes, "48;2;31;35;43m"));
    assert(has(bytes, "48;2;43;53;80m"));

    /* Moving the selection repaints two rows, not the screen. */
    kt_test_type(test, "j");
    assert(model.selected == 1);
    assert(has(kt_test_row(test, 4), "  \xe2\x96\x8c feature-stale") && !has(kt_test_row(test, 3), "\xe2\x96\x8c"));
    bytes = kt_test_output(test, NULL);
    assert(has(bytes, "\033[4;3H") && has(bytes, "\033[5;3H") && addresses(bytes) == 2);

    /* Another theme: every cell again, same text, no erase, and the frame
     * says which theme it is painted with. */
    kt_test_type(test, "t");
    bytes = kt_test_output(test, NULL);
    assert(!strcmp(kt_test_row(test, 0), " themes  light"));
    assert(has(bytes, "\033[1;1H\033[0;38;2;31;36;48;48;2;251;251;252m ") && has(bytes, "\033[9;1H") && !has(bytes, "\033[2J"));
    kt_test_type(test, "t");
    assert(!strcmp(kt_test_row(test, 0), " themes  terminal"));
    /* The terminal's own colours: default background, reverse video selection. */
    bytes = kt_test_output(test, NULL);
    assert(has(bytes, "\033[1;1H\033[0m ") && !has(bytes, "48;") && has(bytes, "\033[7"));
    kt_test_type(test, "q");
    assert(!kt_test_running(test) && kt_test_status(test) == 0);
    kt_test_free(test);

    /* No colour at all: the selection is reverse video and the muted text
     * dim, in a theme that otherwise says both with colour. */
    model.selected = model.theme = 0;
    options.profile = KT_PROFILE_NONE;
    test = kt_test_new(&themes_app, &model, 64, 9, &options);
    assert(test);
    bytes = kt_test_output(test, NULL);
    assert(has(bytes, "\033[7m\xe2\x96\x8c") && has(bytes, "\033[2mdark") && !has(bytes, "38;") && !has(bytes, "48;"));
    kt_test_free(test);

    /* Narrower: the name keeps its minimum and is cut to it. Narrower
     * still: the columns are served in order, and the last one goes. */
    options.profile = KT_PROFILE_AUTO;
    test = kt_test_new(&themes_app, &model, 44, 9, &options);
    assert(test);
    assert(has(kt_test_row(test, 5), "    fix-\xe7\x95\x8c\xe9\x9d\xa2-re\xe2\x80\xa6  stopped           shell"));
    kt_test_free(test);
    test = kt_test_new(&themes_app, &model, 34, 7, &options);
    assert(test);
    assert(!strcmp(kt_test_row(test, 3), "  \xe2\x96\x8c feature-live  running"));
    kt_test_free(test);
    puts("themes: layout, ellipsis, selection, theme changes and monochrome passed under the headless driver");
    return 0;
}
