#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define HYDRA_TUI_VERSION "1.8.0"
#define HYDRA_TUI_PROTOCOL 2
#define MAX_HEADS 512
#define MAX_RECOVERY 512
#define TEXT 256
#define SOURCE_TEXT 768
#define MAX_DATA_BYTES (8U * 1024U * 1024U)

extern char **environ;

struct head {
    char branch[TEXT], session[TEXT], profile[TEXT], group[TEXT], pr[TEXT];
    char status[32], liveness[32], declared[64], observed[64], confidence[64];
    char instance[TEXT], desired[64], source[SOURCE_TEXT], head_id[TEXT];
    char adapter[64], adapter_confidence[64], adapter_source[SOURCE_TEXT];
    char notification_source[SOURCE_TEXT];
    unsigned notifications;
    unsigned events, signals, messages, claims, scopes, queue, resources, diff, gates, approved;
};

struct recovery {
    char kind[64], label[TEXT], source[SOURCE_TEXT], confidence[64], action[TEXT];
};

struct model {
    struct head heads[MAX_HEADS];
    struct recovery recovery[MAX_RECOVERY];
    size_t head_count, recovery_count;
};

struct app {
    struct model model;
    const char *hydra;
    size_t selected;
    int view;
    int rows, cols;
    bool raw, no_color, preview, running;
    char search[TEXT];
    char notice[TEXT];
    char preview_text[4096];
    struct termios saved;
};

static struct app *active_app;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t stop_signal;

static struct head *selected_head(struct app *app);
static void retarget_selection(struct app *app);

static void copy_text(char *dst, size_t size, const char *src) {
    if (size == 0U) return;
    snprintf(dst, size, "%s", src == NULL ? "" : src);
}

static unsigned parse_unsigned(const char *value) {
    char *end = NULL;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(value == NULL ? "" : value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > 1000000UL) return 0U;
    return (unsigned)parsed;
}

static size_t split_fields(char *line, char **fields, size_t capacity) {
    size_t count = 0U;
    char *cursor = line;
    if (capacity == 0U) return 0U;
    fields[count++] = cursor;
    while (*cursor != '\0') {
        if (*cursor == '\t') {
            *cursor = '\0';
            if (count < capacity) fields[count++] = cursor + 1;
        }
        cursor++;
    }
    return count;
}

static int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size) {
    char *line = NULL;
    size_t line_size = 0U;
    ssize_t length;
    bool handshake = false;
    memset(model, 0, sizeof(*model));
    while ((length = getline(&line, &line_size, input)) >= 0) {
        char *fields[40];
        size_t count;
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (!handshake) {
            if (count != 2U || strcmp(fields[0], "HYDRA_TUI") != 0 || strcmp(fields[1], "2") != 0) {
                copy_text(error, error_size, "native data protocol handshake failed");
                free(line);
                return -1;
            }
            handshake = true;
            continue;
        }
        if (count == 30U && strcmp(fields[0], "H") == 0) {
            struct head *head;
            if (model->head_count >= MAX_HEADS) {
                copy_text(error, error_size, "native data exceeds the 512-head safety bound");
                free(line);
                return -1;
            }
            head = &model->heads[model->head_count++];
            memset(head, 0, sizeof(*head));
            copy_text(head->branch, sizeof(head->branch), fields[1]);
            copy_text(head->session, sizeof(head->session), fields[2]);
            copy_text(head->profile, sizeof(head->profile), fields[3]);
            copy_text(head->group, sizeof(head->group), fields[4]);
            copy_text(head->pr, sizeof(head->pr), fields[5]);
            copy_text(head->status, sizeof(head->status), fields[6]);
            copy_text(head->liveness, sizeof(head->liveness), fields[7]);
            copy_text(head->declared, sizeof(head->declared), fields[8]);
            copy_text(head->observed, sizeof(head->observed), fields[9]);
            copy_text(head->confidence, sizeof(head->confidence), fields[10]);
            copy_text(head->instance, sizeof(head->instance), fields[11]);
            head->events = parse_unsigned(fields[12]);
            head->signals = parse_unsigned(fields[13]);
            head->messages = parse_unsigned(fields[14]);
            head->claims = parse_unsigned(fields[15]);
            head->scopes = parse_unsigned(fields[16]);
            head->queue = parse_unsigned(fields[17]);
            head->resources = parse_unsigned(fields[18]);
            head->diff = parse_unsigned(fields[19]);
            head->gates = parse_unsigned(fields[20]);
            head->approved = parse_unsigned(fields[21]);
            copy_text(head->desired, sizeof(head->desired), fields[22]);
            copy_text(head->source, sizeof(head->source), fields[23]);
            copy_text(head->head_id, sizeof(head->head_id), fields[24]);
            copy_text(head->adapter, sizeof(head->adapter), fields[25]);
            copy_text(head->adapter_confidence, sizeof(head->adapter_confidence), fields[26]);
            copy_text(head->adapter_source, sizeof(head->adapter_source), fields[27]);
            head->notifications = parse_unsigned(fields[28]);
            copy_text(head->notification_source, sizeof(head->notification_source), fields[29]);
        } else if (count == 6U && strcmp(fields[0], "R") == 0) {
            struct recovery *recovery;
            if (model->recovery_count >= MAX_RECOVERY) continue;
            recovery = &model->recovery[model->recovery_count++];
            memset(recovery, 0, sizeof(*recovery));
            copy_text(recovery->kind, sizeof(recovery->kind), fields[1]);
            copy_text(recovery->label, sizeof(recovery->label), fields[2]);
            copy_text(recovery->source, sizeof(recovery->source), fields[3]);
            copy_text(recovery->confidence, sizeof(recovery->confidence), fields[4]);
            copy_text(recovery->action, sizeof(recovery->action), fields[5]);
        } else {
            copy_text(error, error_size, "native data contains a malformed record");
            free(line);
            return -1;
        }
    }
    free(line);
    if (!handshake) {
        copy_text(error, error_size, "native data is empty");
        return -1;
    }
    return 0;
}

