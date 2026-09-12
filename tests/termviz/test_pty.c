#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char root[4096], build[4096], demo[4096], child[4096], evidence[4096];
static void observer_resize(void) {
    const unsigned char frame[] = "\033[2J\033[H界";
    size_t split;
    for (split = 1; split < sizeof(frame) - 1; split++) {
        struct tv_screen s = {0};
        tv_screen_reset(&s, 80, 24, false);
        tv_screen_feed(&s, frame, split);
        tv_screen_reset(&s, 40, 10, true);
        tv_screen_feed(&s, frame + split, sizeof(frame) - 1 - split);
        if (split < 4)
            CHECK(!s.awaiting_clear && !strcmp(s.cells[0].text, "界"),
                  "resize completes a split clear sequence");
        tv_screen_feed(&s, frame, sizeof(frame) - 1);
        CHECK(!s.awaiting_clear && !s.overflow && !strcmp(s.cells[0].text, "界"),
              "resize preserves split UTF-8 and CSI streams");
        free(s.cells);
        free(s.text);
    }
    puts("PASS observer: resize at every UTF-8 and CSI byte boundary");
}
static void save(struct tv_session *s, const char *name) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s", evidence, name);
    tv_save(s, path);
}
static void workspace(void) {
    struct tv_session s;
    int before;
    const char *argv[] = {demo, NULL};
    tv_open(&s, argv, 80, 24, NULL);
    tv_until(&s, "ACTIVITY", 3);
    tv_pump(&s, .1);
    CHECK(tv_pump(&s, .2) == 0, "unchanged frame emitted bytes");
    before = s.screen.clears;
    tv_send(&s, "\tjjj");
    tv_until(&s, "pane 3 / offset 3", 3);
    tv_send(&s, "\tjj");
    tv_until(&s, "pane 4 / offset 2", 3);
    CHECK(tv_contains(&s, "pane 3 / offset 3"), "independent pane scroll");
    tv_send(&s, "\t");
    tv_pump(&s, .1);
    tv_send(&s, "h");
    tv_pump(&s, .1);
    CHECK(!tv_contains(&s, "renderer"), "collapsed branch");
    tv_send(&s, "l");
    tv_until(&s, "renderer", 3);
    tv_send(&s, "\033[<0;19;3M\033[<32;30;3M\033[<0;30;3m");
    tv_pump(&s, .15);
    CHECK(!strcmp(s.screen.cells[2 * s.screen.cols + 28].text, "│"), "mouse divider position");
    CHECK(s.screen.clears == before, "incremental interaction");
    save(&s, "workspace-80x24.html");
    tv_resize(&s, 40, 10);
    tv_send(&s, "\t");
    tv_pump(&s, .15);
    CHECK(tv_contains(&s, "FOCUS") && tv_contains(&s, "q quit"), "small workspace controls");
    CHECK(!s.screen.overflow, "40x10 overflow");
    save(&s, "workspace-40x10.html");
    tv_resize(&s, 140, 40);
    tv_until(&s, "INSPECT", 3);
    CHECK(!s.screen.overflow, "140x40 overflow");
    save(&s, "workspace-140x40.html");
    tv_close(&s, "q", 0, 0);
    puts("PASS workspace: tree, independent scroll, focus, mouse, incremental output, three sizes, "
         "termios");
}
static void shell_test(void) {
    struct tv_session s;
    char groups[2][256], model[2][256], reported[2][256], command[8192];
    pid_t pid;
    const char *argv[] = {demo, "--shell", NULL};
    tv_open(&s, argv, 140, 40, NULL);
    tv_until(&s, "TV$ ", 3);
    tv_send(&s, "printf '\\033[31mSHELL_%s\\033[0m\\n' OK; false; printf 'STATUS:%s\\n' \"$?\"\r");
    tv_until(&s, "SHELL_OK", 3);
    tv_until(&s, "STATUS:1", 3);
    CHECK(tv_match(&s, "Child PID: ([0-9]+)", groups, 1), "shell PID");
    pid = (pid_t)atoi(groups[0]);
    tv_send(&s, "i=0; while [ $i -lt 700 ]; do printf 'HISTORY_%03d\\n' $i; i=$((i+1)); done\r");
    tv_until(&s, "HISTORY_699", 3);
    tv_until(&s, "History: 512 / 512 rows", 3);
    tv_send(&s, "\002[k");
    tv_pump(&s, .2);
    CHECK(tv_contains(&s, "offset 1"), "history scroll");
    tv_send(&s, "\002]");
    tv_send(
        &s,
        "i=0; while [ $i -lt 12 ]; do printf 'STREAM_%02d\\n' $i; i=$((i+1)); sleep 0.1; done\r");
    tv_send(&s, "\002\t");
    tv_until(&s, "FOCUS pane 4", 3);
    tv_until(&s, "STREAM_11", 3);
    tv_send(&s, "\002\t\002\t");
    tv_format(command, sizeof(command), "%s\r", child);
    tv_send(&s, command);
    tv_until(&s, "ALT_READY", 3);
    tv_until(&s, "Screen: alternate", 3);
    tv_send(&s, "x");
    tv_until(&s, "SAFE_OUTPUT", 3);
    CHECK(!strstr(s.raw, "]52;") && !strstr(s.raw, "SHOULD_NOT_ESCAPE"), "escape isolation");
    tv_resize(&s, 80, 24);
    tv_send(&s, "r");
    tv_until(&s, "SIZE:", 3);
    CHECK(tv_match(&s, "Terminal: ([0-9]+)x([0-9]+)", model, 2) &&
              tv_match(&s, "SIZE:([0-9]+):([0-9]+)", reported, 2),
          "size reports");
    CHECK(!strcmp(model[0], reported[1]) && !strcmp(model[1], reported[0]), "child/model geometry");
    save(&s, "shell-80x24.html");
    tv_resize(&s, 40, 10);
    tv_send(&s, "r");
    tv_until(&s, "SIZE:", 3);
    CHECK(!s.screen.overflow, "small shell overflow");
    save(&s, "shell-40x10.html");
    tv_resize(&s, 140, 40);
    tv_send(&s, "r");
    tv_until(&s, "ALT_READY", 3);
    save(&s, "shell-140x40.html");
    tv_send(&s, "q");
    tv_until(&s, "AFTER_ALT", 3);
    tv_send(&s, "printf 'ALT_STATUS:%s\\n' \"$?\"\r");
    tv_until(&s, "ALT_STATUS:7", 3);
    tv_send(&s, "exit 9\r");
    tv_until(&s, "exited / status 9", 3);
    CHECK(kill(pid, 0) < 0 && errno == ESRCH, "shell reaped");
    tv_close(&s, "\002q", 0, 0);
    puts("PASS shell: command status, bounded history, stream focus, alternate client, escape "
         "isolation, resize, reap");
}
static void interruption(void) {
    struct tv_session s;
    char groups[1][256], out[1024];
    const char *argv[] = {demo, "--shell", NULL};
    tv_open(&s, argv, 140, 40, NULL);
    tv_until(&s, "TV$ ", 3);
    tv_send(&s, "sleep 30 & printf 'JOB_PID:'; jobs -p; wait\r");
    tv_until_match(&s, "JOB_PID:([0-9]+)", groups, 1, 3);
    tv_close(&s, NULL, SIGTERM, 143);
    {
        const char *ps[] = {"ps", "-o", "stat=", "-p", groups[0], NULL};
        char *p = out;
        (void)tv_command(NULL, NULL, out, sizeof(out), 5, ps);
        while (*p == ' ' || *p == '\n')
            p++;
        CHECK(!*p || *p == 'Z', "owned job stopped");
    }
    {
        const char *bad[] = {demo, "--command", "/nonexistent/termviz-child", NULL};
        tv_open(&s, bad, 80, 24, NULL);
        tv_until(&s, "exited / status 127", 3);
        tv_close(&s, "\002q", 0, 0);
    }
    puts("PASS lifecycle: interruption, owned job termination, visible exec failure, termios");
}
static void hydra(void) {
    struct tv_session s;
    char tui[4096], fake[4096];
    int before;
    tv_format(tui, sizeof(tui), "%s/hydra-tui", build);
    tv_format(fake, sizeof(fake), "%s/tests/fixtures/tui/fake-hydra.sh", root);
    {
        const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
        tv_open(&s, argv, 140, 40, NULL);
    }
    tv_until(&s, "HYDRA WORKSPACE", 3);
    tv_until(&s, "Observed: LIVE", 3);
    before = s.screen.clears;
    tv_send(&s, "j");
    tv_until(&s, "Observed: STALE", 3);
    tv_send(&s, "\tjjj\tjj");
    tv_pump(&s, .2);
    CHECK(tv_contains(&s, "scroll 3") && tv_contains(&s, "scroll 2"), "Hydra independent scroll");
    tv_send(&s, "\033[<0;42;4M\033[<32;50;4M\033[<0;50;4m");
    tv_pump(&s, .3);
    CHECK(s.screen.clears == before, "Hydra incremental drag");
    save(&s, "hydra-140x40.html");
    tv_resize(&s, 80, 24);
    tv_until(&s, "NAVIGATION", 3);
    CHECK(!s.screen.overflow, "Hydra medium overflow");
    save(&s, "hydra-80x24.html");
    tv_resize(&s, 40, 10);
    tv_until(&s, "FOCUS", 3);
    CHECK(tv_contains(&s, "q quit") && !s.screen.overflow, "Hydra small frame");
    save(&s, "hydra-40x10.html");
    tv_close(&s, "q", 0, 0);
    puts("PASS Hydra: adapter rows, selection, pane scroll, divider, resize and restoration");
}
static void hydra_startup_open(struct tv_session *s, const char *tui, const char *fake) {
    const char *argv[] = {tui, "--hydra", fake, NULL};
    tv_open(s, argv, 140, 40, NULL);
    tv_until(s, "Loading snapshot...", 3);
    tv_until(s, "visualization-proof", 3);
    CHECK(tv_contains(s, "No matching head"), "workflow arrives before delayed heads");
}
static void hydra_startup_repopulation(struct tv_session *s, const char *model, const char *data) {
    tv_send(s, "C");
    tv_until(s, "Observed: LIVE", 3);
    CHECK(tv_contains(s, "*  └ feature-live"), "first head selected after mode input");
    tv_send(s, "Aj");
    tv_until(s, "Observed: STALE", 3);
    CHECK(tv_contains(s, ">  └ feature-stale"), "single move selects second head");
    tv_write(model, "HYDRA_TUI\t2\n");
    tv_until(s, "No matching head", 3);
    tv_write(model, data);
    tv_until(s, "Observed: LIVE", 3);
    CHECK(tv_contains(s, ">- Current project"), "repopulation does not reseed selection");
}
static void hydra_startup_selection(void) {
    struct tv_session s;
    char tui[4096], fake[4096], source[4096], model[4096], data[8192], name[64];
    const char *keys[] = {"k", "j", "\r"};
    const char *selected[] = {">- Current project", ">  └ visualization-proof", ">+ Current project"};
    size_t i;
    tv_format(tui, sizeof(tui), "%s/hydra-tui", build);
    tv_format(fake, sizeof(fake), "%s/tests/fixtures/tui/fake-hydra.sh", root);
    tv_format(source, sizeof(source), "%s/tests/fixtures/tui/native-v2.tsv", root);
    tv_format(model, sizeof(model), "%s/startup-model.tsv", evidence);
    tv_read(source, data, sizeof(data));
    CHECK(!setenv("HYDRA_TEST_TUI_DELAY", "1", 1) && !setenv("HYDRA_TUI_FIXTURE", model, 1),
          "configure delayed startup snapshot");
    tv_write(model, data);
    hydra_startup_open(&s, tui, fake);
    hydra_startup_repopulation(&s, model, data);
    save(&s, "startup-repopulation.html");
    tv_close(&s, "q", 0, 0);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        hydra_startup_open(&s, tui, fake);
        tv_send(&s, keys[i]);
        tv_until(&s, "Observed: LIVE", 3);
        CHECK(tv_contains(&s, selected[i]), "explicit root/run/collapse retained after heads arrive");
        tv_format(name, sizeof(name), "startup-selection-%zu.html", i);
        save(&s, name);
        tv_close(&s, "q", 0, 0);
    }
    CHECK(!unsetenv("HYDRA_TEST_TUI_DELAY") && !unsetenv("HYDRA_TUI_FIXTURE"),
          "restore startup fixture environment");
    puts("PASS Hydra startup: first head, single move, explicit root/run/collapse, repopulation");
}
int main(int argc, char **argv) {
    tv_init();
    observer_resize();
    tv_paths(root, sizeof(root), build, sizeof(build));
    tv_format(demo, sizeof(demo), "%s/termviz-workspace", build);
    tv_format(child, sizeof(child), "%s/test-workspace-child", build);
    tv_format(evidence, sizeof(evidence), "%s/workspace-evidence", build);
    tv_mkdir(evidence);
    CHECK(argc == 1 || (argc == 2 && !strcmp(argv[1], "--standalone")),
          "usage: test-pty [--standalone]");
    workspace();
    shell_test();
    interruption();
    if (argc == 1) {
        hydra();
        hydra_startup_selection();
    }
    return 0;
}
