#include "workspace_demo.h"
#include <stdio.h>

void demo_colors(void *context, enum tv_style style) {
    static const char *palette[] = {"\033[0;38;5;252;48;5;234m", "\033[0;38;5;103;48;5;234m",
        "\033[1;38;5;153;48;5;234m", "\033[0;38;5;16;48;5;147m",
        "\033[0;38;5;215;48;5;234m", "\033[1;38;5;183;48;5;234m"};
    (void)context;
    fputs(palette[style >= TV_BASE && style <= TV_STRONG ? style : TV_BASE], stdout);
}

void demo_draw(struct tv_canvas *c, struct demo *d) {
    struct tv_workspace *w = &d->workspace;
    int i, row;
    char text[200];
    tv_clear(c, TV_BASE);
    tv_text(c, (struct tv_rect){1,0,c->width-2,1}, "termviz / workspace demo", TV_TITLE);
    for (i = 0; i < w->count; i++) {
        struct tv_pane *p = &w->panes[i];
        struct tv_canvas view;
        if (p->split != TV_LEAF) {
            for (row = 0; row < p->divider.height; row++) {
                int x;
                for (x = 0; x < p->divider.width; x++)
                    tv_put(c, p->divider.x + x, p->divider.y + row, p->split == TV_COLUMNS ? 0x2502 : 0x2500, TV_BORDER);
            }
            continue;
        }
        if (!tv_canvas_view(&view, c, p->bounds)) continue;
        tv_panel(&view, (struct tv_rect){0,0,view.width,view.height}, i == 1 ? "WORKSPACES" : i == 3 ? (d->model ? "REAL SHELL" : "ACTIVITY") : "INSPECT");
        snprintf(text, sizeof(text), "%s pane %d / offset %zu", i == w->focus ? "FOCUS" : "     ", i, p->scroll);
        tv_text(&view, (struct tv_rect){1,1,view.width-2,1}, text, i == w->focus ? TV_SELECTED : TV_BORDER);
        if (i == 1) {
            struct tv_canvas content;
            if (tv_canvas_view(&content, &view, (struct tv_rect){1,2,view.width-2,view.height-3}))
                tv_tree_draw(&d->tree, &content, &p->scroll, w->focus == 1);
            continue;
        }
        if (i == 3 && d->model) {
            struct tv_canvas content;
            if (tv_canvas_view(&content, &view, (struct tv_rect){1,2,view.width-2,view.height-3}))
                tv_term_draw(d->model, &content, p->scroll, w->focus == 3 && !d->scrolling);
            continue;
        }
        for (row = 2; row < view.height - 1; row++) {
            size_t index = p->scroll + (size_t)row - 2;
            if (i == 1) snprintf(text, sizeof(text), "%s %s %zu", index % 3 == 0 ? "▸" : " └", index % 3 == 0 ? "project" : "agent", index);
            else if (i == 3) snprintf(text, sizeof(text), "%04zu  composed / café / 界 / é", index);
            else if (d->model) {
                switch (index) {
                case 0: snprintf(text, sizeof(text), "Child PID: %ld", (long)d->child.pid); break;
                case 1: snprintf(text, sizeof(text), "Child: %s / status %d", d->child.finished ? "exited" : "running", d->child.status); break;
                case 2: snprintf(text, sizeof(text), "Terminal: %dx%d", d->model->primary.canvas.width, d->model->primary.canvas.height); break;
                case 3: snprintf(text, sizeof(text), "Screen: %s", d->model->alternate_active ? "alternate" : "primary"); break;
                case 4: snprintf(text, sizeof(text), "History: %zu / %zu rows", d->model->history_count, d->model->history_rows); break;
                case 5: snprintf(text, sizeof(text), "Unsupported: %llu", (unsigned long long)d->model->unsupported); break;
                default: snprintf(text, sizeof(text), "^B [ scroll / ^B ] live"); break;
                }
            } else snprintf(text, sizeof(text), "row %zu / isolated scroll", index);
            tv_text(&view, (struct tv_rect){1,row,view.width-2,1}, text, index % 3 ? TV_BASE : TV_STRONG);
        }
    }
    tv_text(c, (struct tv_rect){0,c->height-1,c->width,1}, d->notice[0] ? d->notice : c->width < 60 ? (d->model ? "^B Tab focus ^B q quit ^B [ scroll" : "Tab focus j/k scroll drag q quit") : d->model ? (d->prefix ? "Prefix: Tab focus / q quit / [ scroll / ] live" : "^B Tab focus | ^B q quit | ^B [ scroll | drag split") : "Tab focus | j/k scroll | drag split | q quit", TV_BORDER);
}

