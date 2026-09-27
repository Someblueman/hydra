/* U16: an attached agent view can always be left and closed cleanly. Real key
 * sequences drive repeated attach -> type -> leave -> close -> layout switch
 * -> resize -> reattach cycles across the planning, plan-overview and
 * monitoring layouts, including 80x24 and the compact fallback. Each cycle
 * checks that the footer names the leave and close keys, input reaches only
 * the agent, one view shows the agent once, a full repaint equals the
 * incrementally updated screen (no residual regions), and the tmux session
 * and its shell survive every client close. */
#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static struct hf_fixture f;
static struct tv_session s;
static char session[256], target[300], evidence[4096], pane_pid[64];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)

struct cycle { char layout; const char *title; int cols, rows; };
static const struct cycle cycles[] = {
    {'A', "PLAN TOGETHER", 140, 40}, {'B', "PLAN OVERVIEW", 80, 24}, {'C', "MONITOR", 60, 20},
    {'A', "PLAN TOGETHER", 80, 24}, {'C', "MONITOR", 140, 40}, {'B', "PLAN OVERVIEW", 100, 30},
    {'A', "PLAN TOGETHER", 60, 20}, {'A', "PLAN TOGETHER", 140, 40},
};

static void save(const char *step, int cycle) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%02d-%s-%dx%d.html", evidence, cycle, step, s.screen.cols, s.screen.rows);
    tv_save(&s, path);
}

static void fail_with(const char *message) {
    fprintf(stderr, "%s\n%s\n", message, tv_text(&s));
    CHECK(false, message);
}

static const char *tmux_out(const char *const argv[]) {
    return hf_run(&f, NULL, 0, argv);
}

/* The head's own pane: its shell PID and screen, read from tmux directly. */
static void agent_pid(char *out, size_t size) {
    const char *argv[] = {"tmux", "display-message", "-p", "-t", target, "#{pane_pid}", NULL};
    tv_format(out, size, "%s", tmux_out(argv));
    hf_trim(out);
}

static bool agent_contains(const char *marker) {
    const char *argv[] = {"tmux", "capture-pane", "-p", "-J", "-S", "-", "-t", target, NULL};
    return strstr(tmux_out(argv), marker) != NULL;
}

static int agent_cursor(void) {
    const char *argv[] = {"tmux", "display-message", "-p", "-t", target, "#{cursor_x}:#{cursor_y}", NULL};
    const char *out = tmux_out(argv);
    int x = -1, y = -1;
    CHECK(sscanf(out, "%d:%d", &x, &y) == 2, "agent cursor");
    return y * 1000 + x;
}

static size_t clients(void) {
    const char *argv[] = {"tmux", "list-clients", "-F", "#{client_pid}", NULL};
    char out[4096];
    size_t n = 0;
    const char *p;
    tv_format(out, sizeof(out), "%s", tmux_out(argv));
    for (p = out; *p; p++) n += *p == '\n';
    return n;
}

/* The owner session and its shell outlive every client close. */
static void owner_alive(const char *when) {
    char now[64];
    const char *argv[] = {"tmux", "has-session", "-t", target, NULL};
    (void)tmux_out(argv);
    agent_pid(now, sizeof(now));
    if (strcmp(now, pane_pid)) fail_with(when);
}

static void wait_clients(size_t expected) {
    double end = tv_now() + 5;
    while (clients() != expected && tv_now() < end) tv_pump(&s, .1);
    CHECK(clients() == expected, "attach client count");
}

static bool row_has_run(const char *text, const char *glyph, int length) {
    int run = 0;
    size_t n = strlen(glyph);
    while (*text) {
        if (!strncmp(text, glyph, n)) { run++; text += n; if (run >= length) return true; }
        else { run = 0; text++; }
    }
    return false;
}

/* Rows other than the snapshot-age status line. */
static void stable_text(char *out, size_t size) {
    char row[8192];
    size_t used = 0;
    int y;
    for (y = 0; y < s.screen.rows; y++) {
        size_t n;
        tv_row(&s, y, row, sizeof(row));
        if (strstr(row, "age ") && strstr(row, "snapshot")) continue;
        n = strlen(row);
        CHECK(used + n + 2 < size, "stable text bound");
        memcpy(out + used, row, n); used += n; out[used++] = '\n';
    }
    out[used] = 0;
}

/* A forced full repaint must reproduce the incrementally updated screen:
 * any residual region or stale cell would differ. No dotted fill regions. */
