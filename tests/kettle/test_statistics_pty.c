#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char root[4096], build[4096], tui[4096], fake[4096], evidence[4096];
#define U(marker) kt_until(&s, (marker), 3)
#define S(keys) kt_send(&s, (keys))
#define HAS(marker) kt_contains(&s, (marker))
#define BAR "\xe2\x96\x8c "
#define DOT " \xc2\xb7 "
#define CTRL_R "\x12"
static void save(struct kt_session *s, const char *name, int cols, int rows) {
    char path[4096];
    kt_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, cols, rows);
    kt_save(s, path);
}
/* The screen row holding one marker holds the other too. */
static bool row_has(struct kt_session *s, const char *marker, const char *also) {
    char row[2048];
    int y;
    for (y = 0; y < s->screen.rows; y++) {
        kt_row(s, y, row, sizeof(row));
        if (strstr(row, marker)) return strstr(row, also) != NULL;
    }
    return false;
}
/* The steps of one session share it by pointer; a marker that is not on
 * screen fails the wait. */
#define PU(marker) kt_until(p, (marker), 3)
#define PS(keys) kt_send(p, (keys))
#define PHAS(marker) kt_contains(p, (marker))

static void runs_overview(struct kt_session *p) {
    PU("Workflow runs");
    PU("Latest attempts average 95.0s over 6 timed steps, longest 120s");
    CHECK(PHAS("Attempts 8/11") && PHAS("Run outcomes") && PHAS("Runs created, last 7 days"), "attempt coverage and charts");
    CHECK(!PHAS("Sessions  Runs"), "a sample without sessions has no sessions page");
    save(p, "statistics", 140, 40);
    PS("T");
    PU("24 hours" DOT "all workflows");
    CHECK(PHAS("1 outside range"), "date exclusion");
    PU("1 of 4" DOT "newest first");
}

/* The filter narrows the list as it is typed and Enter keeps it; the
 * evidence opens beside the list and follows the selection while the list
 * has the keys; Tab gives them to the evidence, whose arrows scroll it. */
static void runs_filter_evidence(struct kt_session *p) {
    PS("0/");
    PU("enter keep filter");
    PS("recov");
    PU("/recov\xe2\x96\x8f" DOT "1 of 2");
    PS("ery\r");
    PU("all recorded" DOT "recovery");
    PU("/recovery" DOT "1 of 2");
    PS("\r");
    PU("Run evidence");
    PU("run_cccccccccccccccccccc");
    CHECK(PHAS("120") && PHAS("failed") && PHAS("Workflow runs"), "run evidence beside the list");
    PU("tab pane");
    PS("j");
    PU("2 of 2");
    PU("run_gggggggggggggggggggg");
    PS("\tj");
    kt_pump(p, .2);
    CHECK(PHAS("2 of 2") && PHAS("run_gggggggggggggggggggg"), "the focused evidence scrolls; the selection stays");
    PS("\tk");
    PU("1 of 2");
    /* Esc: the filter first, then the evidence. */
    PS("\033");
    PU("all recorded" DOT "all workflows");
    CHECK(PHAS("Run evidence"), "clearing the filter keeps the evidence open");
    PS("\033");
    kt_pump(p, .2);
    CHECK(!PHAS("Run evidence"), "the second Esc closes the evidence");
}

/* D leaves and comes back with Work as it was (the selection and the
 * focused pane); g opens the run's Run page in Work and Esc comes back; a
 * click selects a row. */
static void runs_jumps(struct kt_session *p) {
    PS("D");
    PU("Heads \xe2\x94\x80");
    CHECK(PHAS("feature-stale \xe2\x94\x80") && PHAS("esc back"), "Work keeps its selection and focus");
    PS("D");
    PU("Workflow runs");
    PS("0/visualization\r");
    PU("1 of 1" DOT "newest first");
    PS("g");
    PU("STEP");
    PU("prepare");
    PS("\033");
    PU("Workflow runs");
    PU("/visualization" DOT "1 of 1");
    PS("0");
    PU("1 of 8" DOT "newest first");
    PS("\033[<0;40;25M");
    kt_pump(p, .15);
    PU("3 of 8" DOT "newest first");
    PS("!");
    PU("all workflows" DOT "needs you only");
}

