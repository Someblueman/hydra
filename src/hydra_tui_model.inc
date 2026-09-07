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
    bool handshake = false, fleet_stream = false, fleet_hosts = false;
    memset(model, 0, sizeof(*model));
    while ((length = getline(&line, &line_size, input)) >= 0) {
        char *fields[40];
        size_t count;
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (!handshake) {
            if (count != 2U || !((strcmp(fields[0], "HYDRA_TUI") == 0 && strcmp(fields[1], "2") == 0) || (strcmp(fields[0], "HYDRA_FLEET_TUI") == 0 && (!strcmp(fields[1], "1") || !strcmp(fields[1], "2"))))) {
                copy_text(error, error_size, "native data protocol handshake failed");
                free(line);
                return -1;
            }
            fleet_stream = strcmp(fields[0], "HYDRA_FLEET_TUI") == 0;
            fleet_hosts = fleet_stream && !strcmp(fields[1], "2");
            handshake = true;
            continue;
        }
        if (fleet_hosts && count == 5 && !strcmp(fields[0], "T")) {
            struct host_observation *host;
            size_t i;
            if (model->host_count == 16 || !fields[1][0] || strlen(fields[1]) >= 128 || strlen(fields[4]) >= 128 ||
                (strcmp(fields[2], "responded") && strcmp(fields[2], "failed"))) {
                copy_text(error, error_size, "invalid fleet host observation"); free(line); return -1;
            }
            for (i = 0; i < model->host_count; i++) if (!strcmp(model->hosts[i].name, fields[1])) {
                copy_text(error, error_size, "duplicate fleet host observation"); free(line); return -1;
            }
            host = &model->hosts[model->host_count++];
            copy_text(host->name, sizeof(host->name), fields[1]); copy_text(host->state, sizeof(host->state), fields[2]);
            copy_text(host->error, sizeof(host->error), fields[4]);
            if (!parse_unsigned(fields[3], &host->heads)) { copy_text(error, error_size, "invalid fleet head count"); free(line); return -1; }
        } else if (fleet_stream && count == 7U && strcmp(fields[0], "F") == 0) {
            struct head *head;
            if (model->head_count >= MAX_HEADS || strlen(fields[1]) >= 128U || strlen(fields[2]) >= SOURCE_TEXT || strlen(fields[3]) >= TEXT || strlen(fields[4]) >= TEXT || strlen(fields[5]) >= TEXT) {
                copy_text(error, error_size, "fleet row exceeds native bounds"); free(line); return -1;
            }
            head = &model->heads[model->head_count++];
            copy_text(head->remote_host, sizeof(head->remote_host), fields[1]);
            copy_text(head->remote_project, sizeof(head->remote_project), fields[2]);
            copy_text(head->remote_branch, sizeof(head->remote_branch), fields[3]);
            (void)snprintf(head->branch, sizeof(head->branch), "%.80s/%.170s", fields[1], fields[3]);
            copy_text(head->head_id, sizeof(head->head_id), fields[4]);
            copy_text(head->instance, sizeof(head->instance), fields[5]);
            copy_text(head->desired, sizeof(head->desired), fields[6]);
            copy_text(head->status, sizeof(head->status), fields[6]);
            copy_text(head->liveness, sizeof(head->liveness), "unknown");
            copy_text(head->source, sizeof(head->source), "remote durable state");
        } else if (!fleet_stream && count == 30U && strcmp(fields[0], "H") == 0) {
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

static void record_snapshot(struct app *app, bool valid) {
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

/* Returned stream is caller-owned and rewound; close it after parsing. */
static FILE *capture_adapter(struct app *app, const char *command, const char *option, long budget_ms) {
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

/* Consumes input on every path. */
static int accept_model_data(struct app *app, FILE *input) {
    struct model *next;
    char error[TEXT] = "";
    if (!input) return -1;
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
    if (app->selected < app->model.head_count) {
        const struct head *previous = &app->model.heads[app->selected]; size_t index;
        for (index = 0; index < next->head_count; index++) {
            if ((previous->head_id[0] ? !strcmp(previous->head_id,next->heads[index].head_id) :
                !strcmp(previous->branch,next->heads[index].branch)) && !strcmp(previous->remote_host,next->heads[index].remote_host)) {
                app->selected=index; break;
            }
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

static int refresh_model(struct app *app) {
    int result;
    native_observations_cancel(app,0);
    result = accept_model_data(app,capture_adapter(app,app->fleet ? "fleet" : "tui",
        app->fleet ? "tui-visual-data" : "--data",app->fleet ? 3500 : 2000));
    record_snapshot(app, result == 0);
    return result;
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
    if (app->fleet) return;
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
