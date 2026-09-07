static void render_hosts(struct app *app) {
    struct tv_canvas c;
    struct tv_cell *cells;
    int width = app->cols - 1, height = app->limit - app->line, row, available, split;
    size_t i, start;
    if (!app->fleet) { linef(app, "Remote host observations are available in hydra fleet tui."); return; }
    if (!app->model.host_count) { linef(app, "No host observations. Check remote configuration and Recovery."); return; }
    if (width > 300) width = 300;
    if (height > 120) height = 120;
    if (width < 38 || height < 6) { linef(app, "More space needed / Esc heads"); return; }
    if (app->host_selected >= app->model.host_count) app->host_selected = 0;
    cells = malloc((size_t)width * (size_t)height * sizeof(*cells));
    if (!cells) { linef(app, "Host view allocation unavailable"); return; }
    (void)tv_init(&c, cells, (size_t)width * (size_t)height, width, height, !app->ascii);
    split = width >= 110 ? width * 3 / 5 : width;
    tv_panel(&c, (struct tv_rect){0, 0, split, height}, "HOSTS / latest bounded list response");
    available = height - 5;
    start = app->host_selected >= (size_t)available ? app->host_selected - (size_t)available + 1 : 0;
    for (i = start, row = 2; i < app->model.host_count && row < height - 3; i++, row++) {
        const struct host_observation *host = &app->model.hosts[i];
        char count[24];
        enum tv_style tone = i == app->host_selected ? TV_SELECTED : !strcmp(host->state, "failed") ? TV_WARNING : TV_BASE;
        if (!strcmp(host->state, "failed")) copy_text(count, sizeof(count), "--");
        else snprintf(count, sizeof(count), "%u", host->heads);
        dashboard_text(&c, 2, row, split - 4, tone, "%c %-*.*s %-9s %s heads", i == app->host_selected ? '>' : ' ',
                       split - 29, split - 29, host->name, host->state, count);
        app->hit_rows[app->hit_count] = app->line + row + 1;
        app->hit_bottom[app->hit_count] = app->line + row + 1;
        app->hit_left[app->hit_count] = 3; app->hit_right[app->hit_count] = split - 2;
        app->hit_items[app->hit_count++] = i;
    }
    dashboard_text(&c, 2, height - 2, split - 4, TV_BORDER, "%zu hosts / Enter filters heads / j k select", app->model.host_count);
    if (split < width) {
        const struct host_observation *host = &app->model.hosts[app->host_selected];
        tv_panel(&c, (struct tv_rect){split + 1, 0, width - split - 1, height}, "HOST DETAIL");
        dashboard_text(&c, split + 3, 2, width - split - 5, TV_STRONG, "%s", host->name);
        dashboard_text(&c, split + 3, 4, width - split - 5, TV_BASE, "List response: %s", host->state);
        dashboard_text(&c, split + 3, 5, width - split - 5, TV_WARNING, "%s", host->error);
        if (height > 10) {
            dashboard_text(&c, split + 3, 7, width - split - 5, TV_BASE, "Process liveness: unobserved");
            dashboard_text(&c, split + 3, 8, width - split - 5, TV_BASE, "CPU / memory / load: unavailable");
            dashboard_text(&c, split + 3, 9, width - split - 5, TV_BASE, "An empty host still has a row.");
        }
    } else {
        dashboard_text(&c, 2, height - 3, split - 4, TV_WARNING, "%s", app->model.hosts[app->host_selected].error);
    }
    for (row = 0; row < height; row++) {
        (void)tv_write_row(&c, row, stdout, dashboard_style, app); putchar('\n'); app->line++;
    }
    style(app, TONE_BASE); free(cells);
}
