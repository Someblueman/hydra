/* Bounded terminal views. Diagnostics are an explicit detail mode. */
static void style(const struct app *app, const char *sequence) {
    if (app->paint) fputs(sequence, stdout);
}

/* Draw the border outside the content padding; title is sanitized by linef. */
static void panel_edge(struct app *app, const char *title) {
    char border[2048];
    int width = app->cols - 1;
    bool boxed = app->boxed;
    if (width < 2) return;
    if (width >= (int)sizeof(border)) width = (int)sizeof(border) - 1;
    memset(border, '-', (size_t)width);
    border[0] = '+'; border[width - 1] = '+'; border[width] = '\0';
    if (title != NULL && width > 8) {
        size_t length = strlen(title);
        if (length > (size_t)width - 6U) length = (size_t)width - 6U;
        border[2] = ' ';
        memcpy(border + 3, title, length);
        border[3 + length] = ' ';
    }
    app->boxed = false;
    style(app, "\033[36m");
    linef(app, "%s", border);
    style(app, "\033[0m");
    app->boxed = boxed;
}

static void head_row(struct app *app, const struct head *head, bool selected) {
    const char *name = head == NULL ? "HEAD" : head->branch;
    const char *status = head == NULL ? (app->fleet ? "DESIRED" : "SESSION") : app->fleet ? head->desired : display_status(head);
    const char *agent = head == NULL ? (app->fleet ? "HOST" : "AGENT") : app->fleet ? head->remote_host : strcmp(head->profile, "none") == 0 ? "shell" : head->profile;
    const char *outcome = head == NULL ? "REPORTED" : head->declared[0] ? head->declared : "-";
    char cursor = selected ? '>' : ' ';
    char mark = head != NULL && marked_index(app, head->branch) < app->marked_count ? '*' : ' ';
    if (head == NULL) style(app, "\033[1m");
    else if (selected) style(app, "\033[1;30;46m");
    else if (!app->fleet && strcmp(status, "LIVE") != 0) style(app, "\033[33m");
    if (app->cols >= 100 && !app->fleet) {
        int width = app->cols - 54;
        linef(app, "%c%c %-*.*s | %-11.11s | %-14.14s | %-12.12s", cursor, mark, width, width, name, status, agent, outcome);
    } else if (app->cols >= 70) {
        int width = app->cols - 39;
        if (head != NULL && app->fleet) name = head->remote_branch;
        linef(app, "%c%c %-*.*s | %-11.11s | %-14.14s", cursor, mark, width, width, name, status, agent);
    } else {
        int width = app->cols - 22;
        linef(app, "%c%c %-*.*s | %-11.11s", cursor, mark, width, width, name, status);
    }
    style(app, "\033[0m");
}

static void detail_pair(struct app *app, const char *left_label, const char *left,
                        const char *right_label, const char *right) {
    if (app->cols < 70) {
        linef(app, "%-10.10s %s", left_label, left);
        linef(app, "%-10.10s %s", right_label, right);
    } else {
        int width = (app->cols - 8) / 2 - 11;
        linef(app, "%-10.10s %-*.*s | %-10.10s %-*.*s", left_label, width, width, left,
              right_label, width, width, right);
    }
}

static void render_list(struct app *app) {
    size_t index, ordinal = 0U, selected = 0U, count = 0U;
    int available = app->limit - app->line - 2;
    size_t start;
    bool summary = app->rows >= 20 && app->cols >= 70;
    if (summary && available > app->rows / 3) available = app->rows / 3;
    retarget_selection(app);
    for (index = 0U; index < app->model.head_count; index++) {
        if (!head_matches(&app->model.heads[index], app->search)) continue;
        if (index == app->selected) selected = count;
        count++;
    }
    if (available < 1) available = 1;
    start = selected >= (size_t)available ? selected - (size_t)available + 1U : 0U;
    head_row(app, NULL, false);
    for (index = 0U; index < app->model.head_count; index++) {
        const struct head *head = &app->model.heads[index];
        if (!head_matches(head, app->search)) continue;
        if (ordinal++ < start) continue;
        if (available-- <= 0) break;
        record_hit(app, index);
        head_row(app, head, index == app->selected);
    }
    if (count == 0U) linef(app, app->search[0] ? "No matching heads. Esc clears search." :
                         app->fleet ? "No remote heads. Check Recovery for host issues." : "No heads yet. Press : and choose spawn.");
    else linef(app, "%zu heads  |  Row %zu of %zu", count, selected + 1U, count);
    if (summary && selected_head(app) != NULL) {
        const struct head *head = selected_head(app);
        linef(app, "");
        panel_edge(app, "SELECTED HEAD");
        style(app, "\033[1m");
        linef(app, "%s", head->branch);
        style(app, "\033[0m");
        if (app->fleet) {
            detail_pair(app, "Host", head->remote_host, "Desired", head->desired);
            linef(app, "a Attach to terminal   c Interrupt   Enter Details");
        } else {
            char changes[24], gates[32];
            snprintf(changes, sizeof(changes), "%u files", head->diff);
            snprintf(gates, sizeof(gates), "%u/%u approved", head->approved, head->gates);
            detail_pair(app, "Agent", strcmp(head->profile, "none") == 0 ? "shell" : head->profile,
                        "Session", display_status(head));
            detail_pair(app, "Changes", changes, "Gates", gates);
            linef(app, "Enter Details   p Terminal output   : Actions");
        }
    }
}

