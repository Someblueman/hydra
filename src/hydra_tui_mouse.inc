/* SGR mouse reports borrow the last rendered frame's hit map. No mutations. */
static void handle_mouse(struct app *app, const char *sequence) {
    unsigned values[3] = {0U, 0U, 0U};
    const char *cursor = sequence + 1;
    size_t field, index;
    for (field = 0U; field < 3U; field++) {
        if (*cursor < '0' || *cursor > '9') return;
        while (*cursor >= '0' && *cursor <= '9') {
            values[field] = values[field] * 10U + (unsigned)(*cursor++ - '0');
            if (values[field] > 65535U) return;
        }
        if (field < 2U && *cursor++ != ';') return;
    }
    if (*cursor != 'M' || cursor[1] != '\0') return; /* Release is inert. */
    update_size(app);
    if (app->cols != app->hit_cols || app->rows != app->hit_height ||
        app->view != app->hit_view || app->cols < 40 || app->rows < 10) return;
    if (values[1] == 0U || values[1] >= (unsigned)app->cols ||
        values[2] == 0U || values[2] > (unsigned)app->rows) return;
    if (values[0] == 0U && values[2] == 2U && app->hit_tabs) {
        static const unsigned starts[] = {1U, 10U, 21U, 37U};
        static const unsigned ends[] = {7U, 18U, 34U, 46U};
        for (index = 0U; index < 4U; index++) {
            if (values[1] >= starts[index] && values[1] <= ends[index]) {
                app->view = (int)index; app->help = false; app->diagnostics = false;
                return;
            }
        }
    }
    if (app->help || app->diagnostics || values[1] < 3U ||
        values[1] > (unsigned)(app->cols - 3)) return;
    for (index = 0U; index < app->hit_count; index++) {
        if (values[2] != (unsigned)app->hit_rows[index]) continue;
        if (values[0] == 64U || values[0] == 65U) {
            move_selection(app, values[0] == 64U ? -1 : 1);
        } else if (values[0] == 0U) {
            if (app->view == 0) app->selected = app->hit_items[index];
            else if (app->view == 3) app->recovery_selected = app->hit_items[index];
        }
        return;
    }
}
