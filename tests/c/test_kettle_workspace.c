#include "kettle/workspace.h"
#include <assert.h>
#include <limits.h>

int main(void) {
    struct kt_workspace w;
    int first, content, old_width;
    kt_workspace_init(&w, 12, 4);
    first = kt_workspace_split(&w, 0, KT_COLUMNS, 250);
    assert(first == 1);
    content = kt_workspace_split(&w, 2, KT_ROWS, 500);
    assert(content == 3);
    assert(kt_workspace_layout(&w, (struct kt_rect){0, 1, 79, 22}));
    assert(w.panes[1].bounds.width >= 12);
    assert(w.panes[3].bounds.height >= 4 && w.panes[4].bounds.height >= 4);
    old_width = w.panes[1].bounds.width;
    assert(kt_workspace_press(&w, w.panes[0].divider.x, 2));
    assert(kt_workspace_motion(&w, 30, 2));
    assert(w.panes[1].bounds.width > old_width);
    kt_workspace_release(&w);
    assert(!kt_workspace_motion(&w, 20, 2));
    assert(kt_workspace_press(&w, 2, 2));
    kt_workspace_scroll(&w, 5, 100);
    kt_workspace_focus_next(&w, 1);
    assert(w.focus == 3);
    kt_workspace_scroll(&w, INT_MAX, 10);
    assert(w.panes[1].scroll == 5 && w.panes[3].scroll == 10);
    kt_workspace_scroll(&w, INT_MIN, 10);
    assert(w.panes[3].scroll == 0);
    assert(kt_workspace_layout(&w, (struct kt_rect){0,0,10,3}));
    assert(w.panes[3].bounds.width == 10 && w.panes[1].bounds.width == 0);
    kt_workspace_focus_next(&w, 1);
    assert(w.focus == 4 && w.panes[4].bounds.height == 3);
    kt_workspace_focus_next(&w, 1);
    assert(w.focus == 1 && w.panes[1].bounds.width == 10);
    assert(!kt_workspace_layout(&w, (struct kt_rect){INT_MAX,0,10,3}));
    puts("workspace: nested minima, drag, focus, independent scroll and tiny fallback passed");
    return 0;
}
