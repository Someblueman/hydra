/* Explicit shell CLI actions exposed by the native palette. */

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

static int spawn_argv(char *const argv[]) {
    pid_t pid;
    int status = 0;
    if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0) {
        perror("hydra action");
        return 1;
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 1;
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

static void spawn_action(struct app *app) {
    char branch[TEXT] = "", profile[TEXT] = "", template[TEXT] = "", layout[TEXT] = "";
    char *argv[11];
    size_t count = 0U;
    if (prompt_text(app, "Branch to spawn: ", branch, sizeof(branch)) != 0 || branch[0] == '\0') return;
    if (prompt_text(app, "Profile (blank=project default, none=no agent): ", profile, sizeof(profile)) != 0) return;
    if (prompt_text(app, "Template (optional): ", template, sizeof(template)) != 0) return;
    if (prompt_text(app, "Layout (blank=default, dev, full): ", layout, sizeof(layout)) != 0) return;
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)"spawn";
    argv[count++] = branch;
    if (strcmp(profile, "none") == 0) {
        argv[count++] = (char *)"--no-agent";
    } else if (profile[0] != '\0') {
        argv[count++] = (char *)"--profile";
        argv[count++] = profile;
    }
    if (template[0] != '\0') {
        argv[count++] = (char *)"--template";
        argv[count++] = template;
    }
    if (layout[0] != '\0') {
        argv[count++] = (char *)"--layout";
        argv[count++] = layout;
    }
    argv[count] = NULL;
    (void)run_argv(app, argv);
}

static void group_marked_action(struct app *app) {
    char group[TEXT] = "";
    char *argv[MAX_HEADS + 5U];
    size_t index, count = 0U;
    if (prompt_text(app, "Group for selected heads: ", group, sizeof(group)) != 0 || group[0] == '\0') return;
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)"group";
    argv[count++] = (char *)"create";
    argv[count++] = group;
    for (index = 0U; index < app->marked_count; index++) argv[count++] = app->marked[index];
    argv[count] = NULL;
    if (run_argv(app, argv) == 0) app->marked_count = 0U;
}

static void kill_marked_action(struct app *app) {
    size_t index, killed = 0U, skipped = 0U;
    int failed = 0;
    restore_terminal(app);
    printf("\033[H\033[2J");
    fflush(stdout);
    for (index = 0U; index < app->marked_count; index++) {
        const struct head *head = head_for_branch(app, app->marked[index]);
        char *argv[] = {(char *)app->hydra, (char *)"kill", app->marked[index], NULL};
        if (head != NULL && app->current_session[0] != '\0' &&
            strcmp(head->session, app->current_session) == 0) {
            printf("Skipping current session: %s\n", head->branch);
            skipped++;
            continue;
        }
        if (spawn_argv(argv) == 0) killed++;
        else failed = 1;
    }
    printf("\nBulk kill complete: %zu command(s), %zu current skipped%s\n",
           killed, skipped, failed ? ", failures reported above" : "");
    printf("Press Enter to return to Mission Control...");
    fflush(stdout);
    while (getchar() != '\n' && !feof(stdin)) { }
    clearerr(stdin);
    app->marked_count = 0U;
    if (enter_raw(app) != 0) app->running = false;
}

struct palette_action { const char *name; int kind; };
static const struct palette_action palette[] = {
    {"switch", 1}, {"kill", 2}, {"regenerate", 3}, {"spawn", 4}, {"status", 5},
    {"claims", 6}, {"collisions", 7}, {"scopes", 8}, {"queue", 9},
    {"resources", 10}, {"git diff", 11}, {"approvals", 12}, {"recovery inspect", 13},
    {"dashboard", 14}
};

static void execute_palette(struct app *app, const char *query) {
    size_t index;
    const struct palette_action *chosen = NULL;
    struct head *head = selected_head(app);
    if (query[0] == '\0') {
        copy_text(app->notice, sizeof(app->notice), "action search canceled");
        return;
    }
    for (index = 0U; index < sizeof(palette) / sizeof(palette[0]); index++) {
        if (strstr(palette[index].name, query) != NULL) { chosen = &palette[index]; break; }
    }
    if (chosen == NULL) { copy_text(app->notice, sizeof(app->notice), "no explicit local action matched"); return; }
    if (chosen->kind == 4) { spawn_action(app); return; }
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
    if (chosen->kind == 14) {
        char *argv[] = {(char *)app->hydra, (char *)"dashboard", NULL};
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

/* Fleet actions carry host, project and observed instance to the public CLI. */
static void fleet_action(struct app *app, bool attach) {
    struct head *head = selected_head(app);
    char confirm[TEXT] = "";
    char *argv[12];
    if (head == NULL || head->remote_host[0] == '\0') return;
    if (!attach && (prompt_text(app, "Interrupt remote head? Type yes: ", confirm, sizeof(confirm)) != 0 || strcmp(confirm, "yes") != 0)) return;
    argv[0] = (char *)app->hydra; argv[1] = (char *)"fleet";
    argv[2] = (char *)(attach ? "attach" : "cancel"); argv[3] = head->remote_host;
    argv[4] = (char *)"--project"; argv[5] = head->remote_project;
    argv[6] = (char *)"--instance"; argv[7] = head->instance;
    argv[8] = (char *)"--"; argv[9] = head->remote_branch; argv[10] = NULL;
    (void)run_argv(app, argv);
}
