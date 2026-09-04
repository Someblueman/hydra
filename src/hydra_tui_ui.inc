/* Terminal control, rendering, palette actions, and interactive input. */

static void write_terminal(const char *data, size_t length) {
    size_t written = 0U;
    while (written < length) {
        ssize_t result = write(STDOUT_FILENO, data + written, length - written);
        if (result > 0) {
            written += (size_t)result;
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static void restore_terminal(struct app *app) {
    if (app != NULL && app->raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &app->saved);
        app->raw = false;
    }
    if (isatty(STDOUT_FILENO)) {
        static const char reset[] = "\033[0m\033[?25h\n";
        write_terminal(reset, sizeof(reset) - 1U);
    }
}

static void cleanup_terminal(void) { restore_terminal(active_app); }

static void signal_handler(int signal_number) {
    stop_signal = signal_number;
    stop_requested = 1;
}

static int enter_raw(struct app *app) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &app->saved) != 0) return -1;
    raw = app->saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    app->raw = true;
    write_terminal("\033[?25l", 6U);
    return 0;
}

static void update_size(struct app *app) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
        app->cols = size.ws_col;
        app->rows = size.ws_row;
    }
}

static void safe_print(const char *text, int width) {
    const unsigned char *cursor = (const unsigned char *)(text == NULL ? "" : text);
    int used = 0;
    while (*cursor != '\0' && used < width) {
        unsigned char ch = *cursor++;
        if (ch == '\t') ch = ' ';
        if (ch < 0x20U || ch == 0x7fU) ch = '?';
        if (ch >= 0x80U) ch = '?';
        putchar((int)ch);
        used++;
    }
}

static void linef(struct app *app, const char *format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    safe_print(buffer, app->cols > 1 ? app->cols - 1 : 1);
    putchar('\n');
}

static const char *display_status(const struct head *head) {
    if (strcmp(head->observed, "unavailable") == 0 || strcmp(head->liveness, "unavailable") == 0) return "UNAVAILABLE";
    if (strcmp(head->liveness, "stopped") == 0 &&
        (strcmp(head->observed, "running") == 0 || strcmp(head->observed, "idle") == 0)) return "STALE";
    if (strcmp(head->liveness, "live") == 0) return "LIVE";
    return "STOPPED";
}

static bool contains_folded(const char *text, const char *search) {
    size_t length = strlen(search);
    if (length == 0U) return true;
    while (*text != '\0') {
        if (strncasecmp(text, search, length) == 0) return true;
        text++;
    }
    return false;
}

static bool head_matches(const struct head *head, const char *search) {
    if (search[0] == '\0') return true;
    return contains_folded(head->branch, search) || contains_folded(head->session, search) ||
           contains_folded(head->group, search) || contains_folded(head->profile, search);
}

static struct head *selected_head(struct app *app) {
    if (app->selected >= app->model.head_count) return NULL;
    if (!head_matches(&app->model.heads[app->selected], app->search)) return NULL;
    return &app->model.heads[app->selected];
}

static void retarget_selection(struct app *app) {
    size_t index;
    if (selected_head(app) != NULL) return;
    for (index = 0U; index < app->model.head_count; index++) {
        if (head_matches(&app->model.heads[index], app->search)) {
            app->selected = index;
            return;
        }
    }
    app->selected = app->model.head_count;
}

static void move_selection(struct app *app, int direction) {
    size_t index;
    retarget_selection(app);
    if (selected_head(app) == NULL) return;
    if (direction > 0) {
        for (index = app->selected + 1U; index < app->model.head_count; index++) {
            if (head_matches(&app->model.heads[index], app->search)) {
                app->selected = index;
                return;
            }
        }
    } else {
        index = app->selected;
        while (index > 0U) {
            index--;
            if (head_matches(&app->model.heads[index], app->search)) {
                app->selected = index;
                return;
            }
        }
    }
}

