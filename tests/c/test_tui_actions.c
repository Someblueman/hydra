/* Production actions with real spawn, pipes, clocks, signals and waits.
 * Only terminal painting/input waiting is replaced for this process test. */
#define update_size test_update_size
#define render test_render
#define read_key test_read_key
#define terminal_stopped test_terminal_stopped
#include "../../src/tui/actions.c"
#include "../../src/tui/action_capture.c"
#undef update_size
#undef render
#undef read_key
#undef terminal_stopped

void test_update_size(struct app *app) { (void)app; }
void test_render(struct app *app, unsigned frame, bool headless) { (void)app; (void)frame; (void)headless; }
int test_read_key(int timeout_ms, char *key) {
    const struct timespec pause = {0, timeout_ms * 1000000L};
    (void)key; (void)nanosleep(&pause, NULL); return 0;
}
bool test_terminal_stopped(void) { return false; }

static int check(bool ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    return ok ? 0 : 1;
}

static int timeout_case(struct app *app, const char *script, const char *name) {
    char out[512];
    char *argv[] = {"sh", "-c", (char *)script, NULL};
    struct timespec started;
    int status, child_status;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    status = run_captured(app, argv, out, sizeof(out), 50L);
    long elapsed = action_elapsed(&started);
    int failures = check(status == 124 && elapsed < 2000 && strstr(out, "outcome unknown"), name);
    failures += check(!app->action_pid && waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD,
                      "timed-out action leader is reaped");
    return failures;
}

int main(void) {
    struct app *app = calloc(1U, sizeof(*app));
    char output[128], *ok[] = {"sh", "-c", "printf complete; exit 7", NULL};
    struct removal r = {0};
    char targets[1][TEXT] = {"current-head"};
    int failed = 0;
    if (!app) return 1;
    failed += check(run_captured(app, ok, output, sizeof(output), 1000L) == 7 && !strcmp(output, "complete"),
                    "normal action preserves output and nonzero status");
    failed += timeout_case(app, "exec 1>&- 2>&-; exec sleep 20", "pipe EOF does not bypass action deadline");
    failed += timeout_case(app, "while :; do printf flood; done", "continuous output does not bypass action deadline");
    failed += timeout_case(app, "trap '' TERM; exec sleep 20", "TERM resistance reaches bounded KILL escalation");
    failed += timeout_case(app, "sleep 20 & exit 0", "inherited output does not bypass action deadline");
    r.count = 1;
    app->model.head_count = 1;
    copy_text(app->model.heads[0].branch, TEXT, targets[0]);
    copy_text(app->model.heads[0].session, TEXT, "current-session");
    copy_text(app->current_session, TEXT, "current-session");
    remove_one(app, targets[0], 0, &r);
    removal_notice(app, targets, &r);
    failed += check(!r.removed && r.skipped == 1 && strstr(app->notice, "Skipped current-head"),
                    "protected current head reports skipped removal");
    free(app);
    return failed ? 1 : 0;
}
