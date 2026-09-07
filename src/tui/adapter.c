#define _POSIX_C_SOURCE 200809L
#include "adapter.h"
#include "process.h"
#include "selection.h"
#include "text.h"
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;

int refresh_model(struct app *app) {
    struct output_child child;
    FILE *input;
    struct model *next;
    size_t bytes = 0U;
    bool failed = false;
    char buffer[8192], error[TEXT] = "";
    char *argv[] = {(char *)app->hydra, (char *)(app->fleet ? "fleet" : "tui"), (char *)(app->fleet ? "tui-data" : "--data"), NULL};
    if (output_start(&child, argv, app->fleet ? 3500L : 2000L)) return -1;
    input = tmpfile();
    if (input == NULL) { (void)output_finish(&child, true); return -1; }
    for (;;) {
        ssize_t length = output_read(&child, buffer, sizeof(buffer));
        if (length == 0) break;
        if (length < 0) { failed = true; break; }
        bytes += (size_t)length;
        if (bytes > MAX_DATA_BYTES || fwrite(buffer, 1U, (size_t)length, input) != (size_t)length) {
            failed = true; break;
        }
    }
    if (output_finish(&child, failed)) {
        fclose(input);
        copy_text(app->notice, sizeof(app->notice), child.timed_out ?
                  "shell data adapter timed out; showing last good snapshot" :
                  "shell data adapter failed; showing last good snapshot");
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
    if (app->fleet && app->selected < app->model.head_count) {
        const struct head *previous = &app->model.heads[app->selected]; size_t index;
        for (index = 0; index < next->head_count; index++) {
            if (strcmp(previous->head_id, next->heads[index].head_id) == 0 && strcmp(previous->remote_host, next->heads[index].remote_host) == 0) { app->selected = index; break; }
        }
    }
    app->model = *next;
    free(next);
    if (app->selected >= app->model.head_count && app->model.head_count > 0U) {
        app->selected = app->model.head_count - 1U;
    }
    app->notice[0] = '\0';
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