static size_t marked_index(const struct app *app, const char *branch) {
    size_t index;
    for (index = 0U; index < app->marked_count; index++) {
        if (strcmp(app->marked[index], branch) == 0) return index;
    }
    return MAX_HEADS;
}

static void toggle_mark(struct app *app) {
    struct head *head = selected_head(app);
    size_t index;
    if (head == NULL) return;
    index = marked_index(app, head->branch);
    if (index < app->marked_count) {
        app->marked_count--;
        if (index < app->marked_count) {
            memmove(app->marked[index], app->marked[index + 1U],
                    (app->marked_count - index) * sizeof(app->marked[0]));
        }
    } else if (app->marked_count < MAX_HEADS) {
        copy_text(app->marked[app->marked_count++], TEXT, head->branch);
    }
}

static void select_all_visible(struct app *app) {
    size_t index;
    app->marked_count = 0U;
    for (index = 0U; index < app->model.head_count; index++) {
        if (head_matches(&app->model.heads[index], app->search)) {
            copy_text(app->marked[app->marked_count++], TEXT, app->model.heads[index].branch);
        }
    }
}

static const struct head *head_for_branch(const struct app *app, const char *branch) {
    size_t index;
    for (index = 0U; index < app->model.head_count; index++) {
        if (strcmp(app->model.heads[index].branch, branch) == 0) return &app->model.heads[index];
    }
    return NULL;
}

static void render_list(struct app *app) {
    size_t index;
    size_t visible = 0U;
    int available = app->rows - 7;
    retarget_selection(app);
    if (app->cols < 80) linef(app, "HEADS   status       branch");
    else linef(app, "HEADS   status      declared       observed/confidence      branch");
    for (index = 0U; index < app->model.head_count && available > 0; index++) {
        const struct head *head = &app->model.heads[index];
        if (!head_matches(head, app->search)) continue;
        if (app->cols < 80) {
            linef(app, "%c%c %-12s %s", index == app->selected ? '>' : ' ',
                  marked_index(app, head->branch) < app->marked_count ? '*' : ' ',
                  display_status(head), head->branch);
        } else {
            linef(app, "%c%c %-11s %-14s %-11s/%-11s %s",
                  index == app->selected ? '>' : ' ',
                  marked_index(app, head->branch) < app->marked_count ? '*' : ' ',
                  display_status(head),
                  head->declared[0] == '\0' ? "none" : head->declared,
                  head->observed, head->confidence, head->branch);
        }
        visible++;
        available--;
    }
    if (app->model.head_count == 0U) linef(app, "  No Hydra heads. Open the palette (:) to spawn one.");
    else if (visible == 0U) linef(app, "  No heads match the current search.");
}

static void render_detail(struct app *app) {
    const struct head *head = selected_head(app);
    if (head == NULL) { linef(app, "No selected head matching the current search."); return; }
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
    if (app->preview) {
        char preview[sizeof(app->preview_text)];
        char *line, *save = NULL;
        int remaining = app->rows - 15;
        copy_text(preview, sizeof(preview), app->preview_text[0] == '\0' ? "preview unavailable" : app->preview_text);
        linef(app, "PANE PREVIEW (untrusted controls stripped)");
        line = strtok_r(preview, "\r\n", &save);
        while (line != NULL && remaining-- > 0) {
            linef(app, "  %s", line);
            line = strtok_r(NULL, "\r\n", &save);
        }
    }
}

