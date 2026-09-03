/* Model parsing, adapter refresh, and pane preview. */

static void copy_text(char *dst, size_t size, const char *src) {
    size_t length = 0U;
    if (size == 0U) return;
    if (src != NULL) {
        while (length + 1U < size && src[length] != '\0') length++;
        memmove(dst, src, length);
    }
    dst[length] = '\0';
}

static bool parse_unsigned(const char *value, unsigned *result) {
    char *end = NULL;
    unsigned long parsed;
    if (value == NULL) return false;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > 1000000UL) return false;
    *result = (unsigned)parsed;
    return true;
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
            if (!parse_unsigned(fields[12], &head->events) ||
                !parse_unsigned(fields[13], &head->signals) ||
                !parse_unsigned(fields[14], &head->messages) ||
                !parse_unsigned(fields[15], &head->claims) ||
                !parse_unsigned(fields[16], &head->scopes) ||
                !parse_unsigned(fields[17], &head->queue) ||
                !parse_unsigned(fields[18], &head->resources) ||
                !parse_unsigned(fields[19], &head->diff) ||
                !parse_unsigned(fields[20], &head->gates) ||
                !parse_unsigned(fields[21], &head->approved) ||
                !parse_unsigned(fields[28], &head->notifications)) {
                copy_text(error, error_size, "native data contains an invalid numeric field");
                free(line);
                return -1;
            }
            copy_text(head->desired, sizeof(head->desired), fields[22]);
            copy_text(head->source, sizeof(head->source), fields[23]);
            copy_text(head->head_id, sizeof(head->head_id), fields[24]);
            copy_text(head->adapter, sizeof(head->adapter), fields[25]);
            copy_text(head->adapter_confidence, sizeof(head->adapter_confidence), fields[26]);
            copy_text(head->adapter_source, sizeof(head->adapter_source), fields[27]);
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

static void refresh_current_session(struct app *app) {
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
