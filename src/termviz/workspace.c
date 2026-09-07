#include "workspace.h"
#include <string.h>

static bool inside(struct tv_rect r, int x, int y) {
    return r.width > 0 && r.height > 0 && x >= r.x && y >= r.y &&
        (int64_t)x < (int64_t)r.x + r.width && (int64_t)y < (int64_t)r.y + r.height;
}

void tv_workspace_init(struct tv_workspace *w, int width, int height) {
    memset(w, 0, sizeof(*w));
    w->count = 1; w->drag = -1;
    w->panes[0].parent = -1;
    w->panes[0].min_width = width < 1 ? 1 : width > 4096 ? 4096 : width;
    w->panes[0].min_height = height < 1 ? 1 : height > 4096 ? 4096 : height;
}

int tv_workspace_split(struct tv_workspace *w, int leaf, enum tv_split axis, int ratio) {
    int first;
    struct tv_pane *p;
    if (!w || leaf < 0 || leaf >= w->count || w->count + 2 > TV_PANES_MAX ||
        w->panes[leaf].split != TV_LEAF || (axis != TV_COLUMNS && axis != TV_ROWS) ||
        ratio < 1 || ratio > 999) return -1;
    first = w->count; w->count += 2; p = &w->panes[leaf];
    w->panes[first] = *p; w->panes[first + 1] = *p;
    w->panes[first].parent = w->panes[first + 1].parent = leaf;
    w->panes[first + 1].scroll = 0;
    p->split = axis; p->ratio = ratio; p->first = first; p->second = first + 1;
    if (w->focus == leaf) w->focus = first;
    return first;
}

static bool contains_focus(const struct tv_workspace *w, int node) {
    int focus = w->focus;
    while (focus >= 0) {
        if (focus == node) return true;
        focus = w->panes[focus].parent;
    }
    return false;
}

static void minima(struct tv_workspace *w, int node) {
    struct tv_pane *p = &w->panes[node], *a, *b;
    if (p->split == TV_LEAF) return;
    minima(w, p->first); minima(w, p->second);
    a = &w->panes[p->first]; b = &w->panes[p->second];
    p->min_width = p->split == TV_COLUMNS ? a->min_width + b->min_width + 1 :
        a->min_width > b->min_width ? a->min_width : b->min_width;
    p->min_height = p->split == TV_ROWS ? a->min_height + b->min_height + 1 :
        a->min_height > b->min_height ? a->min_height : b->min_height;
}

static void arrange(struct tv_workspace *w, int node, struct tv_rect r) {
    struct tv_pane *p = &w->panes[node];
    struct tv_rect a = r, b = r;
    int length, min_a, min_b, cut;
    p->bounds = r;
    if (p->split == TV_LEAF) return;
    length = p->split == TV_COLUMNS ? r.width : r.height;
    min_a = p->split == TV_COLUMNS ? w->panes[p->first].min_width : w->panes[p->first].min_height;
    min_b = p->split == TV_COLUMNS ? w->panes[p->second].min_width : w->panes[p->second].min_height;
    if (length < min_a + min_b + 1) {
        arrange(w, contains_focus(w, p->second) ? p->second : p->first, r);
        return;
    }
    cut = (length - 1) * p->ratio / 1000;
    if (cut < min_a) cut = min_a;
    if (cut > length - 1 - min_b) cut = length - 1 - min_b;
    p->divider = r;
    if (p->split == TV_COLUMNS) {
        a.width = cut; b.x += cut + 1; b.width -= cut + 1;
        p->divider.x += cut; p->divider.width = 1;
    } else {
        a.height = cut; b.y += cut + 1; b.height -= cut + 1;
        p->divider.y += cut; p->divider.height = 1;
    }
    arrange(w, p->first, a); arrange(w, p->second, b);
}

bool tv_workspace_layout(struct tv_workspace *w, struct tv_rect r) {
    int i;
    if (!w || r.x < 0 || r.y < 0 || r.width < 1 || r.height < 1 ||
        (int64_t)r.x + r.width > 4096 || (int64_t)r.y + r.height > 4096) return false;
    w->bounds = r;
    for (i = 0; i < w->count; i++) {
        w->panes[i].bounds = (struct tv_rect){0,0,0,0};
        w->panes[i].divider = (struct tv_rect){0,0,0,0};
    }
    minima(w, w->root); arrange(w, w->root, r);
    return true;
}

void tv_workspace_focus_next(struct tv_workspace *w, int direction) {
    int i;
    for (i = 0; i < w->count; i++) {
        w->focus = (w->focus + (direction < 0 ? w->count - 1 : 1)) % w->count;
        if (w->panes[w->focus].split == TV_LEAF) break;
    }
    (void)tv_workspace_layout(w, w->bounds);
}

bool tv_workspace_press(struct tv_workspace *w, int x, int y) {
    int i;
    w->drag = -1;
    for (i = 0; i < w->count; i++) if (inside(w->panes[i].divider, x, y)) {
        w->drag = i; return true;
    }
    for (i = 0; i < w->count; i++) if (w->panes[i].split == TV_LEAF && inside(w->panes[i].bounds, x, y)) {
        w->focus = i; return true;
    }
    return false;
}

bool tv_workspace_motion(struct tv_workspace *w, int x, int y) {
    struct tv_pane *p;
    int length;
    int64_t offset;
    if (w->drag < 0) return false;
    p = &w->panes[w->drag];
    length = (p->split == TV_COLUMNS ? p->bounds.width : p->bounds.height) - 1;
    if (length < 1) return false;
    offset = p->split == TV_COLUMNS ? (int64_t)x - p->bounds.x : (int64_t)y - p->bounds.y;
    p->ratio = offset <= 0 ? 1 : offset >= length ? 999 : (int)(offset * 1000 / length);
    return tv_workspace_layout(w, w->bounds);
}

void tv_workspace_release(struct tv_workspace *w) { w->drag = -1; }

void tv_workspace_scroll(struct tv_workspace *w, int delta, size_t maximum) {
    size_t *value = &w->panes[w->focus].scroll;
    uint64_t amount = delta < 0 ? (uint64_t)-(int64_t)delta : (uint64_t)delta;
    if (*value > maximum) *value = maximum;
    if (delta < 0) *value = amount > *value ? 0 : *value - (size_t)amount;
    else *value = amount > maximum - *value ? maximum : *value + (size_t)amount;
}