static void render_coordination(struct app *app) {
    size_t index;
    int available = app->rows - 7;
    linef(app, "COORDINATION  claims scopes queue resources diff gates  branch");
    for (index = 0U; index < app->model.head_count && available > 0; index++) {
        const struct head *head = &app->model.heads[index];
        linef(app, "%c %6u %6u %5u %9u %4u %u/%u  %s", index == app->selected ? '>' : ' ',
              head->claims, head->scopes, head->queue, head->resources, head->diff,
              head->approved, head->gates, head->branch);
        available--;
    }
    linef(app, "Use : to inspect claims, collisions, scopes, queue, resources, diffs, or approvals.");
    if (selected_head(app) != NULL) {
        const struct head *head = selected_head(app);
        linef(app, "selected sources (exact): lifecycle/events/messages/scopes/gates under %s", head->source);
        linef(app, "inspect claims: hydra claim list");
        linef(app, "inspect scopes: hydra scope show %s", head->branch);
        linef(app, "inspect gates: hydra gate status %s", head->branch);
        linef(app, "inspect queue: hydra queue");
        linef(app, "inspect resources: hydra resource status %s", head->branch);
        linef(app, "inspect diff: hydra diff %s", head->branch);
    }
}

static void render_recovery(struct app *app) {
    size_t index;
    int available = app->rows - 7;
    linef(app, "RECOVERY BOARD  kind / label / confidence / inspectable source");
    for (index = 0U; index < app->model.recovery_count && available > 0; index++) {
        const struct recovery *item = &app->model.recovery[index];
        linef(app, "  %-23s %-20s %-8s %s", item->kind, item->label, item->confidence, item->source);
        available--;
    }
    if (app->model.recovery_count == 0U) linef(app, "  No stale locks, dead sessions, orphan worktrees, or interrupted transitions detected.");
    linef(app, "Recovery actions are suggestions only; open : and inspect before mutation.");
}

static void render_help(struct app *app) {
    linef(app, "KEYBOARD HELP");
    linef(app, "  j / k, arrows  move through heads");
    linef(app, "  Enter          open selected-head detail");
    linef(app, "  v              cycle heads, detail, coordination, recovery");
    linef(app, "  /              search branch, session, group, or profile");
    linef(app, "  :              search explicit local actions");
    linef(app, "  p              toggle sanitized pane preview");
    linef(app, "  Space / A      toggle selection / select all visible heads");
    linef(app, "  G              assign selected heads to a group");
    linef(app, "  x              kill selected heads (current session is skipped)");
    linef(app, "  ?              close this help");
    linef(app, "  q              exit and restore terminal state");
    linef(app, "Actions include spawn, switch, kill, regenerate, dashboard, status,");
    linef(app, "claims, collisions, scopes, queue, resources, git diff, approvals,");
    linef(app, "and recovery inspection. Mutations run through explicit shell CLI argv.");
}

static void render(struct app *app, unsigned frame, bool headless) {
    if (headless) linef(app, "FRAME %u %dx%d", frame, app->cols, app->rows);
    else printf("\033[H\033[2J");
    linef(app, "HYDRA MISSION CONTROL  native %s  bounded-polling  view=%s", HYDRA_TUI_VERSION,
          app->view == 0 ? "heads" : app->view == 1 ? "detail" : app->view == 2 ? "coordination" : "recovery");
    linef(app, "source confidence is explicit; no agent state is inferred");
    if (app->search[0] != '\0') linef(app, "search: %s", app->search);
    if (app->help) render_help(app);
    else if (app->view == 0) render_list(app);
    else if (app->view == 1) render_detail(app);
    else if (app->view == 2) render_coordination(app);
    else render_recovery(app);
    if (app->notice[0] != '\0') linef(app, "notice: %s", app->notice);
    if (app->marked_count > 0U) linef(app, "%zu selected  x kill  G assign group", app->marked_count);
    linef(app, "j/k move  Space mark  A all  x kill  G group  / search  : actions  ? help  q quit");
    fflush(stdout);
}

#include "hydra_tui_actions.inc"

static int read_key(int timeout_ms, char *key) {
    fd_set readfds;
    struct timeval timeout;
    int ready;
    FD_ZERO(&readfds); FD_SET(STDIN_FILENO, &readfds);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    if (ready <= 0) return ready;
    return read(STDIN_FILENO, key, 1U) == 1 ? 1 : -1;
}

