/* View state is caller-owned and independent of head/graph selection. */
struct statistics_view {
    struct hs_model *model;
    struct hs_filter filter;
    size_t selected, visible[HS_RUNS], count;
    size_t step_scroll;
    int back_view;
    bool stale, detail, graph_open;
    char error[TEXT], selected_id[128];
};

static bool statistics_init(struct app *app) {
    if (!app->statistics) {
        app->statistics = calloc(1, sizeof(*app->statistics));
        if (!app->statistics) return false;
        app->statistics->back_view = 7;
    }
    return true;
}

static void statistics_destroy(struct app *app) {
    if (!app->statistics) return;
    free(app->statistics->model); free(app->statistics); app->statistics = NULL;
}

static void statistics_visible(struct app *app) {
    struct statistics_view *v = app->statistics;
    size_t i, j;
    v->count = 0;
    if (app->fleet) {
        for (i = 0; i < app->model.host_count; i++) {
            const struct host_observation *h = &app->model.hosts[i];
            if (v->filter.query[0] && !strstr(h->name, v->filter.query)) continue;
            if (v->filter.attention && strcmp(h->state, "failed")) continue;
            v->visible[v->count++] = i;
        }
    } else if (v->model) {
        for (i = 0; i < v->model->run_count; i++) if (hs_matches(v->model, i, &v->filter)) {
            j = v->count++;
            while (j && v->model->runs[v->visible[j-1]].created < v->model->runs[i].created) {
                v->visible[j] = v->visible[j-1]; j--;
            }
            v->visible[j] = i;
        }
    }
    for (i = 0; i < v->count; i++) {
        const char *identity = app->fleet ? app->model.hosts[v->visible[i]].name : v->model->runs[v->visible[i]].id;
        if (!strcmp(identity, v->selected_id)) { v->selected = i; break; }
    }
    if (v->selected >= v->count) v->selected = v->count ? v->count - 1 : 0;
    if (v->count) copy_text(v->selected_id, sizeof(v->selected_id), app->fleet ?
        app->model.hosts[v->visible[v->selected]].name : v->model->runs[v->visible[v->selected]].id);
    else v->selected_id[0] = '\0';
}

static int refresh_statistics(struct app *app, const char *fixture) {
    FILE *input;
    struct hs_model *next;
    char notice[TEXT];
    if (!statistics_init(app)) return -1;
    if (app->fleet) { statistics_visible(app); return 0; }
    copy_text(notice, sizeof(notice), app->notice);
    input = fixture ? fopen(fixture, "r") : capture_adapter(app, "workflow", "statistics-data", 2000);
    copy_text(app->notice, sizeof(app->notice), notice);
    next = input ? malloc(sizeof(*next)) : NULL;
    if (!input || !next || !hs_load(input, next)) {
        if (input) fclose(input);
        free(next); app->statistics->stale = true;
        copy_text(app->statistics->error, sizeof(app->statistics->error), "Statistics unavailable; last good sample retained");
        return -1;
    }
    fclose(input); free(app->statistics->model); app->statistics->model = next;
    app->statistics->stale = false; app->statistics->error[0] = '\0';
    statistics_visible(app);
    return 0;
}

static void statistics_move(struct app *app, int direction) {
    struct statistics_view *v = app->statistics;
    if (!v) return;
    if (v->detail) {
        if (direction > 0 && v->step_scroll < HS_STEPS - 1) v->step_scroll++;
        if (direction < 0 && v->step_scroll) v->step_scroll--;
    } else {
        if (direction > 0 && v->selected + 1 < v->count) v->selected++;
        if (direction < 0 && v->selected) v->selected--;
        if (v->count) copy_text(v->selected_id, sizeof(v->selected_id), app->fleet ?
            app->model.hosts[v->visible[v->selected]].name : v->model->runs[v->visible[v->selected]].id);
    }
}
