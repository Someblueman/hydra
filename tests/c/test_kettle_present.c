#include "kettle/kettle.h"
#include <assert.h>
#include <string.h>

/* Everything a presenter wrote to `out` since `from`, as a string. */
static const char *written(FILE *out, long from) {
    static char bytes[2048];
    size_t length;
    assert(fflush(out) == 0 && fseek(out, from, SEEK_SET) == 0);
    length = fread(bytes, 1, sizeof(bytes) - 1, out);
    bytes[length] = '\0';
    assert(fseek(out, 0, SEEK_END) == 0);
    return bytes;
}

/* An overwriting presenter repaints every cell without erasing the display.
 * It owns the rendition: the repaint starts from a reset, each change is
 * written once where it happens, and the frame leaves the default behind. */
static void overwrite(void) {
    const struct kt_paint paint = {&kt_theme_terminal, KT_PROFILE_TRUECOLOR};
    const char expected[] = "\033[1;1H\033[0m A  \033[2;1H  \033[1;36mB\033[0m ";
    struct kt_cell cells[8], previous[8];
    struct kt_canvas canvas;
    struct kt_presenter presenter;
    FILE *out = tmpfile();
    long mark;
    assert(out);
    assert(kt_init(&canvas, cells, 8, 4, 2, true));
    assert(kt_present_init(&presenter, previous, 8) && !presenter.overwrite);
    presenter.overwrite = true;
    kt_put(&canvas, 1, 0, 'A', KT_BASE);
    kt_put(&canvas, 2, 1, 'B', KT_TITLE);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, 0), expected));
    kt_present_invalidate(&presenter);
    mark = ftell(out);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), expected));
    fclose(out);
}

/* Between frames the rendition is the default, so an update writes only the
 * difference from it, and resets only if it changed anything. */
static void pen_between_frames(void) {
    const struct kt_paint paint = {&kt_theme_terminal, KT_PROFILE_TRUECOLOR};
    struct kt_cell cells[8], previous[8];
    struct kt_canvas canvas;
    struct kt_presenter presenter;
    FILE *out = tmpfile();
    long mark;
    assert(out && kt_init(&canvas, cells, 8, 4, 2, true) && kt_present_init(&presenter, previous, 8));
    presenter.overwrite = true;
    assert(kt_present(&presenter, &canvas, out, &paint));
    mark = ftell(out);
    assert(kt_present(&presenter, &canvas, out, &paint) && ftell(out) == mark);
    kt_put(&canvas, 0, 0, 'x', KT_BASE);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), "\033[1;1Hx"));
    mark = ftell(out);
    /* Two runs on one row and a third on the next share one pen: the colour
     * set for the first is not set again, and one reset ends the frame. */
    kt_put(&canvas, 0, 0, 'a', KT_WARNING);
    kt_put(&canvas, 3, 1, 'b', KT_WARNING);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), "\033[1;1H\033[33ma\033[2;4Hb\033[0m"));
    mark = ftell(out);
    /* A changed style is a changed cell even when the glyph is the same. */
    kt_put(&canvas, 0, 0, 'a', KT_SUCCESS);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), "\033[1;1H\033[32ma\033[0m"));
    fclose(out);
}

/* Another theme or profile changes what the same cells look like: the
 * presenter repaints on its own. With no colour to show, text without
 * attributes is written with no escape sequence but its position. */
