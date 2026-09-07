/* Command-line parsing and process entry point. */

static int parse_size(const char *value, int *cols, int *rows) {
    char *end;
    long width, height;
    errno = 0; width = strtol(value, &end, 10);
    if (errno || end == value || *end != 'x' || width < 20 || width > 4096) return -1;
    value = end + 1; height = strtol(value, &end, 10);
    if (errno || end == value || *end || height < 6 || height > 4096) return -1;
    *cols = (int)width; *rows = (int)height;
    return 0;
}

static void usage(FILE *out) {
    fputs("usage: hydra-tui [--theme terminal|dark|light] [--no-color] [--ascii] [--fleet]\n"
          "  [--view statistics|workspace|overview|heads|detail|coordination|recovery|workflows|hosts]\n"
          "  [--version|--protocol-version|--diagnostics|--hydra PATH]\n"
          "  [--headless-fixture FILE [--workflow-fixture FILE] [--statistics-fixture FILE]\n"
          "   --size COLSxROWS --frames N]\n", out);
}

int main(int argc, char **argv) {
    struct app app;
    const char *fixture = NULL;
    const char *workflow_fixture = NULL;
    const char *statistics_fixture = NULL;
    unsigned frames = 1U, frame;
    int index;
    bool explicit_view = false;
    const char *theme = getenv("HYDRA_TUI_THEME");
    char error[TEXT] = "";
    memset(&app, 0, sizeof(app));
    signal(SIGPIPE, signal_handler);
    setlocale(LC_CTYPE, "");
    app.hydra = getenv("HYDRA_BIN_CMD") == NULL ? "hydra" : getenv("HYDRA_BIN_CMD");
    app.cols = 80; app.rows = 24;
    app.no_color = getenv("NO_COLOR") != NULL;
    app.ascii = strcasecmp(nl_langinfo(CODESET), "UTF-8") != 0;
    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--version") == 0) {
            printf("Hydra TUI %s protocol %d\n", HYDRA_TUI_VERSION, HYDRA_TUI_PROTOCOL); return 0;
        } else if (strcmp(argv[index], "--protocol-version") == 0) {
            printf("%d\n", HYDRA_TUI_PROTOCOL); return 0;
        } else if (strcmp(argv[index], "--hydra") == 0 && index + 1 < argc) app.hydra = argv[++index];
        else if (strcmp(argv[index], "--fleet") == 0) app.fleet = true;
        else if (strcmp(argv[index], "--headless-fixture") == 0 && index + 1 < argc) fixture = argv[++index];
        else if (strcmp(argv[index], "--workflow-fixture") == 0 && index + 1 < argc) workflow_fixture = argv[++index];
        else if (strcmp(argv[index], "--statistics-fixture") == 0 && index + 1 < argc) statistics_fixture = argv[++index];
        else if (strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
            if (parse_size(argv[++index], &app.cols, &app.rows) != 0) { usage(stderr); return 2; }
        } else if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], &frames) || frames == 0U || frames > 100U) return 2;
        } else if (strcmp(argv[index], "--view") == 0 && index + 1 < argc) {
            const char *view = argv[++index];
            explicit_view = true;
            if (strcmp(view, "heads") == 0) app.view = 0;
            else if (strcmp(view, "detail") == 0) app.view = 1;
            else if (strcmp(view, "coordination") == 0) app.view = 2;
            else if (strcmp(view, "recovery") == 0) app.view = 3;
            else if (strcmp(view, "overview") == 0) app.view = 4;
            else if (strcmp(view, "workflows") == 0) { app.view = 5; app.graph_follow = true; }
            else if (strcmp(view, "hosts") == 0) app.view = 6;
            else if (strcmp(view, "workspace") == 0) app.view = 7;
            else if (strcmp(view, "statistics") == 0) app.view = 8;
            else return 2;
        } else if (strcmp(argv[index], "--diagnostics") == 0) { app.diagnostics = true; app.view = 1; }
        else if (strcmp(argv[index], "--theme") == 0 && index + 1 < argc) theme = argv[++index];
        else if (strcmp(argv[index], "--no-color") == 0) app.no_color = true;
        else if (strcmp(argv[index], "--ascii") == 0) app.ascii = true;
        else { usage(stderr); return 2; }
    }
    if (theme != NULL && !parse_theme(theme, &app.theme)) {
        fputs("hydra-tui: theme must be terminal, dark, or light\n", stderr);
        return 2;
    }
    if (fixture != NULL) {
        if (load_fixture(fixture, &app.model, error, sizeof(error)) != 0) { fprintf(stderr, "%s\n", error); return 2; }
        record_snapshot(&app, true);
        if (workflow_fixture && refresh_workflows(&app, workflow_fixture)) return 2;
        if (statistics_fixture && refresh_statistics(&app, statistics_fixture)) return 2;
        for (frame = 1U; frame <= frames && !stop_requested; frame++) render(&app, frame, true);
        free(app.workflows);
        native_workspace_destroy(&app);
        statistics_destroy(&app);
        if (stop_signal != 0) return 128 + stop_signal;
        return ferror(stdout) ? 3 : 0;
    }
    if (workflow_fixture || statistics_fixture) { usage(stderr); return 2; }
    if (!explicit_view && !app.diagnostics) app.view = 7;
    index = interactive_main(&app);
    free(app.workflows);
    native_workspace_destroy(&app);
    native_terminals_destroy(&app);
    native_observations_destroy(&app);
    statistics_destroy(&app);
    return index;
}
