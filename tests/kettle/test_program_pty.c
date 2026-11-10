#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* The kettle runtime on a real pseudo-terminal, observed from outside: what
 * it writes, how it reacts to signals and a new size, and that every way of
 * leaving puts the terminal back exactly. */

static char root[4096], build[4096], fixture[4096];

static void expect_at(bool passed, const char *file, int line, const char *message) {
    if (!passed)
        kt_fail(file, line, message);
}
#define EXPECT(condition, message) expect_at((condition), __FILE__, __LINE__, (message))

#define FRAME_BEGIN "\033[?2026h"
#define FRAME_END "\033[?2026l"
#define ENTER "\033[?1049h\033[H\033[2J"
#define LEAVE "\033[0m\033[?1002l\033[?1006l\033[?2004l\033[?1004l\033[?25h\033[?1049l"

static size_t count(const char *text, const char *part) {
    size_t found = 0;
    for (text = strstr(text, part); text; text = strstr(text + 1, part))
        found++;
    return found;
}
static bool ends_with(const char *text, const char *tail) {
    size_t length = strlen(text), wanted = strlen(tail);
    return length >= wanted && !strcmp(text + length - wanted, tail);
}
/* Everything written so far is a sequence of complete synchronized updates:
 * no byte outside one, none nested, none left open. */
static bool frames_whole(const char *raw) {
    while (*raw) {
        const char *end, *nested;
        if (strncmp(raw, FRAME_BEGIN, 8))
            return false;
        end = strstr(raw + 8, FRAME_END);
        nested = strstr(raw + 8, FRAME_BEGIN);
        if (!end || (nested && nested < end))
            return false;
        raw = end + 8;
    }
    return true;
}
static void await_raw(struct kt_session *s, const char *marker, size_t from) {
    double end = kt_now() + 5;
    while (!(s->raw && strstr(s->raw + from, marker)) && kt_now() < end)
        kt_pump(s, .05);
    EXPECT(s->raw && strstr(s->raw + from, marker), "terminal output marker");
}
static const char *cell(struct kt_session *s, int x, int y) {
    return s->screen.cells[y * s->screen.cols + x].text;
}
/* The border reaches the last column and the last row of the terminal. */
static bool bordered(struct kt_session *s) {
    int right = s->screen.cols - 1, bottom = s->screen.rows - 1;
    bool top = !strcmp(cell(s, 0, 0), "╭") && !strcmp(cell(s, right, 0), "╮");
    return top && !strcmp(cell(s, 0, bottom), "╰") && !strcmp(cell(s, right, bottom), "╯");
}
static void begin(struct kt_session *s, int cols, int rows) {
    const char *argv[] = {fixture, NULL};
    char size[32];
    kt_open_terminal(s, argv, cols, rows, NULL);
    kt_format(size, sizeof(size), "size %dx%d", cols, rows);
    kt_until(s, size, 5);
}
/* A crashing process can take a while to disappear where the system writes a
 * crash report; wait for it without collecting its status. */
static void await_exit(struct kt_session *s) {
    double end = kt_now() + 30;
    siginfo_t info;
    for (;;) {
        memset(&info, 0, sizeof(info));
        if (waitid(P_PID, (id_t)s->pid, &info, WEXITED | WNOHANG | WNOWAIT) || info.si_pid || kt_now() >= end)
            return;
        kt_pump(s, .05);
    }
}
/* Ends the program by key or signal and checks the last thing it wrote undid
 * its terminal modes; kt_close then compares the terminal settings. */
static void finish(struct kt_session *s, const char *keys, int signum, int expected) {
    size_t mark = s->raw_size;
    if (signum)
        EXPECT(kill(s->pid, signum) == 0, "signal the program");
    else
        kt_send(s, keys);
    await_raw(s, "\033[?1049l", mark);
    await_exit(s);
    EXPECT(ends_with(s->raw, LEAVE), "terminal modes undone as the last output");
    kt_close(s, "", 0, expected);
}

static void observer_repaint(void) {
    const char partial[] = FRAME_BEGIN "\033[1;1Hab" FRAME_END;
    const char wider[] = FRAME_BEGIN "\033[1;1Hwxyz\033[2;1Hz12" FRAME_END;
    const char whole[] = FRAME_BEGIN "\033[1;1Hwxy\033[2;1Hz12" FRAME_END;
    struct kt_screen s = {0};
    kt_screen_reset(&s, 3, 2, true);
    kt_screen_feed(&s, (const unsigned char *)"loose", 5);
    kt_screen_feed(&s, (const unsigned char *)partial, sizeof(partial) - 1);
    EXPECT(s.awaiting_clear && s.cells[0].text[0] == ' ', "a partial update is not a repaint");
    kt_screen_feed(&s, (const unsigned char *)wider, sizeof(wider) - 1);
    EXPECT(s.awaiting_clear && !s.overflow, "an update for another size is not a repaint");
    kt_screen_feed(&s, (const unsigned char *)whole, sizeof(whole) - 1);
    EXPECT(!s.awaiting_clear && s.repaints == 1, "an update painting every cell is a repaint");
    EXPECT(!strcmp(s.cells[0].text, "w") && !strcmp(s.cells[5].text, "2"), "repaint cells");
    free(s.cells);
    free(s.text);
    puts("PASS observer: a full synchronized update acknowledges a resize, anything less does not");
}

