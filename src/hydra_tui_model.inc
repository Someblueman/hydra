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

static int fleet_record(struct model *model, char **fields, char *error, size_t error_size) {
    struct head *head;
    if (model->head_count >= MAX_HEADS || strlen(fields[1]) >= 128U || strlen(fields[2]) >= SOURCE_TEXT || strlen(fields[3]) >= TEXT || strlen(fields[4]) >= TEXT || strlen(fields[5]) >= TEXT) {
        copy_text(error, error_size, "fleet row exceeds native bounds"); return -1;
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
    return 0;
}
static int head_record(struct model *model, char **fields, char *error, size_t error_size) {
    struct head *head;
    if (model->head_count >= MAX_HEADS) {
        copy_text(error, error_size, "native data exceeds the 512-head safety bound");
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
        return -1;
    }
    copy_text(head->desired, sizeof(head->desired), fields[22]);
    copy_text(head->source, sizeof(head->source), fields[23]);
    copy_text(head->head_id, sizeof(head->head_id), fields[24]);
    copy_text(head->adapter, sizeof(head->adapter), fields[25]);
    copy_text(head->adapter_confidence, sizeof(head->adapter_confidence), fields[26]);
    copy_text(head->adapter_source, sizeof(head->adapter_source), fields[27]);
    copy_text(head->notification_source, sizeof(head->notification_source), fields[29]);
    return 0;
}

static int load_model_stream(FILE *input, struct model *model, char *error, size_t error_size) {
    char *line = NULL;
    size_t line_size = 0U;
    ssize_t length;
    bool handshake = false, fleet_stream = false;
    memset(model, 0, sizeof(*model));
    while ((length = getline(&line, &line_size, input)) >= 0) {
        char *fields[40];
        size_t count;
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        count = split_fields(line, fields, sizeof(fields) / sizeof(fields[0]));
        if (!handshake) {
            if (count != 2U || !((strcmp(fields[0], "HYDRA_TUI") == 0 && strcmp(fields[1], "2") == 0) || (strcmp(fields[0], "HYDRA_FLEET_TUI") == 0 && strcmp(fields[1], "1") == 0))) {
                copy_text(error, error_size, "native data protocol handshake failed");
                free(line);
                return -1;
            }
            fleet_stream = strcmp(fields[0], "HYDRA_FLEET_TUI") == 0;
            handshake = true;
            continue;
        }
        if (fleet_stream && count == 7U && strcmp(fields[0], "F") == 0) {
            if (fleet_record(model, fields, error, error_size)) { free(line); return -1; }
        } else if (!fleet_stream && count == 30U && strcmp(fields[0], "H") == 0) {
            if (head_record(model, fields, error, error_size)) { free(line); return -1; }
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
