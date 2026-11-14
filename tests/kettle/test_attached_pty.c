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
#define S(keys) kt_send(&s, (keys))
#define U(marker, timeout) kt_until(&s, (marker), (timeout))
static void proof_path(char *out, size_t capacity, const char *dir, const char *name) {
    kt_format(out, capacity, "%s/%s", dir, name);
}
static void proof(const char *dir, const char *name, const char *value) {
    char path[4096];
    proof_path(path, sizeof(path), dir, name);
    if (!kt_file_equals(path, value))
        fprintf(stderr, "Expected %s to contain %s\n", path, value);
    CHECK(kt_file_equals(path, value), "attached command evidence");
}
static bool exists(const char *dir, const char *name) {
    char path[4096];
    proof_path(path, sizeof(path), dir, name);
    return kt_exists(path);
}
static void save(struct kt_session *s, const char *name, int cols, int rows) {
    char path[4096];
    kt_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, cols, rows);
    kt_save(s, path);
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
static void styled_output(struct kt_session *s) {
    bool colored = false;
    kt_send(s, "printf '\\033[31mCOLORED\\033[0m\\n\\033(0lqqk\\033(B\\ncaf\\303\\251 \\347\\225\\214\\n'\r");
    kt_until(s, "┌──┐", 5);
    kt_until(s, "café 界", 5);
    for (int i = 0; i < s->screen.cols * s->screen.rows; i++) {
        const struct kt_cell *cell = &s->screen.cells[i];
        if (!strcmp(cell->text, "C") && cell->fg == 0xcd3131) colored = true;
    }
    save(s, "unicode-graphics-color", s->screen.cols, s->screen.rows);
    CHECK(colored, "ANSI red survives the tmux and native render path");
}

/* A pane's title starts its rule at (x, y): "two ... ────". */
static bool pane_title_at(struct kt_session *s, int y, int x, const char *name) {
    char row[4096];
    size_t b;
    for (b = 0; name[b]; b++)
        if (x + (int)b >= s->screen.cols || s->screen.cells[y * s->screen.cols + x + (int)b].text[0] != name[b]) return false;
    return strstr(kt_row(s, y, row, sizeof(row)), "\xe2\x94\x80") != NULL;
}

