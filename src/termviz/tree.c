#include "tree.h"

static bool visible(const struct tv_tree *t, size_t target) {
    unsigned depth = t->nodes[target].depth;
    while (target && depth) {
        target--;
        if (t->nodes[target].depth < depth) {
            if (!t->nodes[target].expanded) return false;
            depth = t->nodes[target].depth;
        }
    }
    return true;
}

static bool children(const struct tv_tree *t, size_t node) {
    return node + 1 < t->count && t->nodes[node+1].depth > t->nodes[node].depth;
}

bool tv_tree_init(struct tv_tree *t, struct tv_tree_node *nodes, size_t count) {
    size_t i;
    if (!t || (count && !nodes) || count > TV_TREE_MAX_NODES) return false;
    for (i = 0; i < count; i++) if (nodes[i].depth > 32 || (!i && nodes[i].depth) ||
        (i && nodes[i].depth > nodes[i-1].depth + 1)) return false;
    t->nodes = nodes; t->count = count; t->selected = 0;
    return true;
}

void tv_tree_move(struct tv_tree *t, int direction) {
    size_t next = t->selected;
    if (!t->count) return;
    if (next >= t->count) next = t->count - 1;
    for (;;) {
        if (direction < 0) { if (!next) return; next--; }
        else { if (next + 1 == t->count) return; next++; }
        if (visible(t, next)) { t->selected = next; return; }
    }
}

void tv_tree_expand(struct tv_tree *t, bool expand) {
    size_t selected = t->selected;
    if (selected >= t->count) return;
    if (children(t, selected) && t->nodes[selected].expanded == expand) {
        if (expand) tv_tree_move(t, 1);
    } else if (children(t, selected) && (expand || t->nodes[selected].expanded)) t->nodes[selected].expanded = expand;
    else if (!expand && t->nodes[selected].depth) {
        while (selected && t->nodes[--selected].depth >= t->nodes[t->selected].depth) { }
        t->selected = selected;
    } else if (expand && children(t, selected)) tv_tree_move(t, 1);
}

void tv_tree_draw(struct tv_tree *t, struct tv_canvas *c, size_t *scroll, bool focused) {
    size_t i, ordinal = 0, chosen = 0, total = 0;
    int row = 0;
    if (!t->count) { tv_text(c, (struct tv_rect){0,0,c->width,1}, "No entries", TV_BORDER); return; }
    if (t->selected >= t->count) t->selected = 0;
    while (t->selected && !visible(t, t->selected)) t->selected--;
    for (i = 0; i < t->count; i++) if (visible(t, i)) {
        if (i == t->selected) chosen = total;
        total++;
    }
    if (*scroll > chosen) *scroll = chosen;
    if (chosen >= *scroll + (size_t)c->height) *scroll = chosen - (size_t)c->height + 1;
    for (i = 0; i < t->count && row < c->height; i++) {
        struct tv_tree_node *n = &t->nodes[i];
        enum tv_style tone = i == t->selected && focused ? TV_SELECTED : n->style;
        int indent = 1 + (int)n->depth * 2, x;
        if (!visible(t, i) || ordinal++ < *scroll) continue;
        for (x = 0; x < c->width; x++) tv_put(c, x, row, ' ', tone);
        if (i == t->selected) tv_put(c, 0, row, focused ? '>' : '*', tone);
        tv_put(c, indent, row, children(t, i) ? (n->expanded ? '-' : '+') : (c->unicode ? 0x2514 : '>'), tone);
        tv_text(c, (struct tv_rect){indent+2,row,c->width-indent-2,1}, n->label, tone);
        row++;
    }
}

bool tv_tree_click(struct tv_tree *t, size_t scroll, int row) {
    size_t i, ordinal = 0;
    if (row < 0 || scroll > TV_TREE_MAX_NODES) return false;
    for (i = 0; i < t->count; i++) if (visible(t, i)) {
        if (ordinal++ == scroll + (size_t)row) { t->selected = i; return true; }
    }
    return false;
}
