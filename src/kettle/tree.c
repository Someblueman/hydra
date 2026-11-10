#include "tree.h"

static bool visible(const struct kt_tree *t, size_t target) {
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

static bool children(const struct kt_tree *t, size_t node) {
    return node + 1 < t->count && t->nodes[node+1].depth > t->nodes[node].depth;
}

bool kt_tree_init(struct kt_tree *t, struct kt_tree_node *nodes, size_t count) {
    size_t i;
    if (!t || (count && !nodes) || count > KT_TREE_MAX_NODES) return false;
    for (i = 0; i < count; i++) if (nodes[i].depth > 32 || (!i && nodes[i].depth) ||
        (i && nodes[i].depth > nodes[i-1].depth + 1)) return false;
    t->nodes = nodes; t->count = count; t->selected = 0;
    return true;
}

void kt_tree_move(struct kt_tree *t, int direction) {
    size_t next = t->selected;
    if (!t->count) return;
    if (next >= t->count) next = t->count - 1;
    for (;;) {
        if (direction < 0) { if (!next) return; next--; }
        else { if (next + 1 == t->count) return; next++; }
        if (visible(t, next)) { t->selected = next; return; }
    }
}

void kt_tree_expand(struct kt_tree *t, bool expand) {
    size_t selected = t->selected;
    if (selected >= t->count) return;
    if (children(t, selected) && t->nodes[selected].expanded == expand) {
        if (expand) kt_tree_move(t, 1);
    } else if (children(t, selected) && (expand || t->nodes[selected].expanded)) t->nodes[selected].expanded = expand;
    else if (!expand && t->nodes[selected].depth) {
        while (selected && t->nodes[--selected].depth >= t->nodes[t->selected].depth) { }
        t->selected = selected;
    } else if (expand && children(t, selected)) kt_tree_move(t, 1);
}

/* The row drawn as selected: a selection hidden by a collapsed ancestor
 * shows as the nearest visible row before it. */
static size_t shown_selection(const struct kt_tree *t) {
    size_t selected = t->selected < t->count ? t->selected : 0;
    while (selected && !visible(t, selected)) selected--;
    return selected;
}

size_t kt_tree_scroll(const struct kt_tree *t, size_t scroll, int height) {
    size_t i, chosen = 0, selected;
    if (!t->count || height < 1) return scroll;
    selected = shown_selection(t);
    for (i = 0; i < selected; i++) if (visible(t, i)) chosen++;
    if (scroll > chosen) return chosen;
    return chosen >= scroll + (size_t)height ? chosen - (size_t)height + 1 : scroll;
}

void kt_tree_draw(struct kt_tree *t, struct kt_canvas *c, size_t *scroll, bool focused) {
    size_t i, ordinal = 0;
    int row = 0;
    if (!t->count) { kt_text(c, (struct kt_rect){0,0,c->width,1}, "No entries", KT_BORDER); return; }
    t->selected = shown_selection(t);
    *scroll = kt_tree_scroll(t, *scroll, c->height);
    for (i = 0; i < t->count && row < c->height; i++) {
        struct kt_tree_node *n = &t->nodes[i];
        struct kt_style tone = i == t->selected && focused ? KT_SELECTED : n->style;
        int indent = 1 + (int)n->depth * 2, x;
        if (!visible(t, i) || ordinal++ < *scroll) continue;
        for (x = 0; x < c->width; x++) kt_put(c, x, row, ' ', tone);
        if (i == t->selected) kt_put(c, 0, row, focused ? '>' : '*', tone);
        kt_put(c, indent, row, children(t, i) ? (n->expanded ? '-' : '+') : (c->unicode ? 0x2514 : '>'), tone);
        kt_text(c, (struct kt_rect){indent+2,row,c->width-indent-2,1}, n->label, tone);
        row++;
    }
}

bool kt_tree_click(struct kt_tree *t, size_t scroll, int row) {
    size_t i, ordinal = 0;
    if (row < 0 || scroll > KT_TREE_MAX_NODES) return false;
    for (i = 0; i < t->count; i++) if (visible(t, i)) {
        if (ordinal++ == scroll + (size_t)row) { t->selected = i; return true; }
    }
    return false;
}
