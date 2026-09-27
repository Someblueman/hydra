#include "termviz/termviz.h"
#include <assert.h>
#include <string.h>

/* Joined emoji and VS16 follow tmux 3.5 clustering; format characters vanish. */
static void clusters(void) {
    struct tv_cell cells[24];
    struct tv_canvas c;
    assert(tv_init(&c, cells, 24, 8, 3, true));
    tv_text(&c, (struct tv_rect){0,0,8,1}, "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb\xe2\x80\x8b\xe2\x9d\xa4\xef\xb8\x8fZ", TV_BASE);
    assert(cells[0].glyph == 0x1f468 && cells[0].combining_count == 2 && cells[1].width == 0);
    assert(cells[2].glyph == 0x2764 && cells[2].width == 2 && cells[3].width == 0 && cells[4].glyph == 'Z');
    assert(tv_cell_join(NULL, 0x301) == TV_JOIN_DROP && tv_cell_join(NULL, 27) == TV_JOIN_NONE);
    assert(tv_cell_join(&cells[0], 0x202e) == TV_JOIN_DROP);
}

/* ASCII mode: readable approximations in the same columns, never UTF-8. */
static void ascii(void) {
    struct tv_cell cells[24];
    struct tv_canvas c;
    assert(tv_ascii_fallback(0x2500) == '-' && tv_ascii_fallback(0x2502) == '|' && tv_ascii_fallback(0x256d) == '+');
    assert(tv_ascii_fallback(0x2550) == '=' && tv_ascii_fallback(0x2192) == '>' && tv_ascii_fallback(0x2026) == '.');
    assert(tv_ascii_fallback(0xc0) == 'A' && tv_ascii_fallback(0xe9) == 'e' && tv_ascii_fallback(0xff) == 'y');
    assert(tv_ascii_fallback(0xf7) == '/' && tv_ascii_fallback(0x754c) == '?' && tv_ascii_fallback('q') == 'q');
    assert(tv_init(&c, cells, 24, 8, 3, false));
    tv_text(&c, (struct tv_rect){0,0,8,1}, "\xe2\x94\x8c\xe7\x95\x8c\xc3\xa9" "e\xcc\x81!", TV_BASE);
    assert(cells[0].glyph == '+' && cells[1].glyph == '?' && cells[2].glyph == ' ' && cells[2].width == 1);
    assert(cells[3].glyph == 'e' && cells[4].glyph == 'e' && cells[4].combining_count == 0 && cells[5].glyph == '!');
}

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
    clusters();
    ascii();
    fclose(out);
    puts("unicode: emoji clusters, ASCII approximations, wide/combining, malformed UTF-8, clipped surfaces and overwrite damage passed");
    return 0;
}
