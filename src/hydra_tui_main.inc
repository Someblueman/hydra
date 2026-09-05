/* Command-line parsing and process entry point. */

static int parse_size(const char *value, int *cols, int *rows) {
    char extra;
    return sscanf(value, "%dx%d%c", cols, rows, &extra) == 2 && *cols >= 20 && *rows >= 6 ? 0 : -1;
}

static void usage(FILE *out) {
    fputs("usage: hydra-tui [--theme terminal|dark|light] [--no-color] [--version|--protocol-version|--diagnostics|--hydra PATH|--headless-fixture FILE --size COLSxROWS --frames N [--view heads|detail|coordination|recovery]]\n", out);
}

int main(int argc, char **argv) {
    struct app app;
    const char *fixture = NULL;
    unsigned frames = 1U, frame;
    int index;
    const char *theme = getenv("HYDRA_TUI_THEME");
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
        else if (strcmp(argv[index], "--fleet") == 0) app.fleet = true;
        else if (strcmp(argv[index], "--headless-fixture") == 0 && index + 1 < argc) fixture = argv[++index];
        else if (strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
            if (parse_size(argv[++index], &app.cols, &app.rows) != 0) { usage(stderr); return 2; }
        } else if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], &frames) || frames == 0U || frames > 100U) return 2;
        } else if (strcmp(argv[index], "--view") == 0 && index + 1 < argc) {
            const char *view = argv[++index];
            if (strcmp(view, "heads") == 0) app.view = 0;
            else if (strcmp(view, "detail") == 0) app.view = 1;
            else if (strcmp(view, "coordination") == 0) app.view = 2;
            else if (strcmp(view, "recovery") == 0) app.view = 3;
            else return 2;
        } else if (strcmp(argv[index], "--diagnostics") == 0) { app.diagnostics = true; app.view = 1; }
        else if (strcmp(argv[index], "--theme") == 0 && index + 1 < argc) theme = argv[++index];
        else if (strcmp(argv[index], "--no-color") == 0) app.no_color = true;
        else { usage(stderr); return 2; }
    }
    if (theme != NULL && !parse_theme(theme, &app.theme)) {
        fputs("hydra-tui: theme must be terminal, dark, or light\n", stderr);
        return 2;
    }
    if (fixture != NULL) {
        if (load_fixture(fixture, &app.model, error, sizeof(error)) != 0) { fprintf(stderr, "%s\n", error); return 2; }
        for (frame = 1U; frame <= frames && !stop_requested; frame++) render(&app, frame, true);
        if (stop_signal != 0) return 128 + stop_signal;
        return ferror(stdout) ? 3 : 0;
    }
    return interactive_main(&app);
}
