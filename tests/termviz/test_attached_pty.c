#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include "tui/fleet_budget.h"
#include <errno.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static struct hf_fixture f;
static char evidence[4096];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define REFUSE(...) hf_run(&f, NULL, -2, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) tv_send(&s, (keys))
#define U(marker, timeout) tv_until(&s, (marker), (timeout))
static void proof_path(char *out, size_t capacity, const char *dir, const char *name) {
    tv_format(out, capacity, "%s/%s", dir, name);
}
static void proof(const char *dir, const char *name, const char *value) {
    char path[4096];
    proof_path(path, sizeof(path), dir, name);
    if (!tv_file_equals(path, value))
        fprintf(stderr, "Expected %s to contain %s\n", path, value);
    CHECK(tv_file_equals(path, value), "attached command evidence");
}
static bool exists(const char *dir, const char *name) {
    char path[4096];
    proof_path(path, sizeof(path), dir, name);
    return tv_exists(path);
}
static void save(struct tv_session *s, const char *name, int cols, int rows) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, cols, rows);
    tv_save(s, path);
}
static void column(const char *line, int column, char *out, size_t capacity) {
    const char *p = line, *end;
    int i;
    for (i = 0; i < column; i++) {
        p = strchr(p, '\t');
        CHECK(p, "TSV field exists");
        p++;
    }
    end = strpbrk(p, "\t\n");
    if (!end)
        end = p + strlen(p);
    CHECK((size_t)(end - p) < capacity, "TSV field capacity");
    memcpy(out, p, (size_t)(end - p));
    out[end - p] = 0;
}
static void history(const char *screen, char *out, size_t cap) {
    regex_t r;
    regmatch_t m;
    size_t at = 0;
    CHECK(!regcomp(&r, "HISTORY_A:[0-9]+", REG_EXTENDED), "history regex");
    while (!regexec(&r, screen, 1, &m, 0)) {
        size_t n = (size_t)(m.rm_eo - m.rm_so);
        CHECK(at + n + 2 < cap, "history match bound");
        memcpy(out + at, screen + m.rm_so, n);
        at += n;
        out[at++] = '\n';
        screen += m.rm_eo;
    }
    out[at] = 0;
    regfree(&r);
}
int main(void) {
    struct tv_session s;
    char path[4096], one[4096], two[4096], pids[8192], branch[2][256], session[256], head[256],
        rows[65536], slow[4096], started[4096], observation[4096], adapter[4096], script[32768],
        hist_a[8192], hist_after[8192];
    int sizes[][2] = {{80, 24}, {40, 10}, {140, 40}};
    size_t i;
    const char *observed[] = {"exited exact\n", "exited reported\n", "failed exact\n"};
    const char *labels[] = {"EXIT RECORDED", "AGENT UNKNOWN", "FAIL RECORDED"};
    const long snapshot_delay = (HYDRA_TUI_LOCAL_CAPTURE_BUDGET_MS + 999L) / 1000L + 2L;
    tv_init();
    hf_init(&f, "hydra-attached", "repo", false, true);
    setenv("NO_COLOR", "1", 1);
    hf_file(&f, "input", path, sizeof(path));
    tv_write(path, "attachment\n");
    hf_commit_init(&f);
    H("spawn", "one", "--no-agent");
    H("spawn", "two", "--no-agent");
    tv_format(pids, sizeof(pids), "%s", RUN("tmux", "list-panes", "-a", "-F", "#{pane_pid}"));
    tv_format(rows, sizeof(rows), "%s", H("tui", "--data"));
    {
        const char *p = rows;
        int n = 0;
        while (*p && n < 2) {
            const char *next = strchr(p, '\n');
            if (!strncmp(p, "H\t", 2)) {
                column(p, 1, branch[n], sizeof(branch[n]));
                if (n == 0) {
                    column(p, 2, session, sizeof(session));
                    column(p, 24, head, sizeof(head));
                }
                n++;
            }
            if (!next)
                break;
            p = next + 1;
        }
        CHECK(n == 2, "two head rows");
    }
    tv_format(one, sizeof(one), "%s", H("path", branch[0]));
    hf_trim(one);
    tv_format(two, sizeof(two), "%s", H("path", branch[1]));
    hf_trim(two);
    CHECK(strstr(REFUSE(f.hydra, "tui", "--attach", head, "instance_00000000000000000000"),
                 "no longer current"),
          "stale instance refusal");
    tv_format(slow, sizeof(slow), "%s/slow", f.base);
    tv_format(started, sizeof(started), "%s/started", f.base);
    tv_format(observation, sizeof(observation), "%s/observation-fixture", f.base);
    tv_format(adapter, sizeof(adapter), "%s/hydra-adapter", f.base);
    setenv("PTY_FIXTURE", f.base, 1);
    setenv("PTY_HYDRA", f.hydra, 1);
    tv_format(script, sizeof(script),
              "#!/bin/sh\nif [ \"$1:$2\" = tui:--data ] && [ -f \"$PTY_FIXTURE/slow\" ]; then\n  "
              "touch \"$PTY_FIXTURE/started\"\n  sleep %ld\nfi\nif [ \"$1:$2\" = tui:--data ] && [ "
              "-f \"$PTY_FIXTURE/observation-fixture\" ]; then\n  read -r observed confidence < "
              "\"$PTY_FIXTURE/observation-fixture\"\n  \"$PTY_HYDRA\" \"$@\" | awk -F '\\t' -v "
              "OFS='\\t' -v observed=\"$observed\" -v confidence=\"$confidence\" '$1==\"H\" "
              "{$10=observed; $11=confidence} {print}'\n  exit\nfi\nexec \"$PTY_HYDRA\" \"$@\"\n",
              snapshot_delay);
    tv_write(adapter, script);
    CHECK(!chmod(adapter, 0755), "adapter mode");
    tv_format(evidence, sizeof(evidence), "%s/attached-evidence", f.build);
    tv_mkdir(evidence);
    {
        const char *argv[] = {f.tui, "--hydra", adapter, NULL};
        tv_open(&s, argv, 140, 40, f.repo);
    }
    U("HYDRA WORKSPACE", 3);
    U("Observed: LIVE", 3);
    S("a");
    U("INPUT TO AGENT", 3);
    U("AGENT UNKNOWN", 3);
    for (i = 0; i < 3; i++) {
        tv_write(observation, observed[i]);
        U(labels[i], 8);
    }
    CHECK(!unlink(observation), "remove observation fixture");
    U("AGENT UNKNOWN", 8);
    tv_pump(&s, .5);
    S("printf 'one' > attachment-proof\r");
    tv_pump(&s, .5);
    proof(one, "attachment-proof", "one");
    tv_write(slow, "");
    for (i = 0; i < 30 && !tv_exists(started); i++)
        tv_pump(&s, .1);
    CHECK(tv_exists(started), "delayed observation started");
    S("printf 'responsive' > latency-proof\r");
    tv_pump(&s, .4);
    proof(one, "latency-proof", "responsive");
    U("STALE: last good", snapshot_delay);
    CHECK(!unlink(slow), "remove delay trigger");
    U("Current snapshot", 6);
    S("(sleep 1; printf 'FORM_LIVE' > form-proof; printf 'FORM_LIVE\\n') &\r");
    tv_pump(&s, .2);
    S("\002\t/");
    U("INPUT TO HYDRA / text field", 3);
    tv_pump(&s, 1.2);
    proof(one, "form-proof", "FORM_LIVE");
    CHECK(tv_contains(&s, "FORM_LIVE"), "form does not block output");
    S("\033[200~search\nq\033[201~");
    U("searchq", 3);
    CHECK(tv_contains(&s, "INPUT TO HYDRA / text field"), "paste did not submit form");
    S("\033[H界\033[C\177");
    U("界earchq", 3);
    S("\033");
    tv_pump(&s, .2);
    CHECK(!tv_contains(&s, "INPUT TO HYDRA / text field"), "form dismiss");
    S("\t\t");
    U("INPUT TO AGENT", 3);
    S("sleep 15\r");
    tv_pump(&s, .2);
    S("\003");
    tv_pump(&s, .2);
    S("printf 'interrupted' > interruption-proof\r");
    tv_pump(&s, .4);
    proof(one, "interruption-proof", "interrupted");
    {
        int status;
        CHECK(waitpid(s.pid, &status, WNOHANG) == 0, "Ctrl-C targets attachment");
    }
    S("\033[200~printf 'paste' > paste-proof\033[201~\r");
    tv_pump(&s, .4);
    proof(one, "paste-proof", "paste");
    S("printf 'draft' > draft-proof");
    tv_pump(&s, .2);
    S("\002B");
    U("B PLAN OVERVIEW", 3);
    S("\002C");
    U("C MONITOR", 3);
    S("\002A");
    U("A CONVERSATION", 3);
    S("\002D");
    U("D STATISTICS", 3);
    CHECK(!exists(one, "draft-proof"), "mode switching preserves unsent draft");
    S("D");
    U("INPUT TO AGENT", 3);
    S("\r");
    tv_pump(&s, .5);
    proof(one, "draft-proof", "draft");
    S("\002\t\tj");
    tv_pump(&s, .2);
    S("a");
    U("INPUT TO AGENT", 3);
    tv_pump(&s, .5);
    S("printf 'two' > attachment-proof\r");
    tv_pump(&s, .5);
    proof(two, "attachment-proof", "two");
    S("\002n");
    tv_pump(&s, .2);
    S("printf 'returned' > returned-proof\r");
    tv_pump(&s, .5);
    proof(one, "returned-proof", "returned");
    S("printf 'multi-one' > multi-proof");
    tv_pump(&s, .1);
    S("\002S");
    U("Two attached agents", 3);
    S("\002\t");
    S("printf 'multi-two' > multi-proof");
    tv_pump(&s, .2);
    CHECK(hf_count(tv_text(&s), "ATTACHED") == 2, "two attached panes");
    S("\002B\002S");
    tv_pump(&s, .3);
    CHECK(hf_count(tv_text(&s), "ATTACHED") == 2, "two panes in plan");
    S("\002C\002S");
    tv_pump(&s, .3);
    CHECK(hf_count(tv_text(&s), "ATTACHED") == 2, "two panes in monitor");
    S("\002D");
    U("D STATISTICS", 3);
    S("D\002A");
    for (i = 0; i < 3; i++) {
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        tv_pump(&s, .3);
        CHECK(!s.screen.overflow, "two-agent resize");
        CHECK(!exists(one, "multi-proof") && !exists(two, "multi-proof"),
              "resize preserved drafts");
        save(&s, "two-agents", sizes[i][0], sizes[i][1]);
    }
    CHECK(hf_count(tv_text(&s), "ATTACHED") == 2, "two agents remain attached");
    S("\r");
    tv_pump(&s, .4);
    proof(two, "multi-proof", "multi-two");
    CHECK(!exists(one, "multi-proof"), "other agent draft unsent");
    S("\002n\r");
    tv_pump(&s, .4);
    proof(one, "multi-proof", "multi-one");
    S("i=0; while [ \"$i\" -lt 50 ]; do echo HISTORY_A:$i; i=$((i+1)); done\r");
    tv_pump(&s, .4);
    S("\002[kkkkk");
    tv_pump(&s, .2);
    history(tv_text(&s), hist_a, sizeof(hist_a));
    CHECK(*hist_a, "first agent history");
    S("\002\t");
    S("i=0; while [ \"$i\" -lt 50 ]; do echo HISTORY_B:$i; i=$((i+1)); done\r");
    tv_pump(&s, .4);
    S("\002[kkkkk\002n");
    tv_pump(&s, .2);
    history(tv_text(&s), hist_after, sizeof(hist_after));
    CHECK(!strcmp(hist_a, hist_after), "independent scrollback");
    S("\002]\002n\002]\002n");
    tv_pump(&s, .2);
    {
        int x, y, hit_x = -1, hit_y = -1;
        size_t b;
        for (y = 0; y < s.screen.rows; y++)
            for (x = 0; x < s.screen.cols; x++)
                if (!strcmp(s.screen.cells[y * s.screen.cols + x].text, "─")) {
                    bool matches = true;
                    for (b = 0; branch[1][b]; b++) {
                        if (x + 1 + (int)b >= s.screen.cols ||
                            s.screen.cells[y * s.screen.cols + x + 1 + (int)b].text[0] !=
                                branch[1][b]) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        hit_x = x;
                        hit_y = y;
                        break;
                    }
                }
        CHECK(hit_x >= 0, "second agent border rendered");
        tv_format(script, sizeof(script), "\033[<0;%d;%dM\033[<0;%d;%dm", hit_x + 4, hit_y + 4,
                  hit_x + 4, hit_y + 4);
        S(script);
    }
    S("printf 'mouse-focus' > mouse-multi-proof\r");
    tv_pump(&s, .4);
    proof(two, "mouse-multi-proof", "mouse-focus");
    CHECK(!exists(one, "mouse-multi-proof"), "mouse focus exact agent");
    S("\002n\002S");
    tv_pump(&s, .2);
    S("printf 'reconnected' > reconnect-proof");
    tv_pump(&s, .1);
    S("\002x");
    tv_pump(&s, .2);
    CHECK(!exists(one, "reconnect-proof"), "detach retains unsent draft");
    S("a");
    U("INPUT TO AGENT", 3);
    tv_pump(&s, .4);
    S("\r");
    tv_pump(&s, .4);
    proof(one, "reconnect-proof", "reconnected");
    S("printf 'zoom-draft' > zoom-proof");
    tv_pump(&s, .1);
    S("\002z");
    U("z restore panes", 3);
    CHECK(!tv_contains(&s, "NAVIGATION"), "zoom hides panes");
    S("\002D");
    U("D STATISTICS", 3);
    S("D");
    U("z restore panes", 3);
    S("\002z");
    U("NAVIGATION", 3);
    CHECK(!exists(one, "zoom-proof"), "zoom retains draft");
    S("\r");
    tv_pump(&s, .4);
    proof(one, "zoom-proof", "zoom-draft");
    save(&s, "attached", 140, 40);
    for (i = 0; i < 3; i++) {
        int term_rows = 0, term_cols = 0;
        char dimensions[256];
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        tv_pump(&s, .3);
        CHECK(!s.screen.overflow, "attached resize bounds");
        S("stty size > size-proof\r");
        tv_pump(&s, .3);
        proof_path(path, sizeof(path), one, "size-proof");
        tv_read(path, dimensions, sizeof(dimensions));
        CHECK(sscanf(dimensions, "%d %d", &term_rows, &term_cols) == 2, "terminal dimensions");
        if (sizes[i][0] == 40) {
            CHECK(term_rows >= 6 && term_cols >= 35, "compact conversation usable");
            CHECK(!tv_contains(&s, "NAVIGATION") && tv_contains(&s, "INPUT TO AGENT"),
                  "compact conversation view");
        }
        save(&s, "attached", sizes[i][0], sizes[i][1]);
    }
    tv_format(path, sizeof(path), "=%s", session);
    RUN("tmux", "detach-client", "-s", path);
    U("CLIENT DISCONNECTED", 3);
    CHECK(tv_contains(&s, "NO INPUT"), "disconnected client input disabled");
    S("\002r");
    U("INPUT TO AGENT", 3);
    tv_pump(&s, .4);
    S("printf 'resumed' > resumed-proof\r");
    tv_pump(&s, .4);
    proof(one, "resumed-proof", "resumed");
    tv_close(&s, "\002q", 0, 0);
    CHECK(!strcmp(RUN("tmux", "list-panes", "-a", "-F", "#{pane_pid}"), pids),
          "client exit preserves shell PIDs");
    RUN("tmux", "list-clients", "-F", "#{client_pid}");
    hf_trim(f.output);
    CHECK(!*f.output, "no leaked attached clients");
    hf_cleanup();
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS attached: confidence labels, asynchronous input/output, paste/Unicode/Ctrl-C, "
         "drafts, two clients, history, mouse, reconnect, resize and client-only exit");
    return 0;
}
