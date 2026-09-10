#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
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
int accept_model_data(struct app *app, FILE *input) {
    struct model *next;
    char error[TEXT] = "";
    char previous_task_host[128] = "", previous_task_id[128] = "";
    bool had_task = app->task_selected < app->model.task_count;
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
                !strcmp(previous->branch,next->heads[index].branch)) && !strcmp(previous->remote_host,next->heads[index].remote_host)) {
                app->selected=index; break;
            }
        }
    }
    if (had_task) {
        copy_text(previous_task_host, sizeof(previous_task_host), app->model.tasks[app->task_selected].host);
        copy_text(previous_task_id, sizeof(previous_task_id), app->model.tasks[app->task_selected].task_id);
    }
    app->model = *next;
    free(next);
    app->task_selected = 0;
    if (had_task) {
        size_t index;
        for (index = 0; index < app->model.task_count; index++)
            if (!strcmp(previous_task_host, app->model.tasks[index].host) && !strcmp(previous_task_id, app->model.tasks[index].task_id)) { app->task_selected = index; break; }
    }
    if (app->selected >= app->model.head_count && app->model.head_count > 0U) {
        app->selected = app->model.head_count - 1U;
    }
    app->snapshot_error[0] = '\0';
    return 0;
}
int refresh_model(struct app *app) {
    int result;
    char notice[TEXT];
    FILE *input;
    native_observations_cancel(app,0);
    copy_text(notice,sizeof(notice),app->notice); app->notice[0]='\0';
    input=capture_adapter(app,app->fleet ? "fleet" : "tui",
        app->fleet ? "tui-visual-data" : "--data",app->fleet ? 3500 : 2000);
    if (!input) copy_text(app->snapshot_error,sizeof(app->snapshot_error),app->notice[0] ? app->notice : "Snapshot observation unavailable");
    copy_text(app->notice,sizeof(app->notice),notice);
    result = accept_model_data(app,input);
    record_snapshot(app, result == 0);
    return result;
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
void capture_preview(struct app *app) {
    if (app->fleet) return;
    struct output_child child;
    size_t used = 0U;
    bool failed = false;
    char target[TEXT + 8U];
    char *argv[8];
    if (!app->preview || app->model.head_count == 0U) return;
    retarget_selection(app);
    if (selected_head(app) == NULL) return;
    snprintf(target, sizeof(target), "%s:0.0", selected_head(app)->session);
    argv[0] = (char *)"tmux"; argv[1] = (char *)"capture-pane"; argv[2] = (char *)"-p";
    argv[3] = (char *)"-S"; argv[4] = (char *)"-8"; argv[5] = (char *)"-t";
    argv[6] = target; argv[7] = NULL;
    app->preview_text[0] = '\0';
    if (output_start(&child, argv, 1000L)) return;
    while (used + 1U < sizeof(app->preview_text)) {
        ssize_t length = output_read(&child, app->preview_text + used, sizeof(app->preview_text) - used - 1U);
        if (length == 0) break;
        if (length < 0) { failed = true; break; }
        used += (size_t)length;
    }
    app->preview_text[used] = '\0';
    if (output_finish(&child, failed || used + 1U >= sizeof(app->preview_text))) {
        copy_text(app->preview_text, sizeof(app->preview_text), child.timed_out ? "preview timed out" : "preview unavailable");
    }
}
