#include "workspace_demo.h"
#include <stdio.h>

void demo_colors(void *model, FILE *out, enum kt_style style) {
    static const char *palette[] = {"\033[0;38;5;252;48;5;234m", "\033[0;38;5;103;48;5;234m",
        "\033[1;38;5;153;48;5;234m", "\033[0;38;5;16;48;5;147m",
        "\033[0;38;5;215;48;5;234m", "\033[1;38;5;183;48;5;234m"};
    (void)model;
    fputs(palette[style >= KT_BASE && style <= KT_STRONG ? style : KT_BASE], out);
}

void demo_draw(struct kt_canvas *c, const struct demo *d) {
    const struct kt_workspace *w = &d->workspace;
    int i, row;
    char text[200];
    kt_text(c, (struct kt_rect){1,0,c->width-2,1}, "kettle / workspace demo", KT_TITLE);
    for (i = 0; i < w->count; i++) {
        const struct kt_pane *p = &w->panes[i];
        struct kt_canvas view;
        if (p->split != KT_LEAF) {
            for (row = 0; row < p->divider.height; row++) {
                int x;
                for (x = 0; x < p->divider.width; x++)
                    kt_put(c, p->divider.x + x, p->divider.y + row, p->split == KT_COLUMNS ? 0x2502 : 0x2500, KT_BORDER);
            }
            continue;
        }
        if (!kt_canvas_view(&view, c, p->bounds)) continue;
        kt_panel(&view, (struct kt_rect){0,0,view.width,view.height}, i == 1 ? "WORKSPACES" : i == 3 ? (d->model ? "REAL SHELL" : "ACTIVITY") : "INSPECT");
        snprintf(text, sizeof(text), "%s pane %d / offset %zu", i == w->focus ? "FOCUS" : "     ", i, p->scroll);
        kt_text(&view, (struct kt_rect){1,1,view.width-2,1}, text, i == w->focus ? KT_SELECTED : KT_BORDER);
        if (i == 1) {
            /* update settled the scroll; drawing works on copies and changes nothing. */
            struct kt_canvas content;
            struct kt_tree tree = d->tree;
            size_t scroll = p->scroll;
            if (kt_canvas_view(&content, &view, (struct kt_rect){1,2,view.width-2,view.height-3}))
                kt_tree_draw(&tree, &content, &scroll, w->focus == 1);
            continue;
        }
        if (i == 3 && d->model) {
            struct kt_canvas content;
            if (kt_canvas_view(&content, &view, (struct kt_rect){1,2,view.width-2,view.height-3}))
                kt_term_draw(d->model, &content, p->scroll, w->focus == 3 && !d->scrolling);
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
            kt_text(&view, (struct kt_rect){1,row,view.width-2,1}, text, index % 3 ? KT_BASE : KT_STRONG);
        }
    }
    kt_text(c, (struct kt_rect){0,c->height-1,c->width,1}, d->notice[0] ? d->notice : c->width < 60 ? (d->model ? "^B Tab focus ^B q quit ^B [ scroll" : "Tab focus j/k scroll drag q quit") : d->model ? (d->prefix ? "Prefix: Tab focus / q quit / [ scroll / ] live" : "^B Tab focus | ^B q quit | ^B [ scroll | drag split") : "Tab focus | j/k scroll | drag split | q quit", KT_BORDER);
}

