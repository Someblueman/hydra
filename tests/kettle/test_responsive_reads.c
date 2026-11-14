#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* No key waits on a subprocess. The control centre runs against a CLI and a
 * tmux whose read-only answers take seconds, and every case presses the key
 * that starts such a read and then another key: the second key must take
 * effect while the read is still running, the view must say that it is
 * loading, and the answer must be shown when it arrives. Repeating a refresh
 * key never runs two reads of one source at a time. */

static char root[4096], build[4096], tui[4096], base[4096];
static char hydra[4096], slow[4096], calls[4096];

#define READ_SECONDS "2.5"
#define PANE_SECONDS "0.9"
/* A key must act within this long: far less than any delayed read. */
#define PROMPT 0.7
#define S(keys) kt_send(&s, (keys))
#define U(marker, timeout) kt_until(&s, (marker), (timeout))
#define HAS(marker) kt_contains(&s, (marker))

static const char hydra_script[] =
    "#!/bin/sh\n"
    "# The fixture CLI; workflow reads are logged and, while the marker file\n"
    "# exists, slow.\n"
    "call=\"${1:-}:${2:-}\"\n"
    "case \"$call\" in\n"
    "    workflow:tui-data|workflow:statistics-data)\n"
    "        printf 'start %s\\n' \"$call\" >> \"$RR_CALLS\"\n"
    "        if [ -f \"$RR_SLOW\" ]; then sleep " READ_SECONDS "; fi\n"
    "        \"$RR_FAKE\" \"$@\"; status=$?\n"
    "        printf 'end %s\\n' \"$call\" >> \"$RR_CALLS\"\n"
    "        exit \"$status\" ;;\n"
    "esac\n"
    "exec \"$RR_FAKE\" \"$@\"\n";

static const char tmux_script[] =
    "#!/bin/sh\n"
    "# The fixture tmux; pane captures and history commands are slow while the\n"
    "# marker file exists.\n"
    "case \"${1:-}\" in\n"
    "    capture-pane|copy-mode) if [ -f \"$RR_SLOW\" ]; then sleep " PANE_SECONDS "; fi ;;\n"
    "esac\n"
    "exec \"$RR_TMUX\" \"$@\"\n";

static void executable(const char *path, const char *text) {
    kt_write(path, text);
    CHECK(!chmod(path, 0755), "fixture script is executable");
}

static void environment(const char *name, const char *value) {
    CHECK(!setenv(name, value, 1), "fixture environment");
}

static void fixture(void) {
    char path[8192], value[8192];
    kt_paths(root, sizeof(root), build, sizeof(build));
    kt_format(tui, sizeof(tui), "%s/hydra-tui", build);
    kt_temp(base, sizeof(base), "hydra-responsive");
    kt_format(path, sizeof(path), "%s/bin", base);
    kt_mkdir(path);
    kt_format(hydra, sizeof(hydra), "%s/hydra", base);
    executable(hydra, hydra_script);
    kt_format(path, sizeof(path), "%s/bin/tmux", base);
    executable(path, tmux_script);
    kt_format(slow, sizeof(slow), "%s/slow", base);
    kt_format(calls, sizeof(calls), "%s/calls", base);
    kt_format(value, sizeof(value), "%s/tests/fixtures/tui/fake-hydra.sh", root);
    environment("RR_FAKE", value);
    kt_format(value, sizeof(value), "%s/tests/fixtures/tui/fake-bin/tmux", root);
    environment("RR_TMUX", value);
    environment("RR_SLOW", slow);
    environment("RR_CALLS", calls);
    kt_format(value, sizeof(value), "%s/bin:%s", base, getenv("PATH") ? getenv("PATH") : "");
    environment("PATH", value);
    environment("FAKE_TMUX_SESSIONS", "hydra-feature-live");
    environment("TERM", "xterm-256color");
    unsetenv("TMUX");
    unsetenv("NO_COLOR");
}

static void open_view(struct kt_session *s, const char *view) {
    const char *argv[] = {tui, "--hydra", hydra, "--view", view, NULL};
    kt_write(calls, "");
    kt_write(slow, "");
    kt_open_terminal(s, argv, 140, 40, NULL);
}

/* The key just sent is still being answered by a subprocess; help opens and
 * closes regardless. */
static void keys_answered(struct kt_session *s, const char *what) {
    double sent = kt_now();
    kt_send(s, "?");
    kt_until(s, "KEYBOARD HELP", PROMPT);
    printf("%s: help opened %.0f ms after the key\n", what, (kt_now() - sent) * 1000);
    kt_send(s, "?");
    kt_pump(s, .1);
    CHECK(!kt_contains(s, "KEYBOARD HELP"), "help closed");
}

