#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
void render_workflow_graph(struct app *app) {
    struct workflow_model *m = app->workflows;
    struct tv_cell *cells;
    struct tv_canvas c;
    struct tv_graph_layout layout;
    struct tv_node nodes[TV_GRAPH_MAX_NODES];
    struct tv_edge edges[TV_GRAPH_MAX_EDGES];
    size_t indices[TV_GRAPH_MAX_NODES], n, count, i;
    int width = app->cols - 1, height = app->limit - app->line, row;
    struct tv_rect area;
    if (app->fleet) { linef(app, "Workflow graphs show local recorded runs. Remote graphs are unavailable."); return; }
    if (!m || !m->run_count) {
        linef(app, "%s", app->workflow_error[0] ? app->workflow_error : "No recorded workflow runs in this project.");
        linef(app, "Run a trusted workflow with hydra workflow run <id>."); return;
    }
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (app->workflow_run >= m->run_count) app->workflow_run = 0;
    n = workflow_nodes(m, app->workflow_run, indices);
    if (n > TV_GRAPH_MAX_NODES || !workflow_edges(m, indices, n, edges, &count) || !tv_graph_layout(edges, count, n, &layout)) {
        linef(app, "Recorded graph is invalid; inspect workflow status"); return;
    }
    if (app->workflow_node >= n) app->workflow_node = 0;
    if (width < 60 || height < 10) {
        linef(app, "%s / %s", m->runs[app->workflow_run].name, m->runs[app->workflow_run].state);
        if (n) {
            const struct workflow_node *node = &m->nodes[indices[app->workflow_node]];
            linef(app, "%zu/%zu %s [%s]", app->workflow_node + 1, n, node->id, node->state);
            linef(app, "Requires: %s", node->needs);
            linef(app, "j/k steps / [/] runs / w graph");
        } else linef(app, "No recorded steps");
        return;
    }
    cells = malloc((size_t)width * (size_t)height * sizeof(*cells));
    if (!cells) { linef(app, "Graph allocation unavailable"); return; }
    (void)tv_init(&c, cells, (size_t)width * (size_t)height, width, height, !app->ascii);
    dashboard_text(&c, 1, 0, width - 2, TV_STRONG, "%s / %s / run %zu of %zu", m->runs[app->workflow_run].name,
                   m->runs[app->workflow_run].state, app->workflow_run + 1, m->run_count);
    dashboard_text(&c, 1, 1, width - 2, app->workflow_stale ? TV_WARNING : TV_BORDER,
                   "%s / %zu steps / %zu dependency edges", app->workflow_stale ? "STALE snapshot" : "Recorded state", n, count);
    tv_panel(&c, (struct tv_rect){0, 3, width, height - 6}, "DEPENDENCIES / prerequisite -> dependent");
    area = (struct tv_rect){2, 5, width - 4, height - 9};
    if (area.height < 1) area.height = 1;
    for (i = 0; i < n; i++) {
        struct workflow_node *node = &m->nodes[indices[i]];
        nodes[i] = (struct tv_node){node->id, node->state,
            !strcmp(node->state, "failed") || !strcmp(node->state, "recovery-required") ? TV_WARNING : TV_BASE};
    }
    if (n && app->graph_follow) {
        app->graph_x = layout.x[app->workflow_node] - (area.width - 22) / 2;
        app->graph_y = layout.y[app->workflow_node] - (area.height - 4) / 2;
        if (app->graph_x < 0) app->graph_x = 0;
        if (app->graph_y < 0) app->graph_y = 0;
    }
    tv_graph(&c, area, nodes, n, edges, count, &layout, app->graph_x, app->graph_y, app->workflow_node);
    for (i = 0; i < n && app->hit_count < MAX_HEADS; i++) {
        int x = area.x + layout.x[i] - app->graph_x, y = area.y + layout.y[i] - app->graph_y;
        size_t hit = app->hit_count;
        if (x + 22 <= area.x || x >= area.x + area.width || y + 4 <= area.y || y >= area.y + area.height) continue;
        app->hit_left[hit] = (x < area.x ? area.x : x) + 1;
        app->hit_right[hit] = (x + 21 >= area.x + area.width ? area.x + area.width - 1 : x + 21) + 1;
        app->hit_rows[hit] = app->line + (y < area.y ? area.y : y) + 1;
        app->hit_bottom[hit] = app->line + (y + 3 >= area.y + area.height ? area.y + area.height - 1 : y + 3) + 1;
        app->hit_items[hit] = i; app->hit_count++;
    }
    if (n) {
        const struct workflow_node *node = &m->nodes[indices[app->workflow_node]];
        dashboard_text(&c, 1, height - 3, width - 2, TV_STRONG, "%s [%s] / %s / attempts %u",
                       node->id, node->kind, node->state, node->attempts);
        dashboard_text(&c, 1, height - 2, width - 2, TV_BASE, "Requires: %s", node->needs);
    }
    dashboard_text(&c, 1, height - 1, width - 2, TV_WARNING, "%s", app->workflow_error[0] ? app->workflow_error : m->warning);
    for (row = 0; row < height; row++) {
        (void)tv_write_row(&c, row, stdout, dashboard_style, app); putchar('\n'); app->line++;
    }
    style(app, TONE_BASE); free(cells);
}