static void repaint_on_paint_change(void) {
    struct kt_paint paint = {&kt_theme_dark, KT_PROFILE_256};
    struct kt_cell cells[4], previous[4];
    struct kt_canvas canvas;
    struct kt_presenter presenter;
    FILE *out = tmpfile();
    long mark;
    assert(out && kt_init(&canvas, cells, 4, 4, 1, true) && kt_present_init(&presenter, previous, 4));
    presenter.overwrite = true;
    kt_text(&canvas, (struct kt_rect){0, 0, 4, 1}, "ab", KT_BASE);
    kt_text(&canvas, (struct kt_rect){2, 0, 2, 1}, "cd", KT_STRONG);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, 0), "\033[1;1H\033[0;38;5;253;48;5;234mab\033[1mcd\033[0m"));
    mark = ftell(out);
    paint.theme = &kt_theme_light;
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), "\033[1;1H\033[0;38;5;235;48;5;231mab\033[1mcd\033[0m"));
    mark = ftell(out);
    paint.profile = KT_PROFILE_NONE;
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), "\033[1;1Hab\033[1mcd\033[0m"));
    mark = ftell(out);
    assert(kt_present(&presenter, &canvas, out, &paint) && ftell(out) == mark);
    kt_text(&canvas, (struct kt_rect){0, 0, 4, 1}, "xy", KT_MUTED);
    assert(kt_present(&presenter, &canvas, out, &paint));
    assert(!strcmp(written(out, mark), "\033[1;1H\033[2mxy\033[0m"));
    fclose(out);
}

/* One row as a line of output: from the default rendition and back to it. */
static void rows(void) {
    const struct kt_paint paint = {&kt_theme_terminal, KT_PROFILE_16};
    struct kt_cell cells[6];
    struct kt_canvas canvas;
    FILE *out = tmpfile();
    long mark;
    assert(out && kt_init(&canvas, cells, 6, 6, 1, true));
    kt_text(&canvas, (struct kt_rect){0, 0, 6, 1}, "ok", KT_SUCCESS);
    kt_text(&canvas, (struct kt_rect){3, 0, 3, 1}, "no", KT_SELECTED);
    assert(kt_write_row(&canvas, 0, out, &paint));
    assert(!strcmp(written(out, 0), "\033[32mok\033[0m \033[7mno\033[0m "));
    mark = ftell(out);
    /* Without a paint a row is its text and nothing else. */
    assert(kt_write_row(&canvas, 0, out, NULL));
    assert(!strcmp(written(out, mark), "ok no "));
    fclose(out);
}

int main(void) {
    struct kt_cell cells[80], previous[80];
    struct kt_canvas canvas;
    struct kt_presenter presenter;
    FILE *out = tmpfile();
    long first, unchanged, changed;
    char bytes[128] = "";
    assert(out);
    assert(kt_init(&canvas, cells, 80, 20, 4, true));
    assert(kt_present_init(&presenter, previous, 80));
    assert(kt_present(&presenter, &canvas, out, NULL));
    first = ftell(out);
    assert(first > 80);
    assert(kt_present(&presenter, &canvas, out, NULL));
    unchanged = ftell(out);
    assert(unchanged == first);
    kt_put(&canvas, 4, 2, 'X', KT_BASE);
    assert(kt_present(&presenter, &canvas, out, NULL));
    changed = ftell(out);
    assert(changed - unchanged == 7);
    assert(fseek(out, unchanged, SEEK_SET) == 0);
    assert(fread(bytes, 1, 7, out) == 7);
    assert(!memcmp(bytes, "\033[3;5HX", 7));
    assert(fseek(out, 0, SEEK_END) == 0);
    kt_present_invalidate(&presenter);
    assert(kt_present(&presenter, &canvas, out, NULL));
    assert(ftell(out) - changed == first);
    assert(kt_init(&canvas, cells, 80, 10, 4, true));
    assert(kt_present(&presenter, &canvas, out, NULL));
    assert(presenter.width == 10);
    presenter.capacity = 1;
    assert(!kt_present(&presenter, &canvas, out, NULL));
    fclose(out);
    overwrite();
    pen_between_frames();
    repaint_on_paint_change();
    rows();
    puts("present: unchanged frame 0 bytes; one-cell update 7 bytes; invalidate/resize; overwrite repaint; "
         "pen tracking, theme and profile repaint, plain rows passed");
    return 0;
}