static void frames(void) {
    struct kt_session s;
    size_t mark;
    begin(&s, 80, 24);
    EXPECT(!strncmp(s.raw, FRAME_BEGIN ENTER, strlen(FRAME_BEGIN ENTER)), "screen entered in the first frame");
    EXPECT(strstr(s.raw, "\033[?1002h\033[?1006h\033[?2004h\033[?1004h"), "input modes enabled");
    EXPECT(strstr(s.raw, "\033[?25l") != NULL, "cursor hidden");
    EXPECT(bordered(&s), "first frame reaches every edge");
    kt_pump(&s, .1);
    EXPECT(kt_pump(&s, .3) == 0, "idle program wrote nothing");
    kt_send(&s, "kkk");
    kt_until(&s, "keys 3", 3);
    mark = s.raw_size;
    kt_send(&s, "r");
    kt_until(&s, "keys 4", 3);
    EXPECT(strstr(s.raw + mark, "\033[1;1H") && strstr(s.raw + mark, "\033[24;1H"), "repaint rewrites every row");
    EXPECT(count(s.raw, "\033[2J") == 1 && s.screen.clears == 1, "repaint does not erase the display");
    EXPECT(bordered(&s), "repaint leaves the same screen");
    kt_send(&s, "\033[200~clip\033[201~\033[I");
    kt_until(&s, "paste [clip] focus yes", 3);
    mark = s.raw_size;
    kt_send(&s, "c");
    await_raw(&s, "\033[5;3H\033[?25h", mark);
    kt_send(&s, "c");
    await_raw(&s, "\033[?25l", mark);
    kt_until(&s, "keys 6", 3);
    EXPECT(frames_whole(s.raw), "all output is whole synchronized updates");
    finish(&s, "q", 0, 0);
    puts("PASS frames: one clear on entry, whole synchronized updates, silent when idle, overwrite repaint, "
         "paste, focus, cursor");
}

static void resizing(void) {
    struct kt_session s;
    begin(&s, 80, 24);
    kt_resize(&s, 40, 10);
    kt_until(&s, "size 40x10", 3);
    EXPECT(bordered(&s) && !s.screen.overflow, "40x10 repaint fits the new size");
    kt_resize(&s, 120, 35);
    kt_until(&s, "size 120x35", 3);
    EXPECT(bordered(&s) && !s.screen.overflow, "120x35 repaint fits the new size");
    EXPECT(kt_contains(&s, "resizes 3"), "one resize message per size change");
    EXPECT(count(s.raw, "\033[2J") == 1, "resize does not erase the display");
    EXPECT(frames_whole(s.raw), "all output is whole synchronized updates");
    finish(&s, "q", 0, 0);
    puts("PASS resize: SIGWINCH, one message per change, full repaint without erase at three sizes");
}

static void terminations(void) {
    static const int numbers[] = {SIGTERM, SIGINT, SIGHUP};
    struct kt_session s;
    size_t i;
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
        begin(&s, 80, 24);
        finish(&s, NULL, numbers[i], 128 + numbers[i]);
    }
    puts("PASS signals: SIGTERM, SIGINT and SIGHUP restore the terminal and exit 128+signal");
}

static void crashes(void) {
    static const int numbers[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    struct kt_session s;
    size_t i;
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
        begin(&s, 80, 24);
        finish(&s, NULL, numbers[i], -numbers[i]);
    }
    begin(&s, 80, 24);
    finish(&s, "a", 0, -SIGABRT);
    begin(&s, 80, 24);
    finish(&s, "x", 0, 3);
    puts("PASS abnormal exits: fatal signals, abort() and exit() restore the terminal and keep their status");
}

static void lending(void) {
    struct kt_session s;
    size_t mark;
    const char *child;
    begin(&s, 80, 24);
    mark = s.raw_size;
    kt_send(&s, "e");
    kt_until(&s, "exec 7", 5);
    child = strstr(s.raw + mark, "CHILD_COOKED");
    EXPECT(child && strstr(s.raw + mark, LEAVE) && strstr(s.raw + mark, LEAVE) < child,
           "terminal released before the child ran in cooked mode");
    EXPECT(child && strstr(child, FRAME_BEGIN ENTER), "screen entered again after the child");
    EXPECT(bordered(&s), "repaint after the child");
    mark = s.raw_size;
    kt_send(&s, "z");
    kt_until(&s, "resumes 1", 3);
    EXPECT(strstr(s.raw + mark, LEAVE) && bordered(&s), "suspend releases and resume repaints");
    EXPECT(kill(s.pid, SIGCONT) == 0, "continue");
    kt_until(&s, "resumes 2", 3);
    EXPECT(kill(s.pid, SIGTSTP) == 0, "stop request");
    kt_until(&s, "resumes 3", 3);
    EXPECT(bordered(&s), "screen intact after resuming");
    finish(&s, "q", 0, 0);
    puts("PASS hand-off: exec on a cooked terminal, suspend, SIGTSTP and SIGCONT re-enter and repaint");
}

int main(void) {
    kt_init();
    kt_paths(root, sizeof(root), build, sizeof(build));
    kt_format(fixture, sizeof(fixture), "%s/kettle-program-fixture", build);
    observer_repaint();
    frames();
    resizing();
    terminations();
    crashes();
    lending();
    return 0;
}