/* The largest number of reads of one source that ran at the same time, and
 * how many started, by the fixture CLI's own log. */
static int reads_overlapping(const char *source, int *started) {
    static char log[65536];
    char begin[128], end[128];
    const char *line;
    int running = 0, most = 0;
    kt_format(begin, sizeof(begin), "start %s", source);
    kt_format(end, sizeof(end), "end %s", source);
    kt_read(calls, log, sizeof(log));
    *started = 0;
    for (line = log; *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : line + strlen(line)) {
        if (!strncmp(line, begin, strlen(begin)) && line[strlen(begin)] == '\n') { running++; (*started)++; }
        else if (!strncmp(line, end, strlen(end)) && line[strlen(end)] == '\n') running--;
        if (running > most) most = running;
    }
    return most;
}

/* Work lists runs once they are read; until then its list says so. */
static void workflows(void) {
    struct kt_session s;
    open_view(&s, "heads");
    U("feature-live", 5);
    U("Reading runs", PROMPT);
    keys_answered(&s, "Work");
    CHECK(HAS("Reading runs"), "the list still says it is reading runs while the read runs");
    U("run visualizati", 6);
    CHECK(!HAS("Reading runs"), "the runs replace the loading line");
    kt_close(&s, "q", 0, 0);
    puts("PASS workflows: Work opens at once, says it is reading runs and lists them when read");
}

static void statistics(void) {
    struct kt_session s;
    int started, most;
    open_view(&s, "heads");
    U("feature-live", 5);
    S("D");
    U("Loading statistics...", PROMPT);
    keys_answered(&s, "Statistics");
    U("Latest attempts average", 6);
    CHECK(!HAS("Loading statistics..."), "the sample replaces the loading line");
    /* Twelve refreshes (ctrl+r) in a row, then a key that changes the view. */
    kt_write(calls, "");
    S("\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12\x12T");
    U("24 hours", PROMPT);
    kt_pump(&s, 4);
    most = reads_overlapping("workflow:statistics-data", &started);
    printf("statistics reads for 12 refresh keys: %d started, at most %d at a time\n", started, most);
    CHECK(most == 1, "one statistics read in flight at a time");
    CHECK(started >= 1 && started <= 4, "repeated refresh keys do not pile up reads");
    /* g opens the run's Run page in Work from what is known and asks for a
     * fresh read. */
    S("0g");
    U("STEP", 6);
    keys_answered(&s, "graph");
    kt_close(&s, "q", 0, 0);
    puts("PASS statistics: the view opens at once, ctrl+r and g never wait, refreshes run one at a time");
}

/* The selected head's latest output is read in the background: the
 * conversation says so until it arrives. */
static void preview(void) {
    struct kt_session s;
    open_view(&s, "heads");
    U("feature-live", 5);
    U("Reading the terminal output...", PROMPT);
    keys_answered(&s, "preview");
    U("pane text", 4);
    CHECK(!HAS("Reading the terminal output..."), "the pane's output replaces the loading line");
    /* The refresh that reads the pane again keeps the output on screen. */
    kt_pump(&s, 2.5);
    CHECK(HAS("pane text") && !HAS("Reading the terminal output..."), "a later read keeps what is known on screen");
    kt_close(&s, "q", 0, 0);
    puts("PASS preview: the conversation says it is reading and shows the pane when read");
}

/* Entering an agent's history asks tmux; the chord that follows it is not
 * kept waiting. The fixture tmux has no history, so the client's own scroll
 * takes over once tmux has answered. */
static void history(void) {
    struct kt_session s;
    double sent;
    open_view(&s, "heads");
    U("feature-live", 5);
    S("a");
    U("typing to ", 5);
    U("FAKE AGENT PANE", 5);
    sent = kt_now();
    S("\034[\035");
    U("Keys go to Hydra", PROMPT);
    printf("history: the next chord acted %.0f ms after it\n", (kt_now() - sent) * 1000);
    kt_close(&s, "q", 0, 0);
    puts("PASS history: asking tmux for an agent's history does not hold the next key");
}

int main(void) {
    const char *remove[] = {"rm", "-rf", base, NULL};
    kt_init();
    fixture();
    workflows();
    statistics();
    preview();
    history();
    kt_command_ok(NULL, remove);
    return 0;
}