static void no_residue(const char *when) {
    static char before[65536], after[65536];
    int cols = s.screen.cols, rows = s.screen.rows;
    tv_pump(&s, .4);
    stable_text(before, sizeof(before));
    tv_resize(&s, cols + 1, rows);
    tv_pump(&s, .3);
    tv_resize(&s, cols, rows);
    tv_pump(&s, .5);
    stable_text(after, sizeof(after));
    if (strcmp(before, after)) {
        fprintf(stderr, "BEFORE\n%s\nAFTER\n%s\n", before, after);
        fail_with(when);
    }
    CHECK(!row_has_run(after, "\xc2\xb7", 8) && !strstr(after, "........"), "no dotted fill region");
    CHECK(!s.screen.overflow, "no overflow");
}

static void layout(const struct cycle *c) {
    char key[2] = {c->layout, 0};
    tv_send(&s, key);
    tv_until(&s, c->title, 5);
    tv_resize(&s, c->cols, c->rows);
    tv_pump(&s, .4);
}

/* Attached: leave and close keys visible, one view, input reaches the agent. */
static void attach_and_type(int cycle, const char *marker) {
    char command[128];
    tv_send(&s, "a");
    tv_until(&s, "INPUT TO AGENT", 5);
    wait_clients(1);
    if (!tv_contains(&s, "Ctrl-B Tab leave") || !tv_contains(&s, "Ctrl-B x close")) fail_with("leave/close keys hidden");
    CHECK(hf_count(tv_text(&s), "INPUT TO AGENT") == 1, "one attached view");
    tv_format(command, sizeof(command), "echo MARK_$((%d*1000+7))\r", cycle);
    tv_send(&s, command);
    tv_until(&s, marker, 5);
    CHECK(hf_count(tv_text(&s), marker) == 1, "agent content shown once");
    CHECK(agent_contains(marker), "typed input reached the agent session");
    /* Esc belongs to the agent (it interrupts providers): /bin/cat -v shows the
     * delivered byte and input stays attached. */
    tv_send(&s, "/bin/cat -v\r");
    tv_pump(&s, .3);
    tv_send(&s, "\033\r");
    tv_until(&s, "^[", 3);
    tv_send(&s, "\004");
    tv_pump(&s, .3);
    CHECK(tv_contains(&s, "INPUT TO AGENT"), "Esc does not silently leave the agent");
    save("attached", cycle);
}

/* Ctrl-B Tab leaves input: Hydra keys no longer reach the agent; plain Tab
 * cycles focus back to the agent, and Ctrl-B Tab leaves again. */
static void leave_input(int cycle) {
    int cursor;
    int i;
    tv_send(&s, "\002\t");
    tv_pump(&s, .3);
    if (tv_contains(&s, "INPUT TO AGENT")) fail_with("Ctrl-B Tab left input");
    cursor = agent_cursor();
    tv_send(&s, "k");
    tv_pump(&s, .3);
    CHECK(agent_cursor() == cursor, "Hydra keys do not reach the agent after leaving");
    save("left", cycle);
    for (i = 0; i < 8 && !tv_contains(&s, "INPUT TO AGENT"); i++) { tv_send(&s, "\t"); tv_pump(&s, .2); }
    CHECK(tv_contains(&s, "INPUT TO AGENT"), "Tab focus returns to the agent");
    tv_send(&s, "\002\t");
    tv_pump(&s, .3);
}

/* Ctrl-B x closes only the view: no attached content remains on screen. */
static void close_view(int cycle, const char *marker) {
    tv_send(&s, "\002x");
    tv_pump(&s, .4);
    if (tv_contains(&s, "ATTACHED") || tv_contains(&s, "INPUT TO AGENT") || tv_contains(&s, marker))
        fail_with("closed view left agent content");
    tv_until(&s, "View closed", 3);
    wait_clients(0);
    owner_alive("closing the view killed the agent");
    no_residue("residual region after closing the view");
    save("closed", cycle);
}

/* A client that disconnects (the session detached it) never captures keys:
 * Tab moves focus in Hydra and a reattaches. */
static void disconnected_client(void) {
    char quoted[300];
    const char *argv[] = {"tmux", "detach-client", "-s", quoted, NULL};
    tv_send(&s, "a");
    tv_until(&s, "INPUT TO AGENT", 5);
    wait_clients(1);
    tv_format(quoted, sizeof(quoted), "=%s", session);
    (void)tmux_out(argv);
    tv_until(&s, "CLIENT DISCONNECTED", 5);
    if (!tv_contains(&s, "a reconnect") && !tv_contains(&s, "Ctrl-B r reconnect")) fail_with("reconnect key hidden");
    save("disconnected", 90);
    tv_send(&s, "\t");
    tv_pump(&s, .4);
    CHECK(!tv_contains(&s, "Input not delivered"), "disconnected pane does not capture keys");
    tv_send(&s, "a");
    tv_until(&s, "INPUT TO AGENT", 5);
    wait_clients(1);
    owner_alive("disconnect killed the agent");
    tv_send(&s, "\002x");
    wait_clients(0);
}

