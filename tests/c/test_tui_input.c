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

int main(void) {
    bool clock_ok = clock_boundary(), paste_ok = oversized_input(true), csi_ok = oversized_input(false);
    printf("[%s] CSI drain survives a 1ms second-boundary crossing\n", clock_ok ? "PASS" : "FAIL");
    printf("[%s] oversized paste stays inert until its terminator\n", paste_ok ? "PASS" : "FAIL");
    printf("[%s] oversized CSI stays inert until its terminator\n", csi_ok ? "PASS" : "FAIL");
    return clock_ok && paste_ok && csi_ok ? 0 : 1;
}
