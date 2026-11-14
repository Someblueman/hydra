#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the control centre's loop guarantees, observed from outside on a real
 * terminal with a real attached tmux session: it is silent and asleep while
 * nothing happens, a key typed to an agent is echoed without waiting for a
 * tick, keys that arrive together cost one frame, and every frame is one
 * synchronized update that never erases the display. */

static struct hf_fixture f;
#define H(...) hf_run(&f, NULL, 0, (const char *[]){f.hydra, __VA_ARGS__, NULL})
#define S(keys) kt_send(&s, (keys))
#define U(marker, timeout) kt_until(&s, (marker), (timeout))

#define FRAME_BEGIN "\033[?2026h"
#define FRAME_END "\033[?2026l"
#define ERASE "\033[2J"

/* Everything written is a sequence of complete synchronized updates: no byte
 * outside one, none nested, none left open. */
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
static size_t frames_since(const struct kt_session *s, size_t mark) {
    return hf_count(s->raw + mark, FRAME_BEGIN);
}
/* Times the process chose to sleep, where the system reports it; -1 elsewhere. */
static long sleeps(pid_t pid) {
    char path[64], line[256];
    long count = -1;
    FILE *file;
    kt_format(path, sizeof(path), "/proc/%ld/status", (long)pid);
    file = fopen(path, "r");
    if (!file)
        return -1;
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "voluntary_ctxt_switches: %ld", &count) == 1)
            break;
    fclose(file);
    return count;
}
static int by_value(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* With nothing happening the terminal is sent nothing, and between refreshes
 * the process sleeps: a loop that polled on a 40 ms tick would wake 25 times
 * a second, which the bound below excludes. */
static void idle(struct kt_session *s) {
    long before, after;
    kt_pump(s, 2.0);
    before = sleeps(s->pid);
    CHECK(kt_pump(s, 6.5) == 0, "an idle control centre writes nothing across three refreshes");
    after = sleeps(s->pid);
    if (before >= 0 && after >= 0) {
        printf("idle wake-ups: %.1f per second\n", (double)(after - before) / 6.5);
        CHECK(after - before < 110, "an idle control centre sleeps between refreshes");
    }
}

/* 119 presses of the help key in one write leave help open after one frame,
 * not one frame per key. */
static void burst(struct kt_session *s) {
    char keys[120];
    size_t mark = s->raw_size, frames;
    memset(keys, '?', sizeof(keys) - 1);
    keys[sizeof(keys) - 1] = 0;
    kt_send(s, keys);
    kt_until(s, "KEYBOARD HELP", 3);
    kt_pump(s, .3);
    frames = frames_since(s, mark);
    printf("frames for 119 keys in one write: %zu\n", frames);
    CHECK(frames >= 1 && frames <= 4, "a burst of keys is drawn once, not once per key");
    kt_send(s, "?");
    kt_pump(s, .3);
    CHECK(!kt_contains(s, "KEYBOARD HELP"), "help closed");
}

/* An open prompt is part of the frame: it appears without an erase, is not
 * repainted while nothing changes, and leaves without an erase. */
static void prompt(struct kt_session *s) {
    size_t mark = s->raw_size;
    kt_send(s, "/");
    kt_until(s, "Filter heads", 3);
    kt_pump(s, .5);
    CHECK(kt_pump(s, 4.5) == 0, "an open, idle prompt writes nothing");
    kt_send(s, "on");
    kt_until(s, "\xe2\x94\x83 on", 3);
    kt_send(s, "\033");
    kt_pump(s, .5);
    CHECK(!kt_contains(s, "Filter heads"), "prompt closed");
    CHECK(!strstr(s->raw + mark, ERASE), "a prompt opens and closes without erasing the display");
}

/* A new size, another theme (dark, the default, to light) and another page of
 * Work's side pane each repaint by overwriting. */
static void repaints(struct kt_session *s) {
    size_t mark = s->raw_size;
    kt_send(s, "t");
    kt_until(s, "Theme: light", 3);
    kt_resize(s, 80, 24);
    kt_until(s, "Theme: light", 3);
    kt_resize(s, 140, 40);
    kt_until(s, "Theme: light", 3);
    kt_send(s, "\t\t");
    kt_send(s, "\034B");
    kt_until(s, "No Plan page", 3);
    kt_send(s, "\034A");
    kt_until(s, "Session     running", 3);
    kt_send(s, "\033");
    kt_pump(s, .3);
    CHECK(!strstr(s->raw + mark, ERASE), "theme, resize and page changes never erase the display");
}

/* The time from typing a character to an attached agent until its echo is on
 * the screen. The agent's PTY is watched, so the echo is drawn when it
 * arrives; reading it on a 40 ms tick would put the median near 40 ms. */
static void latency(struct kt_session *s) {
    enum { KEYS = 40, LINE = 8 };
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmn";
    char typed[64] = "", key[2] = {0, 0};
    double took[KEYS];
    int i;
    for (i = 0; i < KEYS; i++) {
        size_t length;
        double sent, limit;
        if (i % LINE == 0) {
            /* A fresh, short line with a prefix nothing else on screen has. */
            kt_send(s, "\025");
            kt_format(typed, sizeof(typed), "zq%dv", i / LINE);
            kt_send(s, typed);
            kt_until(s, typed, 5);
        }
        length = strlen(typed);
        key[0] = letters[i];
        typed[length] = key[0];
        typed[length + 1] = 0;
        kt_sleep(.05);
        sent = kt_now();
        limit = sent + 2;
        kt_send(s, key);
        while (!kt_contains(s, typed) && kt_now() < limit)
            kt_pump(s, .001);
        CHECK(kt_contains(s, typed), "typed character echoed");
        took[i] = (kt_now() - sent) * 1000;
    }
    qsort(took, KEYS, sizeof(took[0]), by_value);
    printf("attached key-to-echo: median %.1f ms, p95 %.1f ms, max %.1f ms over %d keys\n", took[KEYS / 2],
           took[KEYS * 95 / 100], took[KEYS - 1], KEYS);
    CHECK(took[KEYS / 2] < 15, "a key typed to an agent is echoed when the echo arrives, not on a tick");
    kt_send(s, "\025");
}

/* Agent output that arrives together is drawn together. */
static void attached_burst(struct kt_session *s) {
    char keys[121];
    size_t mark, frames;
    kt_pump(s, .3);
    mark = s->raw_size;
    memset(keys, 'x', sizeof(keys) - 1);
    keys[sizeof(keys) - 1] = 0;
    kt_send(s, keys);
    kt_until(s, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 5);
    kt_pump(s, .5);
    frames = frames_since(s, mark);
    printf("frames for 120 keys typed to an agent in one write: %zu\n", frames);
    CHECK(frames >= 1 && frames < 40, "a burst typed to an agent costs far fewer frames than keys");
    kt_send(s, "\025");
}

int main(void) {
    struct kt_session s;
    char path[4096];
    kt_init();
    hf_init(&f, "hydra-loop", "repo", false, true);
    hf_file(&f, "input", path, sizeof(path));
    kt_write(path, "loop\n");
    hf_commit_init(&f);
    H("spawn", "one", "--no-agent");
    hf_open(&f, &s);
    U("Heads \xe2\x94\x80", 5);
    U("Session     running", 10);
    U("\xe2\x97\x8f live", 10);
    U("repo \xc2\xb7 this machine", 10);
    idle(&s);
    S("1");
    U("Heads \xe2\x94\x80", 3);
    burst(&s);
    prompt(&s);
    repaints(&s);
    S("a");
    U("typing to ", 5);
    kt_pump(&s, 1);
    latency(&s);
    attached_burst(&s);
    CHECK(hf_count(s.raw, ERASE) == 1, "the display is erased once, when the control centre takes the screen");
    CHECK(frames_whole(s.raw), "all output is whole synchronized updates");
    kt_close(&s, "\034q", 0, 0);
    hf_finish(&f);
    puts("PASS interactive loop: silent and asleep when idle, prompt in the frame, overwrite repaints, "
         "prompt echo to an agent, bursts coalesced, whole synchronized updates");
    return 0;
}
