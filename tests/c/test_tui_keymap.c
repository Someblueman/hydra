/* The control centre's one binding table: no key has two meanings within a
 * scope chain, every binding is labelled and grouped, the footer fits every
 * width and keeps help and quit, the help lists every binding of its chain,
 * and keys files written for the first version still load. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../../src/tui/internal.h"

static int failures;

static void result(bool ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

static struct app *app_new(bool fleet) {
    struct app *app = calloc(1U, sizeof(*app));
    if (!app) exit(1);
    app->hydra = "/nonexistent/hydra";
    app->fleet = fleet;
    app->cols = 140; app->rows = 40;
    app->ascii = true;
    return app;
}

/* The defaults parse, and in each mode no chain holds a key twice. */
static bool chains_unique(bool fleet) {
    struct app *app = app_new(fleet);
    char error[TEXT];
    bool ok = key_chain_check(native_keys(app)->bindings, KEY_ACTIONS, error, sizeof(error));
    if (!ok) fprintf(stderr, "%s\n", error);
    free(app);
    return ok;
}

static bool every_binding_labelled(void) {
    struct app *app = app_new(false);
    const struct native_keys *k = native_keys(app);
    size_t i;
    bool ok = true;
    for (i = 0; i < KEY_ACTIONS; i++) {
        const struct kt_binding *b = &k->bindings[i];
        bool good = b->name && b->label && b->label[0] && b->group && b->group[0] && b->chord_count > 0 &&
            b->action == i;
        if (!good) { fprintf(stderr, "binding %zu (%s) lacks a label, group or chord\n", i, b->name ? b->name : "?"); ok = false; }
    }
    free(app);
    return ok;
}

/* A rule caught by the check: one key bound twice in one chain. */
static bool conflict_refused(void) {
    struct app *app = app_new(false);
    struct kt_binding rows[KEY_ACTIONS];
    struct kt_keymap map = {rows, KEY_ACTIONS};
    char error[TEXT];
    bool ok;
    memcpy(rows, native_keys(app)->bindings, sizeof(rows));
    ok = kt_keymap_configure(&map, "task.new = x\n", 13U, error, sizeof(error));
    ok = ok && !key_chain_check(rows, KEY_ACTIONS, error, sizeof(error)) && strstr(error, "task.new") && strstr(error, "head.remove");
    free(app);
    return ok;
}

/* The canvases here are ASCII, so every drawn cell is one byte. */
static char cell_char(const struct kt_cell *cell) {
    return cell->glyph >= 32U && cell->glyph < 127U ? (char)cell->glyph : ' ';
}

static void set_chain(struct app *app, int view, bool help, bool result_open) {
    app->view = view; app->help = help; app->result_open = result_open;
}

static bool footer_text(struct app *app, int width, char *out, size_t size) {
    struct kt_cell cells[200];
    struct kt_canvas canvas;
    int x;
    size_t used = 0;
    if (!kt_init(&canvas, cells, 200, width, 1, false)) return false;
    kt_clear(&canvas, KT_BASE);
    key_footer(app, &canvas, (struct kt_rect){0, 0, width, 1});
    for (x = 0; x < width && used + 1 < size; x++) out[used++] = cell_char(&cells[x]);
    out[used] = '\0';
    return true;
}

/* Every chain's footer fits 40 to 140 columns without a broken entry and
 * keeps "? help" and "q quit" wherever they are live. */
static bool footers_fit(void) {
    static const int widths[] = {40, 60, 80, 100, 140};
    static const int views[] = {0, 1, 3, 4, 5, 6, 7, 8, 9};
    struct app *app = app_new(false);
    char text[1024];
    size_t v, w;
    bool ok = true;
    for (v = 0; v < sizeof(views) / sizeof(views[0]); v++) {
        for (w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
            set_chain(app, views[v], false, false);
            if (!footer_text(app, widths[w], text, sizeof(text))) { ok = false; continue; }
            if (!strstr(text, "? help") || !strstr(text, "q quit")) {
                fprintf(stderr, "view %d at %d: %s\n", views[v], widths[w], text); ok = false;
            }
        }
    }
    set_chain(app, 0, true, false);
    ok = ok && footer_text(app, 60, text, sizeof(text)) && strstr(text, "close help");
    set_chain(app, 0, false, true);
    ok = ok && footer_text(app, 60, text, sizeof(text)) && strstr(text, "close");
    free(app);
    return ok;
}

/* The help lists each binding of the chain that applies, by its label, and
 * the chords of the agent layer. */
