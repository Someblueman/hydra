/* Exercise the actual private input parser with deterministic clock boundaries.
 * Only clock_gettime is replaced; input uses a real descriptor and read/select. */
#define clock_gettime tui_test_clock_gettime
#include "../../src/tui/input.c"
#undef clock_gettime

static bool cross_second;
static unsigned clock_calls;
int tui_test_clock_gettime(clockid_t clock, struct timespec *value) {
    (void)clock;
    value->tv_sec = cross_second ? (clock_calls == 0U ? 1 : 2) : 3;
    value->tv_nsec = cross_second && clock_calls == 0U ? 999000000L : 0L;
    clock_calls++;
    return 0;
}

static int input_bytes(const char *bytes, size_t size) {
    FILE *file = tmpfile();
    int status = -1;
    if (!file) return -1;
    if (fwrite(bytes, 1, size, file) == size && fflush(file) == 0 &&
        fseek(file, 0, SEEK_SET) == 0 && dup2(fileno(file), STDIN_FILENO) >= 0) status = 0;
    fclose(file);
    return status;
}

static bool clock_boundary(void) {
    char data[241], remaining[300];
    struct app *app = calloc(1U, sizeof(*app));
    ssize_t count;
    if (!app) return false;
    memset(data, '9', sizeof(data));
    memcpy(data, "[<0;", 4U);
    memcpy(data + sizeof(data) - 4U, ";5Mk", 4U);
    if (input_bytes(data, sizeof(data))) { free(app); return false; }
    cross_second = true; clock_calls = 0U;
    handle_escape(app);
    count = read(STDIN_FILENO, remaining, sizeof(remaining));
    free(app);
    return count == 1 && remaining[0] == 'k';
}

static bool oversized_input(bool paste) {
    char data[8300], ch;
    const char *prefix = paste ? "\033[200~" : "\033[<0;";
    const char *suffix = paste ? "q\033[201~q" : ";5qq";
    size_t prefix_size = strlen(prefix), suffix_size = strlen(suffix), count = 0U;
    struct app *app = calloc(1U, sizeof(*app));
    bool ok;
    if (!app) return false;
    memset(data, '9', sizeof(data));
    memcpy(data, prefix, prefix_size);
    memcpy(data + sizeof(data) - suffix_size, suffix, suffix_size);
    if (input_bytes(data, sizeof(data))) { free(app); return false; }
    cross_second = false; clock_calls = 0U; app->running = true;
    while (app->running && count++ < sizeof(data) && read(STDIN_FILENO, &ch, 1U) == 1) handle_input(app, ch);
    /* Only the final normal q may exit; an early exit leaves input unread. */
    ok = !app->running && read(STDIN_FILENO, &ch, 1U) == 0;
    free(app);
    return ok;
}

static void type_keys(struct app *app, const char *bytes) {
    for (; *bytes; bytes++) handle_input(app, *bytes);
}

/* Hydra's chords need no agent view: with nothing attached the leader is
 * held and shown, a key that completes nothing is named and dropped, and the
 * case of a letter decides the command. */
static bool chords_without_agent(void) {
    struct app *app = calloc(1U, sizeof(*app));
    char text[512];
    bool ok;
    if (!app) return false;
    app->running = true;
    ok = !key_pending_text(app, text, sizeof(text));
    type_keys(app, "\x1c");
    ok = ok && key_pending_text(app, text, sizeof(text)) && app->running;
    ok = ok && !strncmp(text, "Ctrl-\\ ", 7) && strstr(text, "Tab leave input") && strstr(text, "q quit");
    type_keys(app, "Q");
    ok = ok && !key_pending_text(app, text, sizeof(text)) && app->running;
    ok = ok && !strcmp(app->notice, "Ctrl-\\ Q is not bound; nothing was sent to the agent");
    type_keys(app, "\x1c\x1c");
    ok = ok && !key_pending_text(app, text, sizeof(text)) && app->running && !app->terminals;
    type_keys(app, "\x1cq");
    ok = ok && !app->running;
    native_workspace_destroy(app);
    frame_free(app);
    free(app);
    return ok;
}

/* Hint text follows the bindings: a rebound action shows its new chord, an
 * unbound one falls back or is left out, and the first variant that fits wins. */
static bool hints_follow_keys(void) {
    static const char *const variants[] = {
        "{terminal.leave} leaves input|{terminal.close} closes the view", "{terminal.leave} leave", "L"};
    static const char keys[] = "terminal.leave = f2\nterminal.reconnect = none\n";
    struct app *app = calloc(1U, sizeof(*app));
    char text[256], error[128];
    bool ok;
    if (!app) return false;
    ok = kt_keymap_configure(&native_keys(app)->map, keys, sizeof(keys) - 1U, error, sizeof(error));
    key_hint(app, text, sizeof(text), " / ",
             "A|a or {terminal.reconnect} reconnect~a reconnect|{terminal.close} close|{terminal.reconnect} again|{no.such} key|B");
    ok = ok && !strcmp(text, "A / a reconnect / Ctrl-\\ x close / B");
    key_hint_fit(app, text, sizeof(text), "  ", variants, 3, 41);
    ok = ok && !strcmp(text, "F2 leaves input  Ctrl-\\ x closes the view");
    key_hint_fit(app, text, sizeof(text), "  ", variants, 3, 40);
    ok = ok && !strcmp(text, "F2 leave");
    key_hint_fit(app, text, sizeof(text), "  ", variants, 3, 0);
    ok = ok && !strcmp(text, "L");
    key_hint(app, text, 6, " / ", "{terminal.close} closes");
    ok = ok && strlen(text) == 5;
    ok = ok && key_text(app, KEY_TERMINAL_LEAVE, text, sizeof(text)) && !strcmp(text, "F2");
    ok = ok && !key_text(app, KEY_TERMINAL_RECONNECT, text, sizeof(text)) && !text[0];
    free(app);
    return ok;
}

int main(void) {
    bool clock_ok = clock_boundary(), paste_ok = oversized_input(true), csi_ok = oversized_input(false);
    bool chords_ok = chords_without_agent(), hints_ok = hints_follow_keys();
    printf("[%s] chords are held, named when unbound and case-sensitive with no agent attached\n", chords_ok ? "PASS" : "FAIL");
    printf("[%s] hints name the chords in effect and drop unbound actions\n", hints_ok ? "PASS" : "FAIL");
    if (!chords_ok || !hints_ok) return 1;
    printf("[%s] CSI drain survives a 1ms second-boundary crossing\n", clock_ok ? "PASS" : "FAIL");
    printf("[%s] oversized paste stays inert until its terminator\n", paste_ok ? "PASS" : "FAIL");
    printf("[%s] oversized CSI stays inert until its terminator\n", csi_ok ? "PASS" : "FAIL");
    return clock_ok && paste_ok && csi_ok ? 0 : 1;
}