static void handle_escape(struct app *app) {
    static const char paste_end[] = "\033[201~";
    char ch, sequence[16];
    size_t count = 0U, matched = 0U, consumed = 0U;
    struct timespec started, now;
    if (read_key(20, &ch) <= 0 || ch != '[') return;
    while (count + 1U < sizeof(sequence) && read_key(20, &ch) > 0) {
        sequence[count++] = ch;
        if ((unsigned char)ch >= 0x40U && (unsigned char)ch <= 0x7eU) break;
    }
    sequence[count] = '\0';
    if (strcmp(sequence, "A") == 0) move_selection(app, -1);
    else if (strcmp(sequence, "B") == 0) move_selection(app, 1);
    else if (strcmp(sequence, "200~") == 0) {
        (void)clock_gettime(CLOCK_MONOTONIC, &started);
        while (consumed++ < 8192U) {
            (void)clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - started.tv_sec >= 1) break;
            if (read_key(20, &ch) <= 0) continue;
            if (ch == paste_end[matched]) matched++;
            else matched = ch == paste_end[0] ? 1U : 0U;
            if (matched == sizeof(paste_end) - 1U) break;
        }
        copy_text(app->notice, sizeof(app->notice), "bracketed paste ignored");
    }
}

static void interactive_prompt(struct app *app, char prefix) {
    char query[TEXT];
    if (prompt_text(app, prefix == '/' ? "Search heads: " : "Action search: ", query, sizeof(query)) != 0) return;
    if (prefix == '/') {
        copy_text(app->search, sizeof(app->search), query);
        retarget_selection(app);
    }
    else execute_palette(app, query);
}

static int interactive_main(struct app *app) {
    time_t last_refresh = 0;
    char key;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("hydra-tui requires interactive stdin/stdout; use --headless-fixture for deterministic rendering\n", stderr);
        return 3;
    }
    if (getenv("TERM") == NULL || strcmp(getenv("TERM"), "dumb") == 0) {
        fputs("hydra-tui cannot use TERM=dumb; run hydra tui --basic\n", stderr);
        return 3;
    }
    update_size(app);
    if (app->cols < 40 || app->rows < 10) {
        fputs("hydra-tui requires at least 40 columns by 10 rows; run hydra tui --basic\n", stderr);
        return 3;
    }
    active_app = app;
    atexit(cleanup_terminal);
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler); signal(SIGHUP, signal_handler);
    signal(SIGPIPE, signal_handler);
    if (refresh_model(app) != 0 && app->model.head_count == 0U) return 4;
    refresh_current_session(app);
    if (enter_raw(app) != 0) return 4;
    app->running = true;
    while (app->running && !stop_requested) {
        time_t now = time(NULL);
        update_size(app);
        if (now - last_refresh >= 2) {
            (void)refresh_model(app);
            refresh_current_session(app);
            capture_preview(app);
            last_refresh = now;
        }
        render(app, 0U, false);
        if (read_key(500, &key) <= 0) continue;
        if (key == 'q') app->running = false;
        else if (key == 'j') move_selection(app, 1);
        else if (key == 'k') move_selection(app, -1);
        else if (key == 'v') app->view = (app->view + 1) % 4;
        else if (key == '\r' || key == '\n') {
            if (selected_head(app) != NULL) app->view = 1;
            else copy_text(app->notice, sizeof(app->notice), "no matching head selected");
        }
        else if (key == '/') interactive_prompt(app, '/');
        else if (key == ':') interactive_prompt(app, ':');
        else if (key == 'p') { app->preview = !app->preview; capture_preview(app); }
        else if (key == ' ') toggle_mark(app);
        else if (key == 'A') select_all_visible(app);
        else if (key == 'x' && app->marked_count > 0U) kill_marked_action(app);
        else if (key == 'G' && app->marked_count > 0U) group_marked_action(app);
        else if (key == '?') app->help = !app->help;
        else if (key == 27) handle_escape(app);
    }
    restore_terminal(app);
    return stop_signal == 0 ? 0 : 128 + stop_signal;
}