static bool help_lists_chain(void) {
    static struct kt_cell cells[140 * 120];
    struct app *app = app_new(false);
    struct kt_canvas canvas;
    const struct native_keys *k = native_keys(app);
    static char screen[141 * 120 + 1];
    size_t i, used = 0;
    int height;
    bool ok;
    set_chain(app, 8, false, false);
    height = key_help(app, NULL, (struct kt_rect){0, 0, 140, 120}, 0);
    ok = height > 4 && height <= 120 && kt_init(&canvas, cells, sizeof(cells) / sizeof(cells[0]), 140, 120, false);
    if (!ok) { free(app); return false; }
    kt_clear(&canvas, KT_BASE);
    (void)key_help(app, &canvas, (struct kt_rect){0, 0, 140, 120}, 0);
    for (i = 0; i < 140U * 120U && used + 1 < sizeof(screen); i++) {
        screen[used++] = cell_char(&cells[i]);
        if (i % 140U == 139U) screen[used++] = '\n';
    }
    screen[used] = '\0';
    for (i = KEY_VIEW_ACTIONS; i < KEY_ACTIONS; i++) {
        uint32_t chain = key_chains[KEY_CHAIN_STATISTICS] & ~(uint32_t)KEY_SCOPE_TERMINAL;
        if (!(k->bindings[i].scope & chain) || !key_applies(app, (enum key_action)i)) continue;
        if (!strstr(screen, k->bindings[i].label)) { fprintf(stderr, "help lacks %s\n", k->bindings[i].name); ok = false; }
    }
    ok = ok && strstr(screen, "While typing to an agent") && strstr(screen, "leave input") && strstr(screen, "Statistics");
    free(app);
    return ok;
}

/* A keys file of the first version (the agent-layer chords only) still
 * loads, and its names still name the same commands. */
static bool old_keys_file_loads(void) {
    static const char text[] = "terminal.leave = ctrl+b tab\nterminal.close = ctrl+b x\nworkspace.layout.plan = ctrl+b B\n";
    char path[] = "/tmp/hydra-keymap-test-XXXXXX";
    struct app *app = app_new(false);
    char shown[64];
    int fd = mkstemp(path);
    bool ok = fd >= 0 && write(fd, text, sizeof(text) - 1U) == (ssize_t)(sizeof(text) - 1U);
    if (fd >= 0) (void)close(fd);
    setenv("HYDRA_TUI_KEYS", path, 1);
    native_keys_load(app, true);
    unsetenv("HYDRA_TUI_KEYS");
    (void)unlink(path);
    ok = ok && !native_keys(app)->problem[0] && key_text(app, KEY_TERMINAL_LEAVE, shown, sizeof(shown)) &&
        !strcmp(shown, "Ctrl-B Tab") && key_text(app, KEY_LAYOUT_PLAN, shown, sizeof(shown)) && !strcmp(shown, "Ctrl-B B");
    free(app);
    return ok;
}

/* Fleet mode leaves local-only actions live nowhere, so they reach neither
 * its footer nor its help. */
/* The fleet entry opens the one control centre filtered to remote work: it
 * keeps every key of local work (a talks to or attaches, x removes or
 * interrupts), leaves out only the local statistics pages, and the rows that
 * a and x took over are live in neither mode. */
static bool fleet_drops_local(void) {
    struct app *app = app_new(true), *local = app_new(false);
    const struct native_keys *k = native_keys(app), *l = native_keys(local);
    bool ok = k->bindings[KEY_TASK_NEW].scope && k->bindings[KEY_HEAD_REMOVE].scope && k->bindings[KEY_HEADS_TALK].scope &&
        !k->bindings[KEY_STATS_PAGE].scope && !k->bindings[KEY_STATS_COHORTS].scope && l->bindings[KEY_STATS_COHORTS].scope &&
        !k->bindings[KEY_HEADS_ATTACH].scope && !k->bindings[KEY_HEADS_INTERRUPT].scope &&
        !l->bindings[KEY_HEADS_ATTACH].scope && !l->bindings[KEY_HEADS_INTERRUPT].scope;
    free(app); free(local);
    return ok;
}

int main(void) {
    result(chains_unique(false), "no key has two meanings within a scope chain (local)");
    result(chains_unique(true), "no key has two meanings within a scope chain (fleet)");
    result(every_binding_labelled(), "every binding has a name, label, group and chord");
    result(conflict_refused(), "a key bound twice in one chain is refused, naming both actions");
    result(footers_fit(), "every footer fits 40 to 140 columns and keeps help and quit");
    result(help_lists_chain(), "help lists every binding of its chain and the agent-layer chords");
    result(old_keys_file_loads(), "a keys file with the first version's names still loads");
    result(fleet_drops_local(), "the fleet entry keeps the one keymap but its own statistics; retired rows are live nowhere");
    printf("Passed: %d checks, Failed: %d\n", 8 - failures, failures);
    return failures ? 1 : 0;
}
