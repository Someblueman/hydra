#include "termviz/workspace.h"
#include <assert.h>
#include <limits.h>

int main(void) {
    struct tv_workspace w;
    int first, content, old_width;
    tv_workspace_init(&w, 12, 4);
    first = tv_workspace_split(&w, 0, TV_COLUMNS, 250);
    assert(first == 1);
    content = tv_workspace_split(&w, 2, TV_ROWS, 500);
    assert(content == 3);
    assert(tv_workspace_layout(&w, (struct tv_rect){0, 1, 79, 22}));
    assert(w.panes[1].bounds.width >= 12);
    assert(w.panes[3].bounds.height >= 4 && w.panes[4].bounds.height >= 4);
    old_width = w.panes[1].bounds.width;
    assert(tv_workspace_press(&w, w.panes[0].divider.x, 2));
    assert(tv_workspace_motion(&w, 30, 2));
    assert(w.panes[1].bounds.width > old_width);
    tv_workspace_release(&w);
    assert(!tv_workspace_motion(&w, 20, 2));
    assert(tv_workspace_press(&w, 2, 2));
    tv_workspace_scroll(&w, 5, 100);
    tv_workspace_focus_next(&w, 1);
    assert(w.focus == 3);
    tv_workspace_scroll(&w, INT_MAX, 10);
    assert(w.panes[1].scroll == 5 && w.panes[3].scroll == 10);
    tv_workspace_scroll(&w, INT_MIN, 10);
    assert(w.panes[3].scroll == 0);
    assert(tv_workspace_layout(&w, (struct tv_rect){0,0,10,3}));
    assert(w.panes[3].bounds.width == 10 && w.panes[1].bounds.width == 0);
    tv_workspace_focus_next(&w, 1);
    assert(w.focus == 4 && w.panes[4].bounds.height == 3);
    tv_workspace_focus_next(&w, 1);
    assert(w.focus == 1 && w.panes[1].bounds.width == 10);
    assert(!tv_workspace_layout(&w, (struct tv_rect){INT_MAX,0,10,3}));
    puts("workspace: nested minima, drag, focus, independent scroll and tiny fallback passed");
    return 0;
}
