#include "termviz/termviz.h"
#include <assert.h>
#include <string.h>

int main(void) {
    struct tv_cell cells[80], previous[80];
    struct tv_canvas canvas;
    struct tv_presenter presenter;
    FILE *out = tmpfile();
    long first, unchanged, changed;
    char bytes[128] = "";
    assert(out);
    assert(tv_init(&canvas, cells, 80, 20, 4, true));
    assert(tv_present_init(&presenter, previous, 80));
    assert(tv_present(&presenter, &canvas, out, NULL, NULL));
    first = ftell(out);
    assert(first > 80);
    assert(tv_present(&presenter, &canvas, out, NULL, NULL));
    unchanged = ftell(out);
    assert(unchanged == first);
    tv_put(&canvas, 4, 2, 'X', TV_BASE);
    assert(tv_present(&presenter, &canvas, out, NULL, NULL));
    changed = ftell(out);
    assert(changed - unchanged == 7);
    assert(fseek(out, unchanged, SEEK_SET) == 0);
    assert(fread(bytes, 1, 7, out) == 7);
    assert(!memcmp(bytes, "\033[3;5HX", 7));
    assert(fseek(out, 0, SEEK_END) == 0);
    tv_present_invalidate(&presenter);
    assert(tv_present(&presenter, &canvas, out, NULL, NULL));
    assert(ftell(out) - changed == first);
    assert(tv_init(&canvas, cells, 80, 10, 4, true));
    assert(tv_present(&presenter, &canvas, out, NULL, NULL));
    assert(presenter.width == 10);
    presenter.capacity = 1;
    assert(!tv_present(&presenter, &canvas, out, NULL, NULL));
    fclose(out);
    puts("present: unchanged frame 0 bytes; one-cell update 7 bytes; invalidate/resize passed");
    return 0;
}
