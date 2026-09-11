#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
static char evidence[4096];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) tv_send(&s, (keys))
#define U(marker, seconds) tv_until(&s, (marker), (seconds))
static void save(struct tv_session *s, const char *name, int width, int height) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, width, height);
    tv_save(s, path);
}
static bool state_is(const char *run_dir, const char *expected) {
    char path[4096], text[256];
    tv_format(path, sizeof(path), "%s/state", run_dir);
    tv_read(path, text, sizeof(text));
    hf_trim(text);
    return !strcmp(text, expected);
}
static size_t after_count(const char *effects) {
    char text[4096];
    tv_read(effects, text, sizeof(text));
    return hf_count(text, "after\n");
}
static int run_index(const char *rows, const char *run_id) {
    int i = 0;
    const char *p = rows;
    while (*p) {
        const char *end = strchr(p, '\n');
        if (!strncmp(p, "W\t", 2)) {
            const char *field = p + 2, *tab = strchr(field, '\t');
            CHECK(tab, "workflow row");
            if ((size_t)(tab - field) == strlen(run_id) && !strncmp(field, run_id, strlen(run_id)))
                return i;
            i++;
        }
        if (!end)
            break;
        p = end + 1;
    }
    CHECK(false, "selected run present");
    return -1;
}
static bool lines_subset(const char *before, const char *after) {
    const char *p = before;
    while (*p) {
        const char *end = strchr(p, '\n');
        char line[256];
        size_t n = end ? (size_t)(end - p) : strlen(p);
        CHECK(n + 2 < sizeof(line), "PID line");
        memcpy(line, p, n);
        line[n] = '\n';
        line[n + 1] = 0;
        {
            const char *match = after;
            do {
                match = strstr(match, line);
                if (!match)
                    return false;
                if (match == after || match[-1] == '\n')
                    break;
                match++;
            } while (true);
        }
        if (!end)
            break;
        p = end + 1;
    }
    return true;
}
int main(void) {
    struct tv_session s;
    char path[4096], worker[4096], pids[8192], effect[4096], effects[4096], flow[4096], yaml[8192],
        run_id[256], run_dir[4096], request[256], decision[4096], pattern[4096], text[8192];
    const char *actions[] = {"approve", "reject", "cancel"},
               *terminals[] = {"succeeded", "failed", "cancelled"};
    int sizes[][2] = {{40, 10}, {80, 24}, {140, 40}};
    size_t i, j;
    tv_init();
    hf_init(&f, "hydra-controls", "repo", false, true);
    hf_file(&f, "tracked", path, sizeof(path));
    tv_write(path, "base\n");
    hf_commit_init(&f);
    H("spawn", "operator-worker", "--no-agent");
    tv_format(worker, sizeof(worker), "%s", H("path", "operator-worker"));
    hf_trim(worker);
    tv_format(pids, sizeof(pids), "%s", RUN("tmux", "list-panes", "-a", "-F", "#{pane_pid}"));
    tv_format(evidence, sizeof(evidence), "%s/build/workspace-control-evidence", f.root);
    tv_mkdir(evidence);
    tv_format(effects, sizeof(effects), "%s/effects", f.base);
    tv_format(effect, sizeof(effect), "%s/effect.sh", f.base);
    tv_write(effect,
             "#!/bin/sh\n[ \"$2\" != after ] || sleep 10\nprintf \"%s\\n\" \"$2\" >> \"$1\"\n");
    tv_format(flow, sizeof(flow), "%s/flow.yml", f.base);
    tv_format(yaml, sizeof(yaml),
              "version: 1\nid: workspace-control\nresources:\n  disk_mb: 1\nsteps:\n  - id: "
              "before\n    kind: exec\n    idempotent: false\n    args:\n      head: "
              "operator-worker\n      argv: [sh, %s, %s, before]\n  - id: input\n    kind: "
              "approval-wait\n    idempotent: false\n    needs: [before]\n    args:\n      head: "
              "operator-worker\n      name: review\n      message: Review operator evidence before "
              "continuing\n  - id: after\n    kind: exec\n    idempotent: false\n    needs: "
              "[input]\n    args:\n      head: operator-worker\n      argv: [sh, %s, %s, after]\n",
              effect, effects, effect, effects);
    tv_write(flow, yaml);
    for (i = 0; i < 3; i++) {
        const char *action = actions[i], *terminal = terminals[i];
        double deadline;
        hf_run(&f, NULL, 3, (const char *[]){f.hydra, "workflow", "run", flow, NULL});
        {
            char *nl = strchr(f.output, '\n');
            CHECK(nl && (size_t)(nl - f.output) < sizeof(run_id), "workflow run ID");
            memcpy(run_id, f.output, (size_t)(nl - f.output));
            run_id[nl - f.output] = 0;
        }
        tv_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/runs/%s", f.home,
                  run_id);
        hf_glob_one(pattern, run_dir, sizeof(run_dir));
        tv_format(path, sizeof(path), "%s/steps/input/request-id", run_dir);
        tv_read(path, request, sizeof(request));
        hf_trim(request);
        tv_format(decision, sizeof(decision), "%s/approvals/%s/decision/action", run_dir, request);
        hf_open(&f, &s);
        U("A CONVERSATION", 3);
        S("Ca");
        U("INPUT TO AGENT", 3);
        tv_pump(&s, .3);
        if (i == 0)
            S("printf unsubmitted > unsent-control-proof");
        S("\002\t\tz");
        U("OBSERVED EVIDENCE", 15);
        tv_repeat(&s, "]", run_index(H("workflow", "tui-data"), run_id));
        U(run_id, 15);
        tv_repeat(&s, "j", 200);
        U("Review operator evidence before continuing", 15);
        tv_format(text, sizeof(text), "waiting-%s", action);
        save(&s, text, 140, 40);
        if (i == 0) {
            for (j = 0; j < 3; j++) {
                tv_resize(&s, sizes[j][0], sizes[j][1]);
                tv_pump(&s, .3);
                CHECK(!s.screen.overflow, "waiting approval resize");
                CHECK(tv_contains(&s, "waiting-approval"), "waiting state visible");
                save(&s, "waiting", sizes[j][0], sizes[j][1]);
            }
            S("Y");
            U("Request ID from the evidence pane", 3);
            S(request);
            S("\r");
            U("Type approve to confirm", 3);
            S("wrong\r");
            U("Control not submitted", 3);
            CHECK(!tv_exists(decision), "wrong confirmation has no decision");
        }
        if (i < 2) {
            S(i == 0 ? "Y" : "N");
            U("Request ID from the evidence pane", 3);
            S(request);
            S("\r");
            tv_format(text, sizeof(text), "Type %s to confirm", action);
            U(text, 3);
            S(action);
            S("\r");
            U("Control completed", 15);
            tv_format(text, sizeof(text), "%s\n", action);
            CHECK(tv_file_equals(decision, text), "exact decision durable");
            CHECK(state_is(run_dir, "waiting-approval"), "decision separate from resume");
            CHECK(after_count(effects) == (i == 0 ? 0U : 1U), "decision has no implicit execution");
            S("R");
            U("Type resume to confirm", 3);
            S("resume\r");
        } else {
            S("X");
            U("Type cancel to confirm", 3);
            S("cancel\r");
        }
        if (i == 0) {
            U("OBSERVED EVIDENCE / run running", 10);
            for (j = 0; j < 3; j++) {
                tv_resize(&s, sizes[j][0], sizes[j][1]);
                tv_pump(&s, .3);
                CHECK(!s.screen.overflow, "running resize");
                save(&s, "running", sizes[j][0], sizes[j][1]);
            }
            CHECK(state_is(run_dir, "running"), "close UI while run active");
            tv_close(&s, "q", 0, 0);
        }
        deadline = tv_now() + 30;
        while (tv_now() < deadline && !state_is(run_dir, terminal)) {
            if (s.closed)
                tv_sleep(.1);
            else
                tv_pump(&s, .1);
        }
        CHECK(state_is(run_dir, terminal), "workflow terminal outcome");
        if (!s.closed) {
            U(i == 1 ? "Control failed" : "Control completed", 15);
            tv_repeat(&s, "k", 200);
            tv_format(text, sizeof(text), "Workflow %s: %s", run_id, terminal);
            U(text, 15);
            for (j = 0; j < 3; j++) {
                tv_resize(&s, sizes[j][0], sizes[j][1]);
                tv_pump(&s, .3);
                CHECK(!s.screen.overflow, "terminal workflow resize");
                save(&s, terminal, sizes[j][0], sizes[j][1]);
            }
            S("R");
            U("Terminal run: no resume or cancel", 10);
            S("]");
            tv_pump(&s, .3);
            CHECK(!tv_contains(&s, "Terminal run: no resume or cancel"),
                  "changing selection clears footer");
            tv_close(&s, "q", 0, 0);
        }
    }
    CHECK(after_count(effects) == 1, "only approved resumed workflow executes after step");
    tv_format(path, sizeof(path), "%s/unsent-control-proof", worker);
    CHECK(!tv_exists(path), "control form preserved attached draft");
    CHECK(lines_subset(pids, RUN("tmux", "list-panes", "-a", "-F", "#{pane_pid}")),
          "original panes survive controls");
    tv_format(text, sizeof(text), "%s", RUN("tmux", "list-panes", "-a", "-F", "#{pane_id}"));
    {
        char *p = text;
        bool found = false;
        while (*p) {
            char *end = strchr(p, '\n');
            if (end)
                *end = 0;
            if (strstr(RUN("tmux", "capture-pane", "-p", "-t", p), "unsent-control-proof"))
                found = true;
            if (!end)
                break;
            p = end + 1;
        }
        CHECK(found, "unsubmitted draft remains in pane");
    }
    hf_run(
        &f, NULL, -2,
        (const char *[]){f.hydra, "workflow", "--workspace-control", "../", "cancel", "-", NULL});
    hf_cleanup();
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    puts("PASS workspace controls: exact decisions, separate resume, detached continuation, "
         "rejection, cancellation, draft and panes preserved");
    return 0;
}