static void render_diagnostics(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { linef(app, "No selected head matching the current search."); return; }
    if (app->fleet) {
        linef(app, "Host: %s", head->remote_host);
        linef(app, "Branch: %s", head->remote_branch);
        linef(app, "Project: %s", head->remote_project);
        linef(app, "Desired state: %s", head->desired);
        linef(app, "Instance: %s", head->instance);
        linef(app, "Head: %s", head->head_id);
        return;
    }
    linef(app, "HEAD DETAIL  %s", head->branch);
    linef(app, "session: %s   profile: %s   group: %s   PR: %s", head->session, head->profile, head->group, head->pr);
    linef(app, "declared: %s   desired: %s", head->declared[0] == '\0' ? "none" : head->declared, head->desired);
    linef(app, "observed: %s   confidence: %s   liveness: %s   display: %s",
          head->observed, head->confidence, head->liveness, display_status(head));
    linef(app, "events: %u   signals: %u   messages: %u   gates: %u (%u approved)",
          head->events, head->signals, head->messages, head->gates, head->approved);
    linef(app, "claims: %u   scopes: %u   queue: %u   resources: %u   changed files: %u",
          head->claims, head->scopes, head->queue, head->resources, head->diff);
    linef(app, "adapter: %s   confidence: %s", head->adapter, head->adapter_confidence);
    linef(app, "adapter source: %s", head->adapter_source);
    linef(app, "notifications: %u configured; delivery delegated", head->notifications);
    linef(app, "notification source: %s", head->notification_source[0] == '\0' ? "unavailable" : head->notification_source);
    linef(app, "lifecycle source: %s", head->source);
    linef(app, "instance: %s   head: %s", head->instance, head->head_id);
}

static void render_detail(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { linef(app, "No selected head. Esc returns to the list."); return; }
    if (app->diagnostics) { render_diagnostics(app); return; }
    style(app, "\033[1m");
    linef(app, "HEAD DETAIL  %s", head->branch);
    style(app, "\033[0m");
    if (app->fleet) {
        linef(app, "Host: %s  |  Desired state: %s", head->remote_host, head->desired);
        linef(app, "Project: %s", head->remote_project);
        linef(app, "Attach to inspect activity. Interrupt preserves the worktree.");
        return;
    }
    detail_pair(app, "Agent", strcmp(head->profile, "none") == 0 ? "shell" : head->profile, "Session", display_status(head));
    if (head->declared[0] != '\0') linef(app, "Reported outcome: %s", head->declared);
    if ((head->group[0] && strcmp(head->group, "-") != 0) || (head->pr[0] && strcmp(head->pr, "-") != 0))
        detail_pair(app, "Group", head->group, "PR", head->pr);
    panel_edge(app, "WORK SUMMARY");
    linef(app, "Changes     %-6u files", head->diff);
    linef(app, "Messages    %-6u   Gates   %u/%u approved", head->messages, head->approved, head->gates);
    linef(app, "");
    if (app->preview) {
        char preview[sizeof(app->preview_text)];
        char *line, *save = NULL;
        panel_edge(app, "TERMINAL OUTPUT");
        copy_text(preview, sizeof(preview), app->preview_text[0] ? app->preview_text : "No terminal output available.");
        line = strtok_r(preview, "\r\n", &save);
        while (line != NULL && app->line < app->limit) {
            linef(app, "%s", line);
            line = strtok_r(NULL, "\r\n", &save);
        }
    } else linef(app, "p terminal output   : actions   d diagnostics");
}

static void render_coordination(struct app *app) {
    const struct head *head = selected_head(app);
    linef(app, "COORDINATION");
    if (head == NULL) { linef(app, "Select a head to inspect its work."); return; }
    linef(app, "%s", head->branch);
    if (app->fleet) { linef(app, "Inspect remote workflows with hydra fleet workflow."); return; }
    linef(app, "");
    linef(app, "Changes     %u files", head->diff);
    linef(app, "Gates       %u of %u approved", head->approved, head->gates);
    linef(app, "Scope       %u claims, %u scopes", head->claims, head->scopes);
    linef(app, "Work queue  %u entries", head->queue);
    linef(app, "Resources   %u allocated", head->resources);
    linef(app, "");
    linef(app, "Press : to inspect diffs, gates, claims, or resources.");
}

