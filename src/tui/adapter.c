#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
void record_snapshot(struct app *app, bool valid) {
    size_t i;
    double queue = 0;
    bool known = app->model.head_count == 0;
    if (app->history_count == 120) {
        memmove(app->queue_history, app->queue_history + 1, 119 * sizeof(double));
        memmove(app->history_valid, app->history_valid + 1, 119 * sizeof(bool));
        app->history_count--;
    }
    for (i = 0; i < app->model.head_count; i++) if (app->model.heads[i].head_id[0]) {
        queue += app->model.heads[i].queue; known = true;
    }
    app->queue_history[app->history_count] = queue;
    app->history_valid[app->history_count++] = valid && known && !app->fleet;
    app->snapshot_stale = !valid;
    if (valid) app->snapshot_at = time(NULL);
}
FILE *capture_adapter(struct app *app, const char *command, const char *option, long budget_ms) {
    struct native_capture capture={.fd=-1};
    char *argv[]={(char *)app->hydra,(char *)command,(char *)option,NULL};
    FILE *input;
    if (!native_capture_start(&capture,argv,budget_ms)) return NULL;
    while (!native_capture_step(&capture)) {
        struct timespec pause={0,10000000L};
        (void)nanosleep(&pause,NULL);
    }
    if (capture.timed_out) copy_text(app->notice,sizeof(app->notice),"shell data adapter timed out; showing last good snapshot");
    input=native_capture_take(&capture);
    if (!input && !app->notice[0]) copy_text(app->notice,sizeof(app->notice),"shell data adapter failed; showing last good snapshot");
    return input;
}
static size_t refreshed_task_selection(const struct app *app, const struct model *next) {
    if (app->task_selected >= app->model.task_count) return app->model.task_count ? next->task_count : 0;
    const struct task_observation *previous = &app->model.tasks[app->task_selected];
    for (size_t index = 0; index < next->task_count; index++)
        if (!strcmp(previous->host, next->tasks[index].host) && !strcmp(previous->task_id, next->tasks[index].task_id)) return index;
    return next->task_count;
}
int accept_model_data(struct app *app, FILE *input) {
    struct model *next;
    char error[TEXT] = "";
    if (!input) return -1;
    next = malloc(sizeof(*next));
    if (next == NULL) {
        fclose(input);
        copy_text(app->snapshot_error, sizeof(app->snapshot_error), "native model allocation failed");
        return -1;
    }
    if (load_model_stream(input, next, error, sizeof(error)) != 0) {
        fclose(input);
        free(next);
        copy_text(app->snapshot_error, sizeof(app->snapshot_error), error);
        return -1;
    }
    fclose(input);
    if (app->selected < app->model.head_count) {
        const struct head *previous = &app->model.heads[app->selected]; size_t index;
        for (index = 0; index < next->head_count; index++) {
            if ((previous->head_id[0] ? !strcmp(previous->head_id,next->heads[index].head_id) :
                !strcmp(previous->branch,next->heads[index].branch)) &&
                !strcmp(previous->remote_host,next->heads[index].remote_host) &&
                !strcmp(previous->remote_project,next->heads[index].remote_project)) {
                app->selected=index; break;
            }
        }
    }
    app->task_selected = refreshed_task_selection(app, next);
    app->model = *next;
    free(next);
    if (app->selected >= app->model.head_count && app->model.head_count > 0U) {
        app->selected = app->model.head_count - 1U;
    }
    app->snapshot_error[0] = '\0';
    return 0;
}
void refresh_current_session(struct app *app) {
    int pipefd[2], status = 0, ready, attempt;
    pid_t pid;
    ssize_t length = 0;
    posix_spawn_file_actions_t actions;
    char output[TEXT] = "";
    char *argv[] = {(char *)"tmux", (char *)"display-message", (char *)"-p",
                    (char *)"#{session_name}", NULL};
    struct timeval timeout = {1, 0};
    struct timespec delay = {0, 50000000L};
    app->current_session[0] = '\0';
    if (getenv("TMUX") == NULL || pipe(pipefd) != 0) return;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (posix_spawnp(&pid, "tmux", &actions, NULL, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]); close(pipefd[1]); return;
    }
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    {
        fd_set readfds;
        FD_ZERO(&readfds); FD_SET(pipefd[0], &readfds);
        ready = select(pipefd[0] + 1, &readfds, NULL, NULL, &timeout);
    }
    if (ready > 0) length = read(pipefd[0], output, sizeof(output) - 1U);
    close(pipefd[0]);
    for (attempt = 0; attempt < 20; attempt++) {
        ready = waitpid(pid, &status, WNOHANG);
        if (ready == pid) break;
        if (ready < 0 && errno != EINTR) break;
        (void)nanosleep(&delay, NULL);
    }
    if (ready != pid) { (void)kill(pid, SIGKILL); (void)waitpid(pid, &status, 0); }
    if (length > 0 && ready == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        output[length] = '\0';
        output[strcspn(output, "\r\n")] = '\0';
        copy_text(app->current_session, sizeof(app->current_session), output);
    }
}
/* A headless head has no pane: its preview is the bounded, read-only view of
 * the step that runs (or last ran) on it, read without blocking the UI. */
