#include "termviz.h"
#include <string.h>

bool tv_graph_layout(const struct tv_edge *edges, size_t edge_count, size_t node_count,
                     struct tv_graph_layout *out) {
    unsigned indegree[TV_GRAPH_MAX_NODES] = {0}, depth[TV_GRAPH_MAX_NODES] = {0};
    unsigned lanes[TV_GRAPH_MAX_NODES] = {0};
    bool done[TV_GRAPH_MAX_NODES] = {false};
    size_t i, j, next;
    if (!out || node_count > TV_GRAPH_MAX_NODES || edge_count > TV_GRAPH_MAX_EDGES ||
        (edge_count && !edges)) return false;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < edge_count; i++) {
        if (edges[i].from >= node_count || edges[i].to >= node_count) return false;
        indegree[edges[i].to]++;
    }
    for (i = 0; i < node_count; i++) {
        for (next = 0; next < node_count; next++) if (!done[next] && !indegree[next]) break;
        if (next == node_count) return false;
        done[next] = true;
        for (j = 0; j < edge_count; j++) if (edges[j].from == next) {
            size_t to = edges[j].to;
            indegree[to]--;
            if (depth[to] <= depth[next]) depth[to] = depth[next] + 1;
        }
        out->x[next] = (int)depth[next] * 27;
        out->y[next] = (int)lanes[depth[next]]++ * 5;
        if (out->x[next] + 22 > out->width) out->width = out->x[next] + 22;
        if (out->y[next] + 4 > out->height) out->height = out->y[next] + 4;
    }
    return true;
}

static void point(struct tv_canvas *c, struct tv_rect r, int64_t x, int64_t y,
                   uint32_t glyph, enum tv_style style) {
    if (x >= r.x && y >= r.y && x < (int64_t)r.x + r.width && y < (int64_t)r.y + r.height &&
        x >= 0 && x < c->width && y >= 0 && y < c->height) tv_put(c, (int)x, (int)y, glyph, style);
}

enum { GRAPH_UP = 1, GRAPH_DOWN = 2, GRAPH_LEFT = 4, GRAPH_RIGHT = 8 };
struct graph_junction { int64_t x, y; unsigned directions; };

static void junction_add(struct graph_junction *junctions, size_t *count, int64_t x, int64_t y, unsigned directions) {
    size_t i;
    for (i = 0; i < *count; i++)
        if (junctions[i].x == x && junctions[i].y == y) { junctions[i].directions |= directions; return; }
    junctions[*count].x = x; junctions[*count].y = y; junctions[*count].directions = directions;
    (*count)++;
}

/* The light box-drawing glyph joining the given directions; '+' in ASCII. */
static uint32_t junction_glyph(bool unicode, unsigned d) {
    static const uint32_t glyphs[16] = {
        ' ', 0x2502U, 0x2502U, 0x2502U, 0x2500U, 0x2518U, 0x2510U, 0x2524U,
        0x2500U, 0x2514U, 0x250CU, 0x251CU, 0x2500U, 0x2534U, 0x252CU, 0x253CU};
    return unicode ? glyphs[d & 15U] : '+';
}

/* Rounded corners match the panels in Unicode; '+' in ASCII. */
static uint32_t node_corner(bool unicode, bool left, bool top) {
    if (!unicode) return '+';
    return top ? (left ? 0x256DU : 0x256EU) : (left ? 0x2570U : 0x256FU);
}

void tv_graph(struct tv_canvas *c, struct tv_rect r, const struct tv_node *nodes,
              size_t node_count, const struct tv_edge *edges, size_t edge_count,
              const struct tv_graph_layout *layout, int pan_x, int pan_y, size_t selected) {
    struct graph_junction junctions[TV_GRAPH_MAX_EDGES * 2];
    size_t i, junction_count = 0;
    int x, y;
    if (!layout || !nodes || node_count > TV_GRAPH_MAX_NODES || edge_count > TV_GRAPH_MAX_EDGES ||
        (edge_count && !edges) || r.width < 1 || r.height < 1) return;
    for (i = 0; i < edge_count; i++) {
        int64_t sx, sy, tx, ty, bend;
        sx = (int64_t)r.x + layout->x[edges[i].from] + 22 - pan_x;
        sy = (int64_t)r.y + layout->y[edges[i].from] + 1 - pan_y;
        tx = (int64_t)r.x + layout->x[edges[i].to] - 1 - pan_x;
        ty = (int64_t)r.y + layout->y[edges[i].to] + 1 - pan_y;
        bend = sx + 2;
        for (x = 0; x < c->width; x++) {
            if (x >= sx && x <= bend) point(c, r, x, sy, c->unicode ? 0x2500U : '-', TV_BORDER);
            if (x >= bend && x <= tx) point(c, r, x, ty, c->unicode ? 0x2500U : '-', TV_BORDER);
        }
        for (y = 0; y < c->height; y++) if (y >= (sy < ty ? sy : ty) && y <= (sy > ty ? sy : ty))
            point(c, r, bend, y, c->unicode ? 0x2502U : '|', TV_BORDER);
        /* Each edge adds the directions it leaves its bends in; edges that
         * share a bend merge into one junction glyph below. */
        junction_add(junctions, &junction_count, bend, sy, GRAPH_LEFT | (ty == sy ? GRAPH_RIGHT : ty > sy ? GRAPH_DOWN : GRAPH_UP));
        if (ty != sy) junction_add(junctions, &junction_count, bend, ty, GRAPH_RIGHT | (ty > sy ? GRAPH_UP : GRAPH_DOWN));
    }
    for (i = 0; i < junction_count; i++)
        point(c, r, junctions[i].x, junctions[i].y, junction_glyph(c->unicode, junctions[i].directions), TV_BORDER);
    for (i = 0; i < edge_count; i++) {
        int64_t tx = (int64_t)r.x + layout->x[edges[i].to] - 1 - pan_x;
        int64_t ty = (int64_t)r.y + layout->y[edges[i].to] + 1 - pan_y;
        point(c, r, tx, ty, '>', TV_BORDER);
    }
    for (i = 0; i < node_count; i++) {
        int64_t nx = (int64_t)r.x + layout->x[i] - pan_x, ny = (int64_t)r.y + layout->y[i] - pan_y;
        enum tv_style tone = i == selected ? TV_SELECTED : nodes[i].style;
        for (y = 0; y < 4; y++) for (x = 0; x < 22; x++) {
            uint32_t glyph = ' ';
            if (y == 0 || y == 3) glyph = c->unicode ? 0x2500U : '-';
            if (x == 0 || x == 21) glyph = (y == 0 || y == 3) ? node_corner(c->unicode, x == 0, y == 0) : c->unicode ? 0x2502U : '|';
            point(c, r, nx + x, ny + y, glyph, tone);
        }
        for (y = 1; y <= 2; y++) {
            const unsigned char *label = (const unsigned char *)(y == 1 ? nodes[i].label : nodes[i].detail);
            if (!label) continue;
            for (x = 0; x < 18 && label[x]; x++)
                point(c, r, nx + x + 2, ny + y, label[x] >= 32 && label[x] <= 126 ? label[x] : '?', tone);
        }
    }
}