int main(void) {
    struct kt_session s;
    char path[4096], one[4096], two[4096], pids[8192], branch[2][256], session[256], head[256],
        rows[65536], slow[4096], started[4096], observation[4096], adapter[4096], script[32768],
        hist_a[8192], hist_after[8192];
    int sizes[][2] = {{80, 24}, {40, 10}, {140, 40}};
    size_t i;
    const char *observed[] = {"exited exact\n", "exited reported\n", "failed exact\n"};
    const char *labels[] = {"shell · exit recorded", "shell · session running", "shell · failure recorded"};
    const long snapshot_delay = (HYDRA_TUI_LOCAL_CAPTURE_BUDGET_MS + 999L) / 1000L + 2L;
    kt_init();
    hf_init(&f, "hydra-attached", "repo", false, true);
    unsetenv("NO_COLOR"); /* This case explicitly verifies terminal color. */
    hf_file(&f, "input", path, sizeof(path));
    kt_write(path, "attachment\n");
    hf_commit_init(&f);
    H("spawn", "one", "--no-agent");
    H("spawn", "two", "--no-agent");
    kt_format(pids, sizeof(pids), "%s", RUN("tmux", "list-panes", "-a", "-F", "#{pane_pid}"));
    kt_format(rows, sizeof(rows), "%s", H("tui", "--data"));
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
    kt_format(one, sizeof(one), "%s", H("path", branch[0]));
    hf_trim(one);
    kt_format(two, sizeof(two), "%s", H("path", branch[1]));
    hf_trim(two);
    CHECK(strstr(REFUSE(f.hydra, "tui", "--attach", head, "instance_00000000000000000000"),
                 "no longer current"),
          "stale instance refusal");
    kt_format(slow, sizeof(slow), "%s/slow", f.base);
    kt_format(started, sizeof(started), "%s/started", f.base);
    kt_format(observation, sizeof(observation), "%s/observation-fixture", f.base);
    kt_format(adapter, sizeof(adapter), "%s/hydra-adapter", f.base);
    setenv("PTY_FIXTURE", f.base, 1);
    setenv("PTY_HYDRA", f.hydra, 1);
    kt_format(script, sizeof(script),
              "#!/bin/sh\nif [ \"$1:$2\" = tui:--data ] && [ -f \"$PTY_FIXTURE/slow\" ]; then\n  "
              "touch \"$PTY_FIXTURE/started\"\n  sleep %ld\nfi\nif [ \"$1:$2\" = tui:--data ] && [ "
              "-f \"$PTY_FIXTURE/observation-fixture\" ]; then\n  read -r observed confidence < "
              "\"$PTY_FIXTURE/observation-fixture\"\n  \"$PTY_HYDRA\" \"$@\" | awk -F '\\t' -v "
              "OFS='\\t' -v observed=\"$observed\" -v confidence=\"$confidence\" '$1==\"H\" "
              "{$10=observed; $11=confidence} {print}'\n  exit\nfi\nexec \"$PTY_HYDRA\" \"$@\"\n",
              snapshot_delay);
    kt_write(adapter, script);
    CHECK(!chmod(adapter, 0755), "adapter mode");
    kt_format(evidence, sizeof(evidence), "%s/attached-evidence", f.build);
    kt_mkdir(evidence);
    {
        const char *argv[] = {f.tui, "--hydra", adapter, NULL};
        kt_open_terminal(&s, argv, 140, 40, f.repo);
    }
    U("Heads \xe2\x94\x80", 3);
    U("Session     running", 3);
    S("a");
    U("typing to ", 3);
    U("shell · session running", 3);
    for (i = 0; i < 3; i++) {
        kt_write(observation, observed[i]);
        U(labels[i], 8);
    }
    CHECK(!unlink(observation), "remove observation fixture");
    U("shell · session running", 8);
    kt_pump(&s, .5);
    S("printf 'one' > attachment-proof\r");
    kt_pump(&s, .5);
    proof(one, "attachment-proof", "one");
    styled_output(&s);
    kt_write(slow, "");
    for (i = 0; i < 30 && !kt_exists(started); i++)
        kt_pump(&s, .1);
    CHECK(kt_exists(started), "delayed observation started");
    S("printf 'responsive' > latency-proof\r");
    kt_pump(&s, .4);
    proof(one, "latency-proof", "responsive");
    U("last good snapshot", snapshot_delay);
    CHECK(!unlink(slow), "remove delay trigger");
    U("\xe2\x97\x8f live", 6);
    S("(sleep 1; printf 'FORM_LIVE' > form-proof; printf 'FORM_LIVE\\n') &\r");
    kt_pump(&s, .2);
    S("\035/");
    U("Filter heads", 3);
    kt_pump(&s, 1.2);
    proof(one, "form-proof", "FORM_LIVE");
    CHECK(kt_contains(&s, "FORM_LIVE"), "form does not block output");
    S("\033[200~search\nq\033[201~");
    U("search q", 3); /* a pasted line break is a blank in a text field */
    CHECK(kt_contains(&s, "Filter heads"), "paste did not submit form");
    S("\033[H界\033[C\177");
    U("界earch q", 3);
    S("\033");
    kt_pump(&s, .2);
    CHECK(!kt_contains(&s, "Filter heads"), "form dismiss");
    S("\t"); /* leaving input focused the list; Tab goes on to the agent */
    U("typing to ", 3);
    S("sleep 15\r");
    kt_pump(&s, .2);
    S("\003");
    kt_pump(&s, .2);
    S("printf 'interrupted' > interruption-proof\r");
    kt_pump(&s, .4);
    proof(one, "interruption-proof", "interrupted");
    {
        int status;
        CHECK(waitpid(s.pid, &status, WNOHANG) == 0, "Ctrl-C targets attachment");
    }
    S("\033[200~printf 'paste' > paste-proof\033[201~\r");
    kt_pump(&s, .4);
    proof(one, "paste-proof", "paste");
    S("printf 'draft' > draft-proof");
    kt_pump(&s, .2);
    /* The page chords: a head without a plan or run has neither page; A
     * shows its Details beside the conversation. Stats and back. */
    S("\034B");
    U("No Plan page for this selection", 3);
    S("\034C");
    U("No Run page for this selection", 3);
    S("\034A");
    U("Session     running", 3);
    S("\034D");
    kt_pump(&s, .4);
    CHECK(!kt_contains(&s, "Heads \xe2\x94\x80"), "Ctrl-\\ D shows Stats");
    CHECK(!exists(one, "draft-proof"), "page and screen switching preserves unsent draft");
    S("D\t\t");
    U("typing to ", 3);
    S("\r");
    kt_pump(&s, .5);
    proof(one, "draft-proof", "draft");
    S("\035j");
    kt_pump(&s, .2);
    S("a");
    U("typing to ", 3);
    kt_pump(&s, .5);
    S("printf 'two' > attachment-proof\r");
    kt_pump(&s, .5);
    proof(two, "attachment-proof", "two");
    S("\034n");
    kt_pump(&s, .2);
    S("printf 'returned' > returned-proof\r");
    kt_pump(&s, .5);
    proof(one, "returned-proof", "returned");
    S("printf 'multi-one' > multi-proof");
    kt_pump(&s, .1);
    S("\034S");
    U("Two attached agents", 3);
    S("\034n");
    S("printf 'multi-two' > multi-proof");
    kt_pump(&s, .2);
    CHECK(hf_count(kt_text(&s), "attached \xc2\xb7") == 2, "two attached panes");
    S("\034A");
    kt_pump(&s, .3);
    CHECK(hf_count(kt_text(&s), "attached \xc2\xb7") == 2, "two panes beside the Details page");
    S("\034D");
    kt_pump(&s, .4);
    S("D");
    kt_pump(&s, .3);
    CHECK(hf_count(kt_text(&s), "attached \xc2\xb7") == 2, "two panes after Stats and back");
    for (i = 0; i < 3; i++) {
        kt_resize(&s, sizes[i][0], sizes[i][1]);
        kt_pump(&s, .3);
        CHECK(!s.screen.overflow, "two-agent resize");
        CHECK(!exists(one, "multi-proof") && !exists(two, "multi-proof"),
              "resize preserved drafts");
        save(&s, "two-agents", sizes[i][0], sizes[i][1]);
    }
    CHECK(hf_count(kt_text(&s), "attached \xc2\xb7") == 2, "two agents remain attached");
    S("\r");
    kt_pump(&s, .4);
    proof(two, "multi-proof", "multi-two");
    CHECK(!exists(one, "multi-proof"), "other agent draft unsent");
    S("\034n\r");
    kt_pump(&s, .4);
    proof(one, "multi-proof", "multi-one");
    S("i=0; while [ \"$i\" -lt 50 ]; do echo HISTORY_A:$i; i=$((i+1)); done\r");
    kt_pump(&s, .4);
    S("\034[kkkkk");
    kt_pump(&s, .2);
    history(kt_text(&s), hist_a, sizeof(hist_a));
    CHECK(*hist_a, "first agent history");
    S("\034n");
    S("i=0; while [ \"$i\" -lt 50 ]; do echo HISTORY_B:$i; i=$((i+1)); done\r");
    kt_pump(&s, .4);
    S("\034[kkkkk\034n");
    kt_pump(&s, .2);
    history(kt_text(&s), hist_after, sizeof(hist_after));
    CHECK(!strcmp(hist_a, hist_after), "independent scrollback");
    S("\034]\034n\034]\034n");
    kt_pump(&s, .2);
    {
        int x, y, hit_x = -1, hit_y = -1;
        for (y = 0; y < s.screen.rows; y++)
            for (x = 0; x < s.screen.cols; x++)
                if (pane_title_at(&s, y, x, branch[1])) {
                    hit_x = x - 1;
                    hit_y = y;
                    break;
                }
        CHECK(hit_x >= 0, "second agent pane title rendered");
        kt_format(script, sizeof(script), "\033[<0;%d;%dM\033[<0;%d;%dm", hit_x + 4, hit_y + 4,
                  hit_x + 4, hit_y + 4);
        S(script);
    }
    S("printf 'mouse-focus' > mouse-multi-proof\r");
    kt_pump(&s, .4);
    proof(two, "mouse-multi-proof", "mouse-focus");
    CHECK(!exists(one, "mouse-multi-proof"), "mouse focus exact agent");
    S("\034n\034S");
    kt_pump(&s, .2);
    S("printf 'reconnected' > reconnect-proof");
    kt_pump(&s, .1);
    S("\034x");
    kt_pump(&s, .2);
    CHECK(!exists(one, "reconnect-proof"), "detach retains unsent draft");
    S("a");
    U("typing to ", 3);
    kt_pump(&s, .4);
    S("\r");
    kt_pump(&s, .4);
    proof(one, "reconnect-proof", "reconnected");
    S("printf 'zoom-draft' > zoom-proof");
    kt_pump(&s, .1);
    S("\034z");
    U("z restores the panes", 3);
    CHECK(!kt_contains(&s, "Heads \xe2\x94\x80"), "zoom hides panes");
    S("\034D");
    kt_pump(&s, .4);
    S("D");
    U("z restores the panes", 3);
    S("\034z");
    U("Heads \xe2\x94\x80", 3);
    CHECK(!exists(one, "zoom-proof"), "zoom retains draft");
    S("\r");
    kt_pump(&s, .4);
    proof(one, "zoom-proof", "zoom-draft");
    save(&s, "attached", 140, 40);
    for (i = 0; i < 3; i++) {
        int term_rows = 0, term_cols = 0;
        char dimensions[256];
        kt_resize(&s, sizes[i][0], sizes[i][1]);
        kt_pump(&s, .3);
        CHECK(!s.screen.overflow, "attached resize bounds");
        S("stty size > size-proof\r");
        kt_pump(&s, .3);
        proof_path(path, sizeof(path), one, "size-proof");
        kt_read(path, dimensions, sizeof(dimensions));
        CHECK(sscanf(dimensions, "%d %d", &term_rows, &term_cols) == 2, "terminal dimensions");
        if (sizes[i][0] == 40) {
            CHECK(term_rows >= 6 && term_cols >= 35, "compact conversation usable");
            CHECK(!kt_contains(&s, "Heads \xe2\x94\x80") && kt_contains(&s, "typing to "),
                  "compact conversation view");
        }
        save(&s, "attached", sizes[i][0], sizes[i][1]);
    }
    kt_format(path, sizeof(path), "=%s", session);
    RUN("tmux", "detach-client", "-s", path);
    U("disconnected \xc2\xb7 a reconnects", 3);
    CHECK(!kt_contains(&s, "typing to "), "disconnected client input disabled");
    S("\034r");
    U("typing to ", 3);
    kt_pump(&s, .4);
    S("printf 'resumed' > resumed-proof\r");
    kt_pump(&s, .4);
    proof(one, "resumed-proof", "resumed");
    kt_close(&s, "\034q", 0, 0);
    CHECK(!strcmp(RUN("tmux", "list-panes", "-a", "-F", "#{pane_pid}"), pids),
          "client exit preserves shell PIDs");
    RUN("tmux", "list-clients", "-F", "#{client_pid}");
    hf_trim(f.output);
    CHECK(!*f.output, "no leaked attached clients");
    hf_finish(&f);
    puts("PASS attached: confidence labels, asynchronous input/output, paste/Unicode/Ctrl-C, "
         "drafts, two clients, history, mouse, reconnect, resize and client-only exit");
    return 0;
}
