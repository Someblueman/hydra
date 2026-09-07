#include "termviz/terminal.h"
#include <assert.h>
#include <string.h>

static struct tv_terminal_model t, other;
static struct tv_cell primary[20*8], alternate[20*8], history[20*4];
static struct tv_cell p2[20*8], a2[20*8], h2[20*4];
static void feed(const char *s) { tv_term_feed(&t, s, strlen(s)); }
static uint32_t at(int x, int y) { return (t.alternate_active ? alternate : primary)[y*20+x].glyph; }

int main(void) {
    size_t split, i;
    const char *stream = "\033[2J\033[2;3Hcaf\xc3\xa9\xe7\x95\x8c\033[31m!\033[0m\r\nnext\033[?1049hALT\033[?1049l\033[6n";
    assert(tv_term_init(&t, primary, alternate, 20, 8, history, 4, 10, 4));
    feed("0123456789");
    assert(t.primary.wrap_pending && t.primary.x == 9);
    feed("X"); assert(at(0,1) == 'X');
    feed("\033[2;3H\033[38;2;10;20;30mY\033[0m");
    assert(at(2,1) == 'Y' && primary[22].foreground == 0x0a141e);
    feed("\033[2;3H\033[K"); assert(at(2,1) == ' ' && at(0,1) == 'X');
    feed("\033[?1049hALT\033[?25l");
    assert(t.alternate_active && at(0,0) == 'A' && !t.cursor_visible);
    feed("\033[?1049l"); assert(!t.alternate_active && at(0,1) == 'X' && t.primary.x == 2);
    tv_term_reset(&t);
    feed("one\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix\r\nseven\r\neight\r\nnine");
    assert(t.history_count == 4 && t.history_serial == 5 && at(0,0) == 's');
    {
        struct tv_cell visible[40]; struct tv_canvas c;
        assert(tv_init(&c, visible, 40, 10, 4, true));
        tv_term_draw(&t, &c, 4, false); assert(visible[0].glyph == 't');
        tv_term_draw(&t, &c, 0, false); assert(visible[0].glyph == 's');
    }
    tv_term_reset(&t);
    feed("top\033[2;4r\033[4;1HZ\n");
    assert(at(0,0) == 't' && t.history_count == 0 && at(0,2) == 'Z');
    feed("\033[?6h\033[1;1HO"); assert(at(0,1) == 'O');
    feed("\033[?6l\033[r\033[1;1Habcde\033[1;2H\033[2P");
    assert(at(0,0) == 'a' && at(1,0) == 'd' && at(2,0) == 'e');
    feed("\033[1;2H\033[2@XY"); assert(at(1,0) == 'X' && at(3,0) == 'd');
    tv_term_reset(&t);
    feed("\033]52;c;secret\007safe\033Pignored\033\\!");
    assert(at(0,0) == 's' && at(4,0) == '!' && t.reply_length == 0);
    feed("\033[999999999999999999999999H?"); assert(at(5,0) == '?');
    for (split = 0; split <= strlen(stream); split++) {
        char reply1[64], reply2[64]; size_t n1, n2;
        tv_term_reset(&t);
        assert(tv_term_init(&other, p2, a2, 20, 8, h2, 4, 10, 4));
        feed(stream);
        tv_term_feed(&other, stream, split); tv_term_feed(&other, stream + split, strlen(stream) - split);
        for (i = 0; i < 160; i++) assert(tv_cell_equal(primary+i,p2+i) && tv_cell_equal(alternate+i,a2+i));
        n1 = tv_term_take_reply(&t, reply1, sizeof(reply1)); n2 = tv_term_take_reply(&other, reply2, sizeof(reply2));
        assert(n1 == n2 && !memcmp(reply1,reply2,n1));
    }
    tv_term_reset(&t);
    feed("abc\xe7\x95\x8c\xcc\x81"); assert(primary[3].width == 2 && primary[3].combining_count == 1);
    assert(tv_term_resize(&t,4,3)); assert(primary[3].glyph == ' ');
    assert(tv_term_resize(&t,10,4)); assert(primary[4].glyph == ' ');
    feed("\xf0\x9f"); tv_term_finish(&t); assert(t.utf8_length == 0);
    {
        unsigned seed = 9187;
        for (i = 0; i < 50000; i++) {
            unsigned char b;
            seed = seed * 1664525U + 1013904223U; b = (unsigned char)(seed >> 24);
            tv_term_feed(&t, &b, 1);
            assert(t.primary.x >= 0 && t.primary.x < t.primary.canvas.width);
            assert(t.primary.y >= 0 && t.primary.y < t.primary.canvas.height);
            assert(t.history_count <= 4 && t.reply_length <= sizeof(t.reply));
        }
    }
    puts("terminal: wrapping, RGB, erase, alternate screen, history, margins, edits, hostile sequences, every stream split and bounds passed");
    return 0;
}