/* Another, smaller client of the same session that typed last makes tmux
 * shrink the window and fill the rest of Hydra's view with dots. Hydra names
 * that honestly; typing in Hydra takes the size back and the dots go. */
static void shared_session(void) {
    struct tv_session other;
    const char *argv[] = {"tmux", "attach-session", "-t", target, NULL};
    if (s.screen.cols != 140 || s.screen.rows != 40) tv_resize(&s, 140, 40);
    tv_send(&s, "a");
    tv_until(&s, "INPUT TO AGENT", 5);
    wait_clients(1);
    tv_open(&other, argv, 50, 15, f.repo);
    wait_clients(2);
    tv_send(&other, "echo OTHER_CLIENT\r");
    tv_until(&other, "OTHER_CLIENT", 5);
    tv_until(&s, "sized by another client", 5);
    save("shared-size", 91);
    tv_send(&s, "echo HYDRA_$((6*7))\r");
    tv_until(&s, "HYDRA_42", 5);
    tv_pump(&s, .5);
    if (tv_contains(&s, "sized by another client")) fail_with("typing in Hydra did not take the size back");
    CHECK(!row_has_run(tv_text(&s), "\xc2\xb7", 8), "no dotted fill once Hydra sets the size");
    save("shared-size-restored", 92);
    tv_send(&other, "\002d"); /* the other client detaches itself */
    {
        /* Drain its PTY so the exiting client is never blocked on output. */
        double end = tv_now() + 5;
        while (clients() != 1 && tv_now() < end) { tv_pump(&other, .1); tv_pump(&s, .05); }
        CHECK(clients() == 1, "other client detached");
        end = tv_now() + 5;
        while (tv_pump(&other, .1) && tv_now() < end) {}
    }
    tv_abort(&other);
    tv_send(&s, "\002x");
    wait_clients(0);
    owner_alive("a second client killed the agent");
}

static void write_raw(void) {
    char path[4096];
    FILE *out;
    tv_format(path, sizeof(path), "%s/session.raw", evidence);
    out = fopen(path, "wb");
    CHECK(out && fwrite(s.raw, 1, s.raw_size, out) == s.raw_size && !fclose(out), "raw PTY log");
}

static void fixture(void) {
    char path[4096], rows[65536];
    const char *p;
    tv_init();
    hf_init(&f, "hydra-attach-cycles", "repo", false, true);
    unsetenv("NO_COLOR");
    hf_file(&f, "input", path, sizeof(path));
    tv_write(path, "cycles\n");
    hf_commit_init(&f);
    H("spawn", "one", "--no-agent");
    tv_format(rows, sizeof(rows), "%s", H("tui", "--data"));
    for (p = rows; p && strncmp(p, "H\t", 2); p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {}
    CHECK(p != NULL, "head row");
    p = strchr(p + 2, '\t') + 1;
    tv_format(session, sizeof(session), "%.*s", (int)(strchr(p, '\t') - p), p);
    tv_format(target, sizeof(target), "=%s:", session);
    agent_pid(pane_pid, sizeof(pane_pid));
    CHECK(*pane_pid, "agent shell PID");
    tv_format(evidence, sizeof(evidence), "%s/attach-cycles-evidence", f.build);
    tv_mkdir(evidence);
}

int main(void) {
    size_t i;
    char marker[64];
    fixture();
    {
        const char *argv[] = {f.tui, "--hydra", f.hydra, NULL};
        tv_open(&s, argv, 140, 40, f.repo);
    }
    tv_until(&s, "Session   running", 8);
    for (i = 0; i < sizeof(cycles) / sizeof(*cycles); i++) {
        tv_format(marker, sizeof(marker), "MARK_%zu", i * 1000 + 7);
        layout(&cycles[i]);
        attach_and_type((int)i, marker);
        leave_input((int)i);
        close_view((int)i, marker);
    }
    disconnected_client();
    shared_session();
    /* Nothing typed in any cycle was lost: the agent session holds it all. */
    for (i = 0; i < sizeof(cycles) / sizeof(*cycles); i++) {
        tv_format(marker, sizeof(marker), "MARK_%zu", i * 1000 + 7);
        CHECK(agent_contains(marker), "work typed in every cycle is retained");
    }
    write_raw();
    tv_close(&s, "\002q", 0, 0);
    owner_alive("closing Hydra killed the agent");
    CHECK(clients() == 0, "no leaked attach clients");
    hf_cleanup();
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    printf("PASS attach cycles: %zu cycles of attach/type/leave/close/layout/resize/reattach at 140x40, 100x30, "
           "80x24 and 60x20; disconnected client releases input; shared-size fill named; evidence in %s\n",
           sizeof(cycles) / sizeof(*cycles), evidence);
    return 0;
}
