#include "termviz/termviz.h"
#include <assert.h>
#include <string.h>

int main(void) {
    struct tv_cell cells[24], old[24];
    struct tv_canvas c, view;
    struct tv_presenter p;
    FILE *out = tmpfile();
    long before;
    char buffer[128];
    uint32_t cp;
    assert(out && tv_init(&c, cells, 24, 8, 3, true));
    assert(tv_utf8_decode("\xf0\x9f", 2, &cp) == 0);
    assert(tv_utf8_decode("\xf0\x80\x80\x80", 4, &cp) == 1 && cp == '?');
    assert(tv_codepoint_width(0x754c) == 2);
    assert(tv_codepoint_width(0x301) == 0);
    assert(tv_codepoint_width(0x202e) == -1); /* bidi control */
    tv_text(&c, (struct tv_rect){0,0,8,1}, "caf\xc3\xa9 \xe7\x95\x8c", TV_BASE);
    assert(cells[3].glyph == 0xe9 && cells[5].width == 2 && cells[6].width == 0);
    tv_text(&c, (struct tv_rect){7,0,1,1}, "e\xcc\x81", TV_BASE);
    assert(cells[7].glyph == 'e' && cells[7].combining[0] == 0x301);
    assert(tv_present_init(&p, old, 24));
    assert(tv_present(&p, &c, out, NULL, NULL));
    before = ftell(out);
    tv_put(&c, 6, 0, 'x', TV_SELECTED); /* overwrite wide continuation */
    assert(cells[5].glyph == ' ' && cells[5].width == 1 && cells[6].glyph == 'x');
    assert(tv_present(&p, &c, out, NULL, NULL));
    assert(fseek(out, before, SEEK_SET) == 0);
    memset(buffer, 0, sizeof(buffer));
    assert(fread(buffer, 1, sizeof(buffer)-1, out) > 0);
    assert(strstr(buffer, "\033[1;6H x"));
    assert(tv_canvas_view(&view, &c, (struct tv_rect){2,1,3,2}));
    tv_text(&view, (struct tv_rect){0,0,100,1}, "abcdef", TV_BASE);
    assert(cells[10].glyph == 'a' && cells[12].glyph == 'c' && cells[13].glyph == ' ');
    tv_text(&view, (struct tv_rect){0,1,3,1}, "\xe7\x95\x8cX", TV_BASE);
    assert(cells[18].width == 2 && cells[20].glyph == 'X' && cells[21].glyph == ' ');
    tv_clear(&view, TV_BASE);
    assert(cells[10].glyph == ' ' && cells[18].glyph == ' ' && cells[7].glyph == 'e');
    fclose(out);
    puts("unicode: wide/combining, malformed UTF-8, clipped surfaces and overwrite damage passed");
    return 0;
}
