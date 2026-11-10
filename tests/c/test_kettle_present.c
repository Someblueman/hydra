#include "kettle/kettle.h"
#include <assert.h>
#include <string.h>

static void tagged(void *context, FILE *out, enum kt_style style) {
    (void)context;
    fprintf(out, "<%d>", (int)style);
}

/* An overwriting presenter repaints every cell without erasing the display,
 * and styles travel in the same stream as the cells they apply to. */
static void overwrite(void) {
    const char expected[] = "\033[1;1H<0> A  \033[2;1H<0>  <2>B<0> ";
    struct kt_cell cells[8], previous[8];
    struct kt_canvas canvas;
    struct kt_presenter presenter;
    char bytes[64] = "";
    FILE *out = tmpfile();
    assert(out);
    assert(kt_init(&canvas, cells, 8, 4, 2, true));
    assert(kt_present_init(&presenter, previous, 8) && !presenter.overwrite);
    presenter.overwrite = true;
    kt_put(&canvas, 1, 0, 'A', KT_BASE);
    kt_put(&canvas, 2, 1, 'B', KT_TITLE);
    assert(kt_present(&presenter, &canvas, out, tagged, NULL));
    rewind(out);
    assert(fread(bytes, 1, sizeof(bytes) - 1, out) == sizeof(expected) - 1);
    assert(!strcmp(bytes, expected));
    kt_present_invalidate(&presenter);
    assert(kt_present(&presenter, &canvas, out, tagged, NULL));
    assert(ftell(out) == 2 * (long)(sizeof(expected) - 1));
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
    assert(kt_present(&presenter, &canvas, out, NULL, NULL));
    first = ftell(out);
    assert(first > 80);
    assert(kt_present(&presenter, &canvas, out, NULL, NULL));
    unchanged = ftell(out);
    assert(unchanged == first);
    kt_put(&canvas, 4, 2, 'X', KT_BASE);
    assert(kt_present(&presenter, &canvas, out, NULL, NULL));
    changed = ftell(out);
    assert(changed - unchanged == 7);
    assert(fseek(out, unchanged, SEEK_SET) == 0);
    assert(fread(bytes, 1, 7, out) == 7);
    assert(!memcmp(bytes, "\033[3;5HX", 7));
    assert(fseek(out, 0, SEEK_END) == 0);
    kt_present_invalidate(&presenter);
    assert(kt_present(&presenter, &canvas, out, NULL, NULL));
    assert(ftell(out) - changed == first);
    assert(kt_init(&canvas, cells, 80, 10, 4, true));
    assert(kt_present(&presenter, &canvas, out, NULL, NULL));
    assert(presenter.width == 10);
    presenter.capacity = 1;
    assert(!kt_present(&presenter, &canvas, out, NULL, NULL));
    fclose(out);
    overwrite();
    puts("present: unchanged frame 0 bytes; one-cell update 7 bytes; invalidate/resize; overwrite repaint passed");
    return 0;
}
