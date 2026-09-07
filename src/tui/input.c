#define _POSIX_C_SOURCE 200809L
#include "input.h"
#include "actions.h"
#include "adapter.h"
#include "render.h"
#include "selection.h"
#include "terminal.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

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


static bool escape_expired(const struct timespec *started) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec - started->tv_sec > 1 ||
        (now.tv_sec - started->tv_sec == 1 && now.tv_nsec >= started->tv_nsec);
}

static bool csi_final(char ch) {
    return (unsigned char)ch >= 0x40U && (unsigned char)ch <= 0x7eU;
}

static bool discard_complete(struct app *app, char ch) {
    static const char paste_end[] = "\033[201~";
    if (app->input_mode == INPUT_DISCARD_CSI) return csi_final(ch);
    if (ch == paste_end[app->paste_matched]) app->paste_matched++;
    else app->paste_matched = ch == paste_end[0] ? 1U : 0U;
    return app->paste_matched == sizeof(paste_end) - 1U;
}

/* A budget yields to the UI, never turns discarded bytes into key bindings.
 * The caller owns app and retains the terminator match across input batches. */
static void discard_input(struct app *app, char ch) {
    struct timespec started;
    size_t consumed = 0U;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    for (;;) {
        if (discard_complete(app, ch)) {
            app->input_mode = INPUT_KEYS;
            app->paste_matched = 0U;
            return;
        }
        if (++consumed >= 8192U || escape_expired(&started) || read_key(20, &ch) <= 0) return;
    }
}

static void handle_escape(struct app *app) {
    char ch, sequence[64];
    size_t count = 0U, consumed = 0U;
    struct timespec started;
    bool complete = false;
    if (read_key(20, &ch) <= 0) {
        app->view = 0; app->help = false; app->diagnostics = false;
        app->search[0] = '\0'; app->notice[0] = '\0';
        return;
    }
    if (ch != '[') return;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    while (consumed++ < 8192U && read_key(20, &ch) > 0) {
        if (count + 1U < sizeof(sequence)) sequence[count++] = ch;
        if (csi_final(ch)) { complete = true; break; }
        if (escape_expired(&started)) break;
    }
    if (!complete) { app->input_mode = INPUT_DISCARD_CSI; return; }
    if (consumed >= sizeof(sequence)) return;
    sequence[count] = '\0';
    if (sequence[0] == '<') handle_mouse(app, sequence);
    else if (strcmp(sequence, "A") == 0) move_selection(app, -1);
    else if (strcmp(sequence, "B") == 0) move_selection(app, 1);
    else if (strcmp(sequence, "M") == 0) {
        /* Legacy X10 carries three bytes after CSI M; never treat them as keys. */
        for (count = 0U; count < 3U; count++) if (read_key(20, &ch) <= 0) break;
    }
    else if (strcmp(sequence, "200~") == 0) {
        app->input_mode = INPUT_DISCARD_PASTE;
        app->paste_matched = 0U;
        copy_text(app->notice, sizeof(app->notice), "bracketed paste ignored");
        if (read_key(20, &ch) > 0) discard_input(app, ch);
    }
}

static void interactive_prompt(struct app *app, char prefix) {
    char query[TEXT];
    if (prompt_text(app, prefix == '/' ? "Search heads: " : "Action search: ", query, sizeof(query)) != 0) return;
    if (prefix == '/') {
        app->notice[0] = '\0';
        copy_text(app->search, sizeof(app->search), query);
        retarget_selection(app);
    }
    else execute_palette(app, query);
}

static void handle_key(struct app *app, char key) {
    if (app->fleet) {
        switch (key) {
            case 'a': fleet_action(app, true); return;
            case 'c': fleet_action(app, false); return;
            case ':': case 'p': case ' ': case 'A': case 'x': case 'G':
                copy_text(app->notice, sizeof(app->notice), "Fleet: a attach, c interrupt, v views, / search, q quit"); return;
            default: break;
        }
    }
    switch (key) {
        case 'q': app->running = false; break;
        case 'j': move_selection(app, 1); break;
        case 'k': move_selection(app, -1); break;
        case 'v': app->view = (app->view + 1) % 4; app->diagnostics = false; break;
        case '\r': case '\n':
            if (selected_head(app) != NULL) app->view = 1;
            else copy_text(app->notice, sizeof(app->notice), "no matching head selected");
            break;
        case '/': case ':': interactive_prompt(app, key); break;
        case 'p': app->view = 1; app->preview = !app->preview; capture_preview(app); break;
        case 'd': if (app->view != 3) app->view = 1; app->diagnostics = !app->diagnostics; break;
        case ' ': toggle_mark(app); break;
        case 'A': select_all_visible(app); break;
        case 'x': if (app->marked_count > 0U) kill_marked_action(app); break;
        case 'G': if (app->marked_count > 0U) group_marked_action(app); break;
        case 't':
            app->theme = (app->theme + 1) % 3;
            snprintf(app->notice, sizeof(app->notice), "Theme: %s%s", theme_name(app->theme), app->no_color ? " (NO_COLOR)" : "");
            break;
        case '?': app->help = !app->help; break;
        case 27: handle_escape(app); break;
        default: break;
    }
}

static void handle_input(struct app *app, char key) {
    if (app->input_mode == INPUT_KEYS) handle_key(app, key);
    else discard_input(app, key);
}

int interactive_main(struct app *app) {
    time_t last_refresh;
    char key;
    const char *term = getenv("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("hydra-tui requires interactive stdin/stdout; use --headless-fixture for deterministic rendering\n", stderr);
        return 3;
    }
    if (term == NULL || strcmp(term, "dumb") == 0) {
        fputs("hydra-tui cannot use TERM=dumb; run hydra tui --basic\n", stderr);
        return 3;
    }
    update_size(app);
    if (app->cols < 40 || app->rows < 10) {
        fputs("hydra-tui requires at least 40 columns by 10 rows; run hydra tui --basic\n", stderr);
        return 3;
    }
    terminal_watch(app);
    if (refresh_model(app) != 0 && app->model.head_count == 0U) return 4;

    refresh_current_session(app);
    if (enter_raw(app) != 0) return 4;
    capture_preview(app);
    last_refresh = time(NULL);
    app->running = true;
    while (app->running && !terminal_stopped()) {
        update_size(app);
        render(app, 0U, false);
        if (read_key(500, &key) <= 0) {
            time_t now = time(NULL);
            if (now - last_refresh >= 2) {
                (void)refresh_model(app);
                refresh_current_session(app);
                capture_preview(app);
                last_refresh = now;
            }
            continue;
        }
        handle_input(app, key);
    }
    restore_terminal(app);
    return terminal_exit_status();
}
