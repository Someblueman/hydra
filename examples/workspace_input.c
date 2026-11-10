#include "workspace_demo.h"
#include <stdio.h>
#include <string.h>

static void send_child(struct demo *d, const void *bytes, size_t length) {
    if (!kt_pty_enqueue(&d->child, bytes, length))
        snprintf(d->notice, sizeof(d->notice), "Input not delivered: %s", d->child.finished ? "child exited" : "input queue full");
}

static bool shell_mouse(struct demo *d, const struct kt_event *e) {
    struct kt_rect r = d->workspace.panes[3].bounds;
    char bytes[64];
    int length;
    if (!d->model || !d->model->mouse_mode || !d->model->mouse_sgr || d->scrolling ||
        e->x < r.x + 1 || e->x >= r.x + r.width - 1 || e->y < r.y + 2 || e->y >= r.y + r.height - 1) return false;
    if (e->button == 32 && d->model->mouse_mode == 1000) return true;
    d->workspace.focus = 3;
    length = snprintf(bytes, sizeof(bytes), "\033[<%u;%d;%d%c", e->button, e->x - r.x, e->y - r.y - 1, e->release ? 'm' : 'M');
    if (length > 0 && (size_t)length < sizeof(bytes)) send_child(d, bytes, (size_t)length);
    return true;
}

bool demo_handle(struct demo *d, const struct kt_event *e) {
    struct kt_workspace *w = &d->workspace;
    if (e->type == KT_MOUSE) {
        if (w->drag < 0 && shell_mouse(d, e)) return true;
        if (e->release) kt_workspace_release(w);
        else if (e->button == 0) {
            if (kt_workspace_press(w, e->x, e->y) && w->focus == 1 && w->drag < 0)
                (void)kt_tree_click(&d->tree, w->panes[1].scroll, e->y - w->panes[1].bounds.y - 2);
        }
        else if (e->button == 32) (void)kt_workspace_motion(w, e->x, e->y);
        else if (e->button == 64 || e->button == 65) {
            if (kt_workspace_press(w, e->x, e->y)) {
                bool shell = d->model && w->focus == 3;
                if (w->focus == 1) kt_tree_move(&d->tree, e->button == 64 ? -1 : 1);
                else kt_workspace_scroll(w, (e->button == 64 ? -3 : 3) * (shell ? -1 : 1), shell ? d->model->history_count : 10000);
            }
            kt_workspace_release(w);
        }
        return true;
    }
    if (d->model && w->focus == 3 && e->type != KT_KEY) {
        if (e->type == KT_PASTE || (d->model->bracketed_paste && (e->type == KT_PASTE_BEGIN || e->type == KT_PASTE_END)))
            send_child(d, e->bytes, e->length);
        return true;
    }
    if (e->type != KT_KEY) return true;
    if (d->prefix) {
        d->prefix = false;
        if (kt_event_is(e, 'q', 0)) return false;
        if (kt_event_is(e, KT_KEY_TAB, 0)) kt_workspace_focus_next(w, 1);
        else if (kt_event_is(e, '[', 0)) d->scrolling = true;
        else if (kt_event_is(e, ']', 0)) { d->scrolling = false; w->panes[3].scroll = 0; }
        else if (kt_event_is(e, 'b', KT_MOD_CTRL) && d->model && w->focus == 3) send_child(d, e->bytes, e->length);
        return true;
    }
    if (kt_event_is(e, 'b', KT_MOD_CTRL)) { d->prefix = true; return true; }
    if (d->model && w->focus == 3 && !d->scrolling) {
        const char *arrow = NULL;
        if (d->model->application_cursor && !e->mods) {
            if (e->key == KT_KEY_UP) arrow = "\033OA";
            if (e->key == KT_KEY_DOWN) arrow = "\033OB";
            if (e->key == KT_KEY_RIGHT) arrow = "\033OC";
            if (e->key == KT_KEY_LEFT) arrow = "\033OD";
        }
        w->panes[3].scroll = 0;
        send_child(d, arrow ? arrow : e->bytes, arrow ? 3 : e->length);
        return true;
    }
    if (w->focus == 1) {
        if (kt_event_is(e, 'j', 0) || e->key == KT_KEY_DOWN) kt_tree_move(&d->tree, 1);
        if (kt_event_is(e, 'k', 0) || e->key == KT_KEY_UP) kt_tree_move(&d->tree, -1);
        if (kt_event_is(e, 'h', 0) || e->key == KT_KEY_LEFT) kt_tree_expand(&d->tree, false);
        if (kt_event_is(e, 'l', 0) || e->key == KT_KEY_RIGHT || e->key == KT_KEY_ENTER) kt_tree_expand(&d->tree, true);
    }
    if (kt_event_is(e, 'q', 0) || kt_event_is(e, 'c', KT_MOD_CTRL)) return d->model != NULL;
    if (kt_event_is(e, KT_KEY_TAB, 0)) kt_workspace_focus_next(w, 1);
    {
        bool shell = d->model && w->focus == 3;
        int delta = 0;
        if (kt_event_is(e, 'j', 0) || e->key == KT_KEY_DOWN) delta = 1;
        if (kt_event_is(e, 'k', 0) || e->key == KT_KEY_UP) delta = -1;
        if (e->key == KT_KEY_PAGE_DOWN) delta = 10;
        if (e->key == KT_KEY_PAGE_UP) delta = -10;
        if (w->focus != 1) kt_workspace_scroll(w, shell ? -delta : delta, shell ? d->model->history_count : 10000);
        if (shell && e->key == KT_KEY_ESCAPE) { d->scrolling = false; w->panes[3].scroll = 0; }
    }
    return true;
}
