#include "kettle/input.h"
#include "kettle/tree.h"
#include <assert.h>
#include <string.h>

static struct kt_input input;
static struct kt_event event;
static bool send(const char *bytes) {
    size_t i;
    bool got = false;
    for (i = 0; bytes[i]; i++) got = kt_input_feed(&input, (unsigned char)bytes[i], &event);
    return got;
}

int main(void) {
    struct kt_tree tree;
    struct kt_tree_node nodes[] = {{"Root", 0, 0, true, KT_BASE}, {"Child", 1, 1, true, KT_BASE},
        {"Nested", 2, 2, false, KT_BASE}, {"Other", 3, 0, false, KT_BASE}};
    struct kt_cell storage[40];
    struct kt_canvas c;
    size_t scroll = 0, i;
    kt_input_init(&input);
    assert(!send("\033[<0;"));
    assert(send("12;3M") && event.type == KT_MOUSE && event.x == 11 && event.y == 2);
    assert(send("\033[<32;23;8M") && event.button == 32);
    assert(send("\033[<0;23;8m") && event.release);
    assert(!send("\033[<0;999999999999;8M"));
    assert(send("\033[200~") && event.type == KT_PASTE_BEGIN);
    assert(send("q") && event.type == KT_PASTE);
    assert(send("\033[201~") && event.type == KT_PASTE_END);
    assert(!send("\033[Mqqq")); /* legacy mouse payload cannot inject quit */
    assert(send("x") && event.type == KT_KEY && event.key == 'x');
    assert(!send("\033")); assert(kt_input_flush(&input, &event) && event.key == 27);
    assert(send("\033[A") && event.key == KT_KEY_UP);
    assert(!send("\033["));
    for (i = 0; i < 200; i++) assert(!kt_input_feed(&input, '9', &event));
    assert(!send("M")); assert(send("j") && event.key == 'j');
    assert(kt_tree_init(&tree, nodes, 4));
    kt_tree_expand(&tree, true); assert(tree.selected == 1);
    kt_tree_move(&tree, 1); assert(tree.selected == 2);
    kt_tree_expand(&tree, false); assert(tree.selected == 1);
    kt_tree_expand(&tree, false); assert(!nodes[1].expanded);
    kt_tree_move(&tree, 1); assert(tree.selected == 3);
    assert(kt_init(&c, storage, 40, 20, 2, true));
    kt_tree_draw(&tree, &c, &scroll, true); assert(scroll == 1);
    assert(kt_tree_click(&tree, scroll, 0) && tree.selected == 1);
    nodes[1].depth = 3; assert(!kt_tree_init(&tree, nodes, 4));
    puts("input/tree: fragmented mouse, paste isolation, legacy/overlong input, focus, collapse and selection bounds passed");
    return 0;
}