static void render_recovery(struct app *app) {
    size_t index, start;
    int available = app->limit - app->line - 2;
    linef(app, "RECOVERY BOARD  %zu findings", app->model.recovery_count);
    if (app->model.recovery_count == 0U) { linef(app, "No recovery issues found."); return; }
    if (app->recovery_selected >= app->model.recovery_count) app->recovery_selected = 0U;
    if (app->diagnostics) {
        const struct recovery *item = &app->model.recovery[app->recovery_selected];
        linef(app, "%s: %s", item->kind, item->label);
        linef(app, "Source: %s", item->source);
        linef(app, "Confidence: %s", item->confidence);
        linef(app, "Inspect: %s", item->action);
        return;
    }
    if (available < 1) available = 1;
    start = app->recovery_selected >= (size_t)available ? app->recovery_selected - (size_t)available + 1U : 0U;
    for (index = start; index < app->model.recovery_count && available-- > 0; index++) {
        const struct recovery *item = &app->model.recovery[index];
        record_hit(app, index);
        if (index == app->recovery_selected) style(app, "\033[7m");
        linef(app, "%c %-20.20s  %s", index == app->recovery_selected ? '>' : ' ', item->kind, item->label);
        style(app, "\033[0m");
    }
    linef(app, "j/k select  d inspect  |  %zu of %zu", app->recovery_selected + 1U, app->model.recovery_count);
}

static void render_help(struct app *app) {
    linef(app, "KEYBOARD HELP");
    if (app->rows < 20) {
        linef(app, "j/k move  Enter detail  Esc back");
        linef(app, "v views  / search  d diagnostics");
        linef(app, app->fleet ? "a attach  c interrupt" : ": actions  p output  Space/A mark");
        linef(app, app->fleet ? "? close help  q quit" : "G group  x kill  ? close  q quit");
        return;
    }
    linef(app, "j/k or arrows   Move between heads");
    linef(app, "Enter           Open details");
    linef(app, "Esc             Back to heads / clear search");
    linef(app, "v               Next view");
    linef(app, "/               Search heads");
    linef(app, "d               Show / hide diagnostics");
    if (app->fleet) {
        linef(app, "a               Attach to remote terminal");
        linef(app, "c               Interrupt remote head (confirmed)");
    } else {
        linef(app, ":               Find an action (spawn, switch, diff...)");
        linef(app, "p               Show / hide terminal output");
        linef(app, "Space / A       Mark one / all visible heads");
        linef(app, "G / x           Group / kill marked heads");
    }
    linef(app, "?               Close help");
    linef(app, "Mouse           Click rows/tabs; wheel moves selection");
    linef(app, "q               Quit");
}

static void render(struct app *app, unsigned frame, bool headless) {
    static const char *views[] = {"Heads", "Details", "Coordination", "Recovery"};
    app->hit_count = 0U;
    app->hit_cols = app->cols; app->hit_height = app->rows; app->hit_view = app->view;
    app->hit_tabs = app->cols >= 70 && !app->search[0];
    app->paint = !headless && !app->no_color;
    app->line = 0;
    app->limit = app->rows - 3;
    app->boxed = false;
    if (headless) printf("FRAME %u %dx%d\n", frame, app->cols, app->rows);
    else printf("\033[H\033[2J");
    style(app, "\033[1;30;44m");
    linef(app, "%-*s", app->cols - 1, app->fleet ? " HYDRA MISSION CONTROL / Fleet" : " HYDRA MISSION CONTROL");
    style(app, "\033[0m");
    if (app->search[0]) linef(app, "[%s]  Search: %s", views[app->view], app->search);
    else if (app->cols >= 70) linef(app, "%sHeads%s  %sDetails%s  %sCoordination%s  %sRecovery%s   v next",
        app->view == 0 ? "[" : " ", app->view == 0 ? "]" : " ",
        app->view == 1 ? "[" : " ", app->view == 1 ? "]" : " ",
        app->view == 2 ? "[" : " ", app->view == 2 ? "]" : " ",
        app->view == 3 ? "[" : " ", app->view == 3 ? "]" : " ");
    else linef(app, "[%s]  v next view", views[app->view]);
    panel_edge(app, app->help ? "HELP" : app->diagnostics ? "DIAGNOSTICS" : views[app->view]);
    app->boxed = true;
    if (app->help) render_help(app);
    else if (app->view == 0) render_list(app);
    else if (app->view == 1) render_detail(app);
    else if (app->view == 2) render_coordination(app);
    else render_recovery(app);
    while (app->line < app->limit) linef(app, "");
    app->boxed = false;
    app->limit = app->rows;
    panel_edge(app, NULL);
    if (app->marked_count) linef(app, "%zu marked  x kill  G group%s%s", app->marked_count, app->notice[0] ? " | " : "", app->notice);
    else if (app->notice[0]) linef(app, "%s", app->notice);
    else linef(app, app->cols >= 70 ? "Click row to select | Wheel to move | Click tab to change view" : "Wheel moves selection | v views");
    style(app, "\033[1m");
    if (app->cols < 60) linef(app, app->fleet ? "a attach  c interrupt  ? help  q quit" : "Enter open  : actions  ? help  q quit");
    else if (app->view == 1 && !app->fleet) linef(app, "p output  d diagnostics  Esc back  ? help  q quit");
    else linef(app, app->fleet ? "a attach  c interrupt  / search  ? help  q quit" : "Enter details  : actions  / search  ? help  q quit");
    style(app, "\033[0m");
    fflush(stdout);
}