static int load_fixture(const char *path, struct model *model, char *error, size_t error_size) {
    FILE *input = fopen(path, "r");
    int result;
    if (input == NULL) {
        snprintf(error, error_size, "cannot open fixture: %s", path);
        return -1;
    }
    result = load_model_stream(input, model, error, error_size);
    fclose(input);
    return result;
}

static int refresh_model(struct app *app) {
    int pipefd[2], status = 0, ready;
    pid_t pid;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    FILE *input;
    struct model *next;
    struct timespec started, now;
    size_t bytes = 0U;
    bool failed = false, timed_out = false;
    char buffer[8192];
    char error[TEXT] = "";
    char *argv[] = {(char *)app->hydra, (char *)"tui", (char *)"--data", NULL};
    if (pipe(pipefd) != 0) return -1;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (posix_spawnp(&pid, app->hydra, &actions, &attributes, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        close(pipefd[0]); close(pipefd[1]);
        return -1;
    }
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(pipefd[1]);
    input = tmpfile();
    if (input == NULL) {
        close(pipefd[0]);
        (void)kill(-pid, SIGKILL); (void)kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
        return -1;
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    for (;;) {
        fd_set readfds;
        struct timeval timeout;
        ssize_t length;
        long elapsed_ms, remaining_ms;
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (long)(now.tv_sec - started.tv_sec) * 1000L +
                     (long)(now.tv_nsec - started.tv_nsec) / 1000000L;
        remaining_ms = 2000L - elapsed_ms;
        if (remaining_ms <= 0L) { timed_out = true; break; }
        FD_ZERO(&readfds); FD_SET(pipefd[0], &readfds);
        timeout.tv_sec = remaining_ms / 1000L;
        timeout.tv_usec = (remaining_ms % 1000L) * 1000L;
        ready = select(pipefd[0] + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) { timed_out = true; break; }
        if (ready < 0) {
            if (errno == EINTR) continue;
            failed = true; break;
        }
        length = read(pipefd[0], buffer, sizeof(buffer));
        if (length == 0) break;
        if (length < 0) {
            if (errno == EINTR) continue;
            failed = true; break;
        }
        bytes += (size_t)length;
        if (bytes > MAX_DATA_BYTES || fwrite(buffer, 1U, (size_t)length, input) != (size_t)length) {
            failed = true; break;
        }
    }
    close(pipefd[0]);
    if (timed_out || failed) {
        (void)kill(-pid, SIGKILL); (void)kill(pid, SIGKILL);
    }
    do { ready = waitpid(pid, &status, 0); } while (ready < 0 && errno == EINTR);
    if (timed_out) {
        fclose(input);
        copy_text(app->notice, sizeof(app->notice), "shell data adapter timed out; showing last good snapshot");
        return -1;
    }
    if (failed || ready != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fclose(input);
        copy_text(app->notice, sizeof(app->notice), "shell data adapter failed; showing last good snapshot");
        return -1;
    }
    rewind(input);
    next = malloc(sizeof(*next));
    if (next == NULL) {
        fclose(input);
        copy_text(app->notice, sizeof(app->notice), "native model allocation failed");
        return -1;
    }
    if (load_model_stream(input, next, error, sizeof(error)) != 0) {
        fclose(input);
        free(next);
        copy_text(app->notice, sizeof(app->notice), error);
        return -1;
    }
    fclose(input);
    app->model = *next;
    free(next);
    if (app->selected >= app->model.head_count && app->model.head_count > 0U) {
        app->selected = app->model.head_count - 1U;
    }
    app->notice[0] = '\0';
    return 0;
}

static void capture_preview(struct app *app) {
    int pipefd[2], status = 0, waited;
    pid_t pid;
    ssize_t length;
    size_t used = 0U;
    char target[TEXT + 8U];
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    struct timespec started, now;
    bool timed_out = false;
    char *argv[8];
    if (!app->preview || app->model.head_count == 0U) return;
    retarget_selection(app);
    if (selected_head(app) == NULL) return;
    snprintf(target, sizeof(target), "%s:0.0", selected_head(app)->session);
    argv[0] = (char *)"tmux"; argv[1] = (char *)"capture-pane"; argv[2] = (char *)"-p";
    argv[3] = (char *)"-S"; argv[4] = (char *)"-8"; argv[5] = (char *)"-t";
    argv[6] = target; argv[7] = NULL;
    app->preview_text[0] = '\0';
    if (pipe(pipefd) != 0) return;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (posix_spawnp(&pid, "tmux", &actions, &attributes, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
        close(pipefd[0]); close(pipefd[1]); return;
    }
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes); close(pipefd[1]);
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    while (used + 1U < sizeof(app->preview_text)) {
        fd_set readfds;
        struct timeval timeout;
        long elapsed_ms, remaining_ms;
        int ready;
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (long)(now.tv_sec - started.tv_sec) * 1000L +
                     (long)(now.tv_nsec - started.tv_nsec) / 1000000L;
        remaining_ms = 1000L - elapsed_ms;
        if (remaining_ms <= 0L) { timed_out = true; break; }
        FD_ZERO(&readfds); FD_SET(pipefd[0], &readfds);
        timeout.tv_sec = remaining_ms / 1000L;
        timeout.tv_usec = (remaining_ms % 1000L) * 1000L;
        ready = select(pipefd[0] + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) { timed_out = true; break; }
        if (ready < 0) { if (errno == EINTR) continue; timed_out = true; break; }
        length = read(pipefd[0], app->preview_text + used, sizeof(app->preview_text) - used - 1U);
        if (length == 0) break;
        if (length < 0) { if (errno == EINTR) continue; timed_out = true; break; }
        used += (size_t)length;
    }
    close(pipefd[0]);
    if (timed_out || used + 1U >= sizeof(app->preview_text)) {
        (void)kill(-pid, SIGKILL); (void)kill(pid, SIGKILL);
    }
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    app->preview_text[used] = '\0';
    if (timed_out || waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        copy_text(app->preview_text, sizeof(app->preview_text), timed_out ? "preview timed out" : "preview unavailable");
    }
}

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

static bool head_matches(const struct head *head, const char *search) {
    if (search[0] == '\0') return true;
    return strstr(head->branch, search) != NULL || strstr(head->session, search) != NULL ||
           strstr(head->group, search) != NULL || strstr(head->profile, search) != NULL;
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

static void render_list(struct app *app) {
    size_t index;
    size_t visible = 0U;
    int available = app->rows - 7;
    retarget_selection(app);
    if (app->cols < 80) linef(app, "HEADS  status       branch");
    else linef(app, "HEADS  status      declared       observed/confidence      branch");
    for (index = 0U; index < app->model.head_count && available > 0; index++) {
        const struct head *head = &app->model.heads[index];
        if (!head_matches(head, app->search)) continue;
        if (app->cols < 80) {
            linef(app, "%c %-12s %s", index == app->selected ? '>' : ' ', display_status(head), head->branch);
        } else {
            linef(app, "%c %-11s %-14s %-11s/%-11s %s",
                  index == app->selected ? '>' : ' ', display_status(head),
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

static void render(struct app *app, unsigned frame, bool headless) {
    if (headless) linef(app, "FRAME %u %dx%d", frame, app->cols, app->rows);
    else printf("\033[H\033[2J");
    linef(app, "HYDRA MISSION CONTROL  native 1.8.0  bounded-polling  view=%s",
          app->view == 0 ? "heads" : app->view == 1 ? "detail" : app->view == 2 ? "coordination" : "recovery");
    linef(app, "source confidence is explicit; no agent state is inferred");
    if (app->search[0] != '\0') linef(app, "search: %s", app->search);
    if (app->view == 0) render_list(app);
    else if (app->view == 1) render_detail(app);
    else if (app->view == 2) render_coordination(app);
    else render_recovery(app);
    if (app->notice[0] != '\0') linef(app, "notice: %s", app->notice);
    linef(app, "j/k move  Enter detail  / search  : actions  v view  p preview  q quit");
    fflush(stdout);
}

static int run_argv(struct app *app, char *const argv[]) {
    pid_t pid;
    int status = 0;
    restore_terminal(app);
    printf("\033[H\033[2J");
    fflush(stdout);
    if (posix_spawnp(&pid, app->hydra, NULL, NULL, argv, environ) != 0) {
        perror("hydra action");
        status = 1;
    } else if (waitpid(pid, &status, 0) < 0) {
        status = 1;
    }
    printf("\nPress Enter to return to Mission Control...");
    fflush(stdout);
    while (getchar() != '\n' && !feof(stdin)) { }
    clearerr(stdin);
    if (enter_raw(app) != 0) {
        app->running = false;
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size) {
    restore_terminal(app);
    printf("\n%s", prompt);
    fflush(stdout);
    if (fgets(buffer, (int)size, stdin) == NULL) buffer[0] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';
    if (enter_raw(app) != 0) {
        app->running = false;
        return -1;
    }
    return 0;
}

struct palette_action { const char *name; int kind; };
static const struct palette_action palette[] = {
    {"switch", 1}, {"kill", 2}, {"regenerate", 3}, {"spawn", 4}, {"status", 5},
    {"claims", 6}, {"collisions", 7}, {"scopes", 8}, {"queue", 9},
    {"resources", 10}, {"git diff", 11}, {"approvals", 12}, {"recovery inspect", 13}
};

static void execute_palette(struct app *app, const char *query) {
    size_t index;
    const struct palette_action *chosen = NULL;
    struct head *head = selected_head(app);
    char branch[TEXT] = "";
    if (query[0] == '\0') {
        copy_text(app->notice, sizeof(app->notice), "action search canceled");
        return;
    }
    for (index = 0U; index < sizeof(palette) / sizeof(palette[0]); index++) {
        if (strstr(palette[index].name, query) != NULL) { chosen = &palette[index]; break; }
    }
    if (chosen == NULL) { copy_text(app->notice, sizeof(app->notice), "no explicit local action matched"); return; }
    if (chosen->kind == 4) {
        char *argv[4];
        if (prompt_text(app, "Branch to spawn: ", branch, sizeof(branch)) != 0 || branch[0] == '\0') return;
        argv[0] = (char *)app->hydra; argv[1] = (char *)"spawn"; argv[2] = branch; argv[3] = NULL;
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 3) {
        char *argv[] = {(char *)app->hydra, (char *)"regenerate", NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 5) {
        char *argv[] = {(char *)app->hydra, (char *)"status", NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 6) {
        char *argv[] = {(char *)app->hydra, (char *)"claim", (char *)"list", NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 9) {
        char *argv[] = {(char *)app->hydra, (char *)"queue", NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 13) {
        char *argv[] = {(char *)app->hydra, (char *)"doctor", NULL};
        (void)run_argv(app, argv); return;
    }
    if (head == NULL) { copy_text(app->notice, sizeof(app->notice), "select a head for that action"); return; }
    if (chosen->kind == 1 || chosen->kind == 2) {
        char *argv[] = {(char *)app->hydra, (char *)(chosen->kind == 1 ? "switch" : "kill"), head->branch, NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 7) {
        char other[TEXT] = "";
        char *argv[5];
        if (prompt_text(app, "Compare with head: ", other, sizeof(other)) != 0 || other[0] == '\0') return;
        argv[0] = (char *)app->hydra; argv[1] = (char *)"collision";
        argv[2] = head->branch; argv[3] = other; argv[4] = NULL;
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 8) {
        char *argv[] = {(char *)app->hydra, (char *)"scope", (char *)"show", head->branch, NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 10) {
        char *argv[] = {(char *)app->hydra, (char *)"resource", (char *)"status", head->branch, NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 11) {
        char *argv[] = {(char *)app->hydra, (char *)"diff", head->branch, NULL};
        (void)run_argv(app, argv); return;
    }
    if (chosen->kind == 12) {
        char *argv[] = {(char *)app->hydra, (char *)"gate", (char *)"status", head->branch, NULL};
        (void)run_argv(app, argv);
    }
}

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
    if (enter_raw(app) != 0) return 4;
    app->running = true;
    while (app->running && !stop_requested) {
        time_t now = time(NULL);
        update_size(app);
        if (now - last_refresh >= 2) {
            (void)refresh_model(app);
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
        else if (key == 27) handle_escape(app);
    }
    restore_terminal(app);
    return stop_signal == 0 ? 0 : 128 + stop_signal;
}

static int parse_size(const char *value, int *cols, int *rows) {
    char extra;
    return sscanf(value, "%dx%d%c", cols, rows, &extra) == 2 && *cols >= 20 && *rows >= 6 ? 0 : -1;
}

static void usage(FILE *out) {
    fputs("usage: hydra-tui [--version|--protocol-version|--hydra PATH|--headless-fixture FILE --size COLSxROWS --frames N [--view heads|detail|coordination|recovery]]\n", out);
}

int main(int argc, char **argv) {
    struct app app;
    const char *fixture = NULL;
    unsigned frames = 1U, frame;
    int index;
    char error[TEXT] = "";
    memset(&app, 0, sizeof(app));
    signal(SIGPIPE, signal_handler);
    setlocale(LC_CTYPE, "");
    app.hydra = getenv("HYDRA_BIN_CMD") == NULL ? "hydra" : getenv("HYDRA_BIN_CMD");
    app.cols = 80; app.rows = 24;
    app.no_color = getenv("NO_COLOR") != NULL;
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--version") == 0) {
            printf("Hydra TUI %s protocol %d\n", HYDRA_TUI_VERSION, HYDRA_TUI_PROTOCOL); return 0;
        } else if (strcmp(argv[index], "--protocol-version") == 0) {
            printf("%d\n", HYDRA_TUI_PROTOCOL); return 0;
        } else if (strcmp(argv[index], "--hydra") == 0 && index + 1 < argc) app.hydra = argv[++index];
        else if (strcmp(argv[index], "--headless-fixture") == 0 && index + 1 < argc) fixture = argv[++index];
        else if (strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
            if (parse_size(argv[++index], &app.cols, &app.rows) != 0) { usage(stderr); return 2; }
        } else if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            frames = parse_unsigned(argv[++index]); if (frames == 0U || frames > 100U) return 2;
        } else if (strcmp(argv[index], "--view") == 0 && index + 1 < argc) {
            const char *view = argv[++index];
            if (strcmp(view, "heads") == 0) app.view = 0;
            else if (strcmp(view, "detail") == 0) app.view = 1;
            else if (strcmp(view, "coordination") == 0) app.view = 2;
            else if (strcmp(view, "recovery") == 0) app.view = 3;
            else return 2;
        } else if (strcmp(argv[index], "--no-color") == 0) app.no_color = true;
        else { usage(stderr); return 2; }
    }
    if (fixture != NULL) {
        if (load_fixture(fixture, &app.model, error, sizeof(error)) != 0) { fprintf(stderr, "%s\n", error); return 2; }
        for (frame = 1U; frame <= frames && !stop_requested; frame++) render(&app, frame, true);
        if (stop_signal != 0) return 128 + stop_signal;
        return ferror(stdout) ? 3 : 0;
    }
    return interactive_main(&app);
}