static void headless_preview_start(struct app *app, const struct head *h) {
    struct native_capture *job;
    char *argv[] = {(char *)app->hydra, (char *)"tui", (char *)"--head-output", (char *)h->branch, NULL};
    if (!app->observations) return;
    if (strcmp(app->preview_head, h->branch)) {
        copy_text(app->preview_head, sizeof(app->preview_head), h->branch);
        app->preview_text[0] = '\0';
    }
    job = &app->observations->preview;
    if (job->pid) return;
    if (!native_capture_start(job, argv, 4000L))
        copy_text(app->preview_text, sizeof(app->preview_text), "Step output unavailable: the reader could not start");
}

void headless_preview_tick(struct app *app) {
    struct native_capture *job;
    FILE *input;
    size_t used;
    if (!app->observations) return;
    job = &app->observations->preview;
    if (!job->pid || !native_capture_step(job)) return;
    input = native_capture_take(job);
    if (!input) {
        if (!app->preview_text[0]) copy_text(app->preview_text, sizeof(app->preview_text), "Step output unavailable");
        return;
    }
    used = fread(app->preview_text, 1, sizeof(app->preview_text) - 1U, input);
    app->preview_text[used] = '\0';
    fclose(input);
}

/* A full buffer keeps its newest half from a line start, so a large capture
 * shows its latest lines rather than failing. */
static size_t preview_keep_tail(char *text, size_t used) {
    const char *line = memchr(text + used / 2U, '\n', used - used / 2U);
    size_t start = line ? (size_t)(line - text) + 1U : used / 2U;
    memmove(text, text + start, used - start);
    return used - start;
}

/* Joined lines (-J) with their colors (-e): Hydra wraps them to the view and
 * renders SGR itself; the pane's history before the screen gives context. */
static void terminal_preview(struct app *app, const struct head *h) {
    struct output_child child;
    size_t used = 0U;
    bool failed = false;
    char target[TEXT + 8U];
    char *argv[] = {(char *)"tmux", (char *)"capture-pane", (char *)"-p", (char *)"-e", (char *)"-J",
                    (char *)"-S", (char *)"-40", (char *)"-t", target, NULL};
    snprintf(target, sizeof(target), "%s:0.0", h->session);
    app->preview_text[0] = '\0';
    if (output_start(&child, argv, 1000L)) return;
    for (;;) {
        ssize_t length;
        if (used + 1U >= sizeof(app->preview_text)) used = preview_keep_tail(app->preview_text, used);
        length = output_read(&child, app->preview_text + used, sizeof(app->preview_text) - used - 1U);
        if (length == 0) break;
        if (length < 0) { failed = true; break; }
        used += (size_t)length;
    }
    app->preview_text[used] = '\0';
    if (output_finish(&child, failed)) {
        copy_text(app->preview_text, sizeof(app->preview_text), child.timed_out ? "preview timed out" : "preview unavailable");
    }
}

void capture_preview(struct app *app) {
    const struct head *h;
    if (app->fleet || !app->preview || app->model.head_count == 0U) return;
    retarget_selection(app);
    h = selected_head(app);
    if (h == NULL) return;
    if (head_headless(h)) { headless_preview_start(app, h); return; }
    app->preview_head[0] = '\0';
    terminal_preview(app, h);
}