static void runs_stale(struct kt_session *p, const char *failure) {
    PS("0");
    kt_write(failure, "");
    PS(CTRL_R);
    PU("\xe2\x96\xb2 stale");
    PU("last statistics from 09-07 14:00 UTC");
    CHECK(PHAS("95.0s"), "last good statistics retained");
    CHECK(!unlink(failure), "remove failure trigger");
    PS(CTRL_R);
    kt_pump(p, .25);
    CHECK(!PHAS("\xe2\x96\xb2 stale"), "refresh recovers");
}

static void runs_sizes(struct kt_session *p) {
    int sizes[][2] = {{80, 24}, {40, 10}, {140, 40}};
    size_t i;
    for (i = 0; i < 3; i++) {
        kt_resize(p, sizes[i][0], sizes[i][1]);
        PU("Workflow runs");
        PU("q quit");
        CHECK(!p->screen.overflow, "statistics resize overflow");
        CHECK(p->screen.repaints >= 1 && !p->screen.clears, "resize repaints every cell without erasing the display");
        save(p, "statistics", sizes[i][0], sizes[i][1]);
    }
}

/* The runs page: notes, range, the filter typed as you go, evidence beside
 * the list, the graph jump, the mouse, a stale sample and every size. */
static void local_statistics(void) {
    struct kt_session s;
    char base[4096], failure[4096];
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
    kt_temp(base, sizeof(base), "hydra-statistics");
    kt_format(failure, sizeof(failure), "%s/fail", base);
    CHECK(!setenv("HYDRA_TEST_STATS_FAIL_FILE", failure, 1), "stats failure environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("Heads \xe2\x94\x80");
    /* The second head, its side pane focused and scrolled (Shift-Tab). */
    S("j\033[Zjj");
    kt_pump(&s, .2);
    CHECK(HAS("feature-stale \xe2\x94\x80") && HAS("esc back"), "Work selection and focus setup");
    S("D");
    runs_overview(&s);
    runs_filter_evidence(&s);
    runs_jumps(&s);
    runs_stale(&s, failure);
    runs_sizes(&s);
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FAIL_FILE"), "reset failure environment");
    CHECK(!rmdir(base), "remove owned stats fixture");
    puts("PASS statistics: notes/range, filter as you type, evidence beside the list, pane focus, Esc ladder, "
         "graph, preserved Work selection, mouse, stale recovery, sizes, termios");
}
/* w cycles the workflow filter over the recorded workflows and back to all;
 * filters reach the tiles and charts as well as the list. */
static void workflow_filter(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("1 of 8" DOT "newest first");
    U("w workflow");
    S("w");
    U("all workflows" DOT "workflow visualization-proof");
    U("1 of 1" DOT "newest first");
    S("w");
    U("workflow release-check");
    U("1 of 4" DOT "newest first");
    CHECK(row_has(&s, "Succeeded", "3") && row_has(&s, "Cancelled", "1"), "the outcomes chart follows the workflow");
    S("ww");
    U("workflow imported-work");
    S("w");
    U("all recorded" DOT "all workflows");
    /* The selection stays on the run it was on. */
    U("8 of 8");
    /* Typing filters; Backspace widens again, Esc clears, a paste fills it. */
    S("/qqq");
    U("Nothing matches the filters" DOT "0 resets them");
    U("8 recorded; the filters hide them all.");
    S("\x7f\x7f\x7f");
    U("8 of 8");
    S("\033[200~release\033[201~");
    U("/release\xe2\x96\x8f" DOT "4 of 4");
    S("\033");
    U("all recorded" DOT "all workflows");
    CHECK(!HAS("keep filter"), "Esc ends typing");
    kt_close(&s, "q", 0, 0);
    puts("PASS workflow filter: w cycles workflows, filter typed and pasted, empty versus filtered out");
}
static void metric_pages(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    int sizes[][2] = {{140, 40}, {80, 24}, {40, 10}};
    const char *titles[] = {"Queue delay", "Total execution", "Recorded verification", "Owner recoveries"};
    const char *coverage[] = {"known 7/10", "known 4/5", "known 2/3", "known 6/8"};
    const char *names[] = {"queue-delay", "total-execution", "recorded-verification",
                           "owner-recoveries"};
    size_t i, j;
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("Latest attempts average");
    for (i = 0; i < 3; i++) {
        if (s.screen.cols != sizes[i][0] || s.screen.rows != sizes[i][1])
            kt_resize(&s, sizes[i][0], sizes[i][1]);
        for (j = 0; j < 4; j++) {
            S("M");
            U(titles[j]);
            U(coverage[j]);
            U("Sample age");
            U("q quit");
            CHECK(HAS("p50") && !s.screen.overflow, "metric percentiles and bounds");
            save(&s, names[j], sizes[i][0], sizes[i][1]);
        }
        S("M");
        U("Workflow runs");
    }
    kt_resize(&s, 140, 40);
    S("MMMM/recovery\r");
    U("known 2/2");
    S("\r");
    U("failed / 2");
    S("D");
    U("Heads \xe2\x94\x80");
    S("D");
    U("Owner recoveries");
    U("known 2/2");
    kt_close(&s, "q", 0, 0);
    puts("PASS metrics: four pages, coverage, percentiles, freshness, filters, evidence, three "
         "sizes");
}
/* Hosts says the same of each host: the failed one has unknown heads (--),
 * the empty one that responded has 0. */
static void hosts_heads(struct kt_session *p) {
    PS("H");
    PU("HOSTS / latest list response");
    PU("3 hosts / Enter follows its work");
    CHECK(row_has(p, "offline", "disconnected") && row_has(p, "offline", "-- heads"), "a failed host has unknown heads");
    CHECK(row_has(p, "empty   ", " 0 heads"), "empty distinct from unknown");
}
static void fleet_statistics(void) {
    struct kt_session s;
    const char *argv[] = {tui,       "--hydra", fake,         "--theme", "dark",
                          "--fleet", "--view",  "statistics", NULL};
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("latest fleet response" DOT "all hosts");
    U("2 responded   1 failed   1 reported heads");
    CHECK(row_has(&s, "offline", "failed") && row_has(&s, "offline", "--"), "unknown failed-host heads");
    S("\r");
    U("Host evidence");
    U("/work/project");
    S("\033");
    U(BAR "builder");
    S("/offline\r");
    U("0 responded   1 failed   -- reported heads");
    CHECK(HAS("Failed hosts have unknown head counts"), "unknown-host explanation");
    save(&s, "statistics-fleet", 140, 40);
    S("0");
    kt_resize(&s, 80, 14);
    S("k");
    kt_pump(&s, .1);
    CHECK(HAS(BAR "empty"), "selection scrolls into view");
    S("j");
    U(BAR "offline");
    kt_pump(&s, .1);
    kt_resize(&s, 40, 10);
    U("3 of 3");
    U("q quit");
    CHECK(!s.screen.overflow, "small fleet statistics overflow");
    S("\r");
    U("No head evidence");
    kt_resize(&s, 140, 40);
    hosts_heads(&s);
    kt_close(&s, "q", 0, 0);
    puts("PASS fleet statistics: responded/failed/empty, known counts, filters, drill-down");
}
/* Head sessions: the page a sample without runs opens on, its filters and
 * evidence, and [ ] between sessions and workflow runs. */

static void session_overview(struct kt_session *p) {
    PU("Head sessions");
    PU("Observed 38% of 6h00m session time; 10m00s reported by adapters, the rest inferred from terminal output");
    PU(BAR "fix-login-timeout");
    PU("1 of 5" DOT "newest first");
    PU("inferred from output");
    PU("3h42m");
    PU("Length 4/5");
    PU("Observed 2/5");
    PU("1 undated");
    PU("Sessions started, last 7 days");
    PU("By agent");
    save(p, "statistics-sessions", 140, 40);
}

static void session_filters(struct kt_session *p) {
    PS("T");
    PU("24 hours" DOT "all heads");
    PU("1 of 3");
    PU("1 outside range");
    /* Filtered out is told apart from nothing recorded, while typing. */
    PS("0/absent");
    PU("Nothing matches the filters" DOT "0 resets them");
    PU("5 recorded; the filters hide them all.");
    CHECK(!PHAS("No sessions recorded yet"), "filtered out is not empty");
    PS("\033");
    PU("1 of 5");
    PS("!");
    PU("all heads" DOT "needs you only");
    PU(BAR "nightly-import");
    PS("\r");
    PU("A headless head has no terminal to observe");
    PS("\033");
    kt_pump(p, .2);
    CHECK(!PHAS("Session evidence"), "Esc closes the evidence");
    PS("0/docs\r");
    PU("all recorded" DOT "docs");
    PU("2 of 2");
}

static void session_evidence(struct kt_session *p) {
    PS("\r");
    PU("Session evidence");
    PU("docs-refresh / replaced");
    PU("instance_bbbbbbbbbbbbbbb1");
    PU("Active     --   Quiet --");
    PU("Unobserved 2h00m   Observed 0%");
    PU("Nobody observed this session");
    /* The evidence beside the list follows the selection. */
    PS("k");
    PU("1 of 2");
    PU("Reported by an adapter (exact): 10m00s");
    PU("Active     40m00s   Quiet 50m00s");
    PU("Unobserved 30m00s   Observed 75%");
    PU("(tmux)");
    PS("\033\033");
    /* The selection stays on the session the evidence showed. */
    PU("2 of 5");
    kt_pump(p, .2);
    CHECK(!PHAS("Session evidence"), "Esc clears the filter, then closes the evidence");
}

static void session_pages(struct kt_session *p) {
    PS("0");
    CHECK(!PHAS("M metric") && !PHAS("w workflow") && PHAS("g open head"), "runs-only keys are not offered on sessions");
    PS("]");
    PU("No workflow runs recorded yet");
    PU("[/] sessions/runs");
    PS("[");
    PU("Head sessions");
    kt_resize(p, 80, 24);
    PU("5 sessions" DOT "1 open" DOT "active 1h05m" DOT "quiet 1h13m" DOT "inferred");
    CHECK(!p->screen.overflow, "medium session statistics overflow");
    save(p, "statistics-sessions", 80, 24);
    kt_resize(p, 40, 10);
    PU("Head sessions");
    PU("q quit");
    CHECK(!p->screen.overflow, "small session statistics overflow");
}

static void session_statistics(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v6-sessions.tsv", 1), "session fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("Heads \xe2\x94\x80");
    S("D");
    session_overview(&s);
    session_filters(&s);
    session_evidence(&s);
    session_pages(&s);
    kt_close(&s, "q", 0, 0);
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v6.tsv", 1), "mixed fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("Heads \xe2\x94\x80");
    S("D");
    U("Workflow runs");
    U("[/] sessions/runs");
    U(BAR "kill-dry-run");
    S("]");
    U("1 of 5" DOT "newest first");
    S("\033[<0;13;3M");
    U(BAR "kill-dry-run");
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FIXTURE"), "session fixture environment cleared");
    puts("PASS session statistics: default page, coverage, filters, evidence, unknowns, pages, strip click, sizes");
}
/* g on a session shows its head in Work (its Details page) and Esc comes
 * back to the same place in Stats; a head that is gone is said so and
 * nothing moves. */
static void session_jump(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v6-heads.tsv", 1), "head sessions fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("Heads \xe2\x94\x80");
    S("D");
    U(BAR "feature-live");
    S("\r");
    U("Session evidence");
    S("g");
    U("feature-live \xe2\x94\x80");
    U("Session     running");
    S("\033");
    U("Head sessions");
    U("Session evidence");
    CHECK(HAS(BAR "feature-live"), "the selection is kept across the jump");
    S("jg");
    U("retired-branch is not a head in this project any more");
    CHECK(HAS("Head sessions"), "a gone head leaves Stats as it was");
    S("\033\033");
    U("Heads \xe2\x94\x80");
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FIXTURE"), "head sessions fixture cleared");
    puts("PASS session jump: g opens the head in Work's Details page, Esc returns, a gone head is named");
}
/* Mixed local and remote runs (schema 5): the runs page says what remote
 * task receipts measured; the Cohorts page (C, or [ ]) and the location
 * filter (L, a chip on the strip) say what the receipts measured and what
 * stays unknown with its reason; nothing unmeasured is shown as zero. */
static void cohorts_runs_page(struct kt_session *p) {
    PU("Latest attempts average");
    PU("Remote CPU 5.5s");
    CHECK(PHAS("(2/4), peak RSS 50.0M") && PHAS("unmeasured."), "the runs page states remote telemetry coverage");
    CHECK(PHAS(" Runs  Cohorts ") && !PHAS("Sessions  Runs"), "the strip offers Cohorts, and no sessions page for schema 5");
    CHECK(!PHAS("location: "), "no location chip while every location is shown");
}
static void cohorts_rows(struct kt_session *p) {
    PS("C");
    PU("Remote cohorts ");
    PU("3 local steps, 4 remote steps on 2 hosts; telemetry block on 2/4 receipts");
    CHECK(PHAS("227s (2/4)") && PHAS("5.5s (2/4)") && PHAS("peak RSS 50.0M (1/4)") && PHAS("50.4k (3/4)"), "cohort tiles keep coverage");
    save(p, "cohorts", 140, 40);
}
static void cohorts_kinds(struct kt_session *p) {
    CHECK(PHAS("location   local") && PHAS("location   remote") && PHAS("host       alpha") && PHAS("host       beta"), "location and host cohorts");
    CHECK(PHAS("placement  auto") && PHAS("placement  pinned") && PHAS("generation generation 2") && PHAS("generation generation unknown"), "placement and generation cohorts");
    CHECK(PHAS("provider   claude") && PHAS("provider   codex") && PHAS("provider   no agent receipt"), "provider cohorts");
}
static void cohorts_unknowns(struct kt_session *p) {
    CHECK(PHAS("2 no telemetry block") && PHAS("3 no task receipt") && PHAS("1 receipt unverified") && PHAS("1 peak_rss_bytes=rss_not_distinguishable"), "unmeasured reasons");
    CHECK(PHAS("unknown     unknown     unknown"), "unmeasured host shows unknown, not zero");
    CHECK(!PHAS(" 0s "), "no zero stands in for an unmeasured wall time");
}
static void cohorts_location(struct kt_session *p) {
    PS("L");
    PU(" location: local ");
    PU("3 local steps, 0 remote steps on 0 hosts");
    CHECK(PHAS("Remote cohorts" DOT "all recorded" DOT "all workflows" DOT "location: local"), "the status line names the location");
    PS("L");
    PU(" location: remote ");
    PU("0 local steps, 4 remote steps on 2 hosts");
    PS("L");
    PU(" location: alpha ");
    PU("0 local steps, 3 remote steps on 2 hosts");
    PS("jj");
    PU(BAR "host       beta");
    PS("0");
    PU("3 local steps, 4 remote steps on 2 hosts");
    CHECK(!PHAS("location: "), "0 resets the location and drops the chip");
}
static void cohorts_sizes(struct kt_session *p) {
    int sizes[][2] = {{140, 40}, {80, 24}, {40, 10}};
    size_t i;
    for (i = 0; i < 3; i++) {
        if (p->screen.cols != sizes[i][0] || p->screen.rows != sizes[i][1]) kt_resize(p, sizes[i][0], sizes[i][1]);
        PU("Remote cohorts");
        PU("q quit");
        CHECK(!p->screen.overflow, "cohorts resize overflow");
        save(p, "cohorts", sizes[i][0], sizes[i][1]);
    }
    CHECK(PHAS("4 remote") && PHAS("wall 227s (2/4)"), "compact cohorts keep coverage");
    kt_resize(p, 140, 40);
}
/* C goes back to the runs, ] goes round the pages, and the location filter
 * narrows the runs as well. */
static void cohorts_back(struct kt_session *p) {
    PS("C");
    PU("Workflow runs ");
    PS("]");
    PU("Remote cohorts ");
    PS("]");
    PU("Workflow runs ");
    PS("L");
    PU(" location: local ");
    PU("1 of 1");
    CHECK(PHAS("Workflow runs" DOT "all recorded" DOT "all workflows" DOT "location: local"), "the runs page names the location");
    PS("LL");
    PU(" location: alpha ");
    PU("1 of 1");
}
static void remote_cohorts(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v5.tsv", 1), "schema 5 fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    cohorts_runs_page(&s);
    cohorts_rows(&s);
    cohorts_kinds(&s);
    cohorts_unknowns(&s);
    cohorts_location(&s);
    cohorts_sizes(&s);
    cohorts_back(&s);
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FIXTURE"), "reset fixture environment");
    puts("PASS remote cohorts: Cohorts page, location/host/placement/generation/provider, unknown reasons, L chip, three sizes");
}
int main(void) {
    kt_init();
    unsetenv("NO_COLOR");
    kt_paths(root, sizeof(root), build, sizeof(build));
    kt_format(tui, sizeof(tui), "%s/hydra-tui", build);
    kt_format(fake, sizeof(fake), "%s/tests/fixtures/tui/fake-hydra.sh", root);
    kt_format(evidence, sizeof(evidence), "%s/build/statistics-evidence", root);
    kt_mkdir(evidence);
    local_statistics();
    workflow_filter();
    fleet_statistics();
    metric_pages();
    session_statistics();
    session_jump();
    remote_cohorts();
    return 0;
}
