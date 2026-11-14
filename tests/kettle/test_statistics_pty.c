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
static void save(struct kt_session *s, const char *name, int cols, int rows) {
    char path[4096];
    kt_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, cols, rows);
    kt_save(s, path);
}
static void local_statistics(void) {
    struct kt_session s;
    char base[4096], failure[4096];
    int sizes[][2] = {{80, 24}, {40, 10}, {140, 40}};
    size_t i;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
    kt_temp(base, sizeof(base), "hydra-statistics");
    kt_format(failure, sizeof(failure), "%s/fail", base);
    CHECK(!setenv("HYDRA_TEST_STATS_FAIL_FILE", failure, 1), "stats failure environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("PLAN TOGETHER");
    /* Tab order: selected work, dependencies, then activity. */
    S("j\tjj\t\tjjj");
    kt_pump(&s, .2);
    CHECK(HAS("scroll 2") && HAS("scroll 3"), "workspace scroll setup");
    S("D");
    U("STATISTICS");
    U("Latest attempts average 95.0s over 6 timed steps, longest 120s");
    CHECK(HAS("Attempts 8/11"), "attempt coverage");
    save(&s, "statistics", 140, 40);
    S("T");
    U("Recorded workflow runs · 24 hours");
    CHECK(HAS("1 outside range"), "date exclusion");
    U("1/4 / Enter evidence");
    S("0/");
    U("Find workflow, run or project");
    S("recovery\r");
    U("Recorded workflow runs · all recorded · recovery");
    U("1/2 / Enter evidence");
    S("\r");
    U("RUN EVIDENCE");
    U("run_cccccccccccccccccccc");
    CHECK(HAS("120") && HAS("failed"), "run evidence values");
    S("\033");
    U("RUNS / newest");
    S("D");
    U("PLAN TOGETHER");
    CHECK(HAS("scroll 2") && HAS("scroll 3"), "preserved workspace scroll");
    S("D");
    U("Recorded workflow runs · all recorded · recovery");
    S("0/");
    U("Find workflow, run or project");
    S("visualization\r");
    U("1/1 / Enter evidence");
    S("g");
    U("WORKFLOWS");
    U("prepare");
    S("\033");
    U("STATISTICS");
    U("Recorded workflow runs · all recorded · visualization");
    S("0");
    U("1/8 / Enter evidence");
    S("\033[<0;40;23M");
    kt_pump(&s, .15);
    U("3/8 / Enter evidence");
    S("!");
    U("all work / attention");
    S("0");
    kt_write(failure, "");
    S("r");
    U("STATISTICS (STALE)");
    CHECK(HAS("95.0s"), "last good statistics retained");
    CHECK(!unlink(failure), "remove failure trigger");
    S("r");
    kt_pump(&s, .25);
    CHECK(!HAS("STATISTICS (STALE)"), "refresh recovers");
    for (i = 0; i < 3; i++) {
        kt_resize(&s, sizes[i][0], sizes[i][1]);
        U("STATISTICS");
        U("q quit");
        CHECK(!s.screen.overflow, "statistics resize overflow");
        CHECK(s.screen.repaints >= 1 && !s.screen.clears, "resize repaints every cell without erasing the display");
        save(&s, "statistics", sizes[i][0], sizes[i][1]);
    }
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FAIL_FILE"), "reset failure environment");
    CHECK(!rmdir(base), "remove owned stats fixture");
    puts("PASS statistics: scope/time, evidence and graph, preserved scroll, mouse, stale "
         "recovery, sizes, termios");
}
static void metric_pages(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    int sizes[][2] = {{140, 40}, {80, 24}, {40, 10}};
    const char *titles[] = {"QUEUE DELAY", "TOTAL EXECUTION", "RECORDED VERIFICATION",
                            "OWNER RECOVERIES"};
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
        U("STATISTICS");
    }
    kt_resize(&s, 140, 40);
    S("MMMM/");
    U("Find workflow, run or project");
    S("recovery\r");
    U("known 2/2");
    S("\r");
    U("failed / 2");
    S("D");
    U("PLAN TOGETHER");
    S("D");
    U("OWNER RECOVERIES");
    U("known 2/2");
    kt_close(&s, "q", 0, 0);
    puts("PASS metrics: four pages, coverage, percentiles, freshness, filters, evidence, three "
         "sizes");
}
static void fleet_statistics(void) {
    struct kt_session s;
    const char *argv[] = {tui,       "--hydra", fake,         "--theme", "dark",
                          "--fleet", "--view",  "statistics", NULL};
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("HOST STATISTICS");
    U("2 responded   1 failed   1 reported heads");
    CHECK(HAS("offline") && HAS("-- heads"), "unknown failed-host heads");
    S("\r");
    U("/work/project");
    S("\033");
    U("> builder");
    S("/");
    U("Find host");
    S("offline\r");
    U("0 responded   1 failed   -- reported heads");
    CHECK(HAS("Failed hosts have unknown head counts"), "unknown-host explanation");
    save(&s, "statistics-fleet", 140, 40);
    S("0");
    kt_resize(&s, 80, 14);
    S("k");
    kt_pump(&s, .1);
    CHECK(HAS("> empty"), "selection scrolls into view");
    S("j");
    U("> offline");
    kt_pump(&s, .1);
    kt_resize(&s, 40, 10);
    U("3/3 offline");
    U("q quit");
    CHECK(!s.screen.overflow, "small fleet statistics overflow");
    S("\r");
    U("No head evidence");
    kt_resize(&s, 140, 40);
    S("W");
    U("Hosts: 3 observed");
    S("\t\tz");
    U("offline · failed · unknown heads");
    CHECK(HAS("empty · responded · 0 heads"), "empty distinct from unknown");
    S("H");
    U("HOSTS / latest list response");
    kt_close(&s, "q", 0, 0);
    puts("PASS fleet statistics: responded/failed/empty, known counts, filters, drill-down");
}
/* Head sessions: the page a sample without runs opens on, its filters and
 * drill-down, and s between sessions and workflow runs. The steps share one
 * session, passed by pointer; a marker that is not on screen fails the wait. */
#define PU(marker) kt_until(p, (marker), 3)
#define PS(keys) kt_send(p, (keys))
#define PHAS(marker) kt_contains(p, (marker))

static void session_overview(struct kt_session *p) {
    PU("HEAD SESSIONS / newest start first");
    PU("Observed 38% of 6h00m session time; 10m00s reported by adapters, the rest inferred from terminal output");
    PU("> fix-login-timeout");
    PU("1/5 / Enter evidence");
    PU("inferred from output");
    PU("Unobserved      3h42m");
    PU("Length 4/5");
    PU("Observed 2/5");
    PU("1 undated");
    save(p, "statistics-sessions", 140, 40);
}

static void session_filters(struct kt_session *p) {
    PS("T");
    PU("Head sessions \302\267 24 hours");
    PU("1/3 / Enter evidence");
    PU("1 outside range");
    PS("0/");
    PU("Find branch, agent or head");
    PS("absent\r");
    PU("No sessions match: 5 recorded, all filtered out");
    PS("0!");
    PU("Head sessions \302\267 all recorded \302\267 all heads / attention only");
    PU("> nightly-import");
    PS("\r");
    PU("A headless head has no terminal to observe");
    PS("\033");
    PU("HEAD SESSIONS / newest start first");
    PS("0/");
    PU("Find branch, agent or head");
    PS("docs\r");
    PU("Head sessions \302\267 all recorded \302\267 docs");
    PU("2/2 / Enter evidence");
}

static void session_evidence(struct kt_session *p) {
    PS("\r");
    PU("SESSION EVIDENCE");
    PU("docs-refresh / replaced");
    PU("instance_bbbbbbbbbbbbbbb1");
    PU("Active --   Quiet --   Unobserved 2h00m   Observed 0%");
    PU("Nobody observed this session");
    PS("\033");
    PU("HEAD SESSIONS / newest start first");
    PS("k\r");
    PU("Reported by an adapter (exact): 10m00s");
    PU("Active 40m00s   Quiet 50m00s   Unobserved 30m00s   Observed 75%");
    PU("inferred from terminal output (tmux)");
    PS("\033");
    PU("HEAD SESSIONS / newest start first");
}

static void session_pages(struct kt_session *p) {
    PS("0M");
    PU("That key is for workflow runs; s shows them");
    PS("s");
    PU("RUNS / newest creation first");
    PU("No matching recorded runs");
    PU("s sessions/runs");
    PS("s");
    PU("HEAD SESSIONS / newest start first");
    kt_resize(p, 80, 24);
    PU("Observed 38% of 6h00m; 10m00s reported, the rest inferred from output");
    CHECK(!p->screen.overflow, "medium session statistics overflow");
    save(p, "statistics-sessions", 80, 24);
    kt_resize(p, 40, 10);
    PU("5 sessions / 1 open");
    PU("Active 1h05m / quiet 1h13m");
    CHECK(!p->screen.overflow, "small session statistics overflow");
}

static void session_statistics(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v6-sessions.tsv", 1), "session fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("PLAN TOGETHER");
    S("D");
    session_overview(&s);
    session_filters(&s);
    session_evidence(&s);
    session_pages(&s);
    kt_close(&s, "q", 0, 0);
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v6.tsv", 1), "mixed fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("PLAN TOGETHER");
    S("D");
    U("RUNS / newest creation first");
    U("s sessions/runs");
    U("> kill-dry-run");
    S("s");
    U("s shows runs (1)");
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FIXTURE"), "session fixture environment cleared");
    puts("PASS session statistics: default page, coverage, filters, drill-down, unknowns, sizes");
}
/* Mixed local and remote runs (schema 5): the cohorts page, the location
 * filter and the side panel say what the receipts measured and what stays
 * unknown with its reason; nothing unmeasured is shown as zero. */
static void remote_cohorts(void) {
    struct kt_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    int sizes[][2] = {{140, 40}, {80, 24}, {40, 10}};
    size_t i;
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v5.tsv", 1), "schema 5 fixture environment");
    kt_open_terminal(&s, argv, 140, 40, NULL);
    U("Latest attempts average");
    U("Remote CPU 5.5s");
    CHECK(HAS("(2/4), peak RSS 50.0M") && HAS("unmeasured."), "side panel wraps remote telemetry coverage");
    CHECK(HAS("Location: all"), "location scope line");
    S("C");
    U("REMOTE COHORTS / recorded task evidence / location: local and remote");
    U("3 local steps, 4 remote steps on 2 hosts; telemetry block on 2/4 receipts");
    U("Wall 227s (2/4) / CPU 5.5s (2/4) / peak RSS 50.0M (1/4) / transport 50.4k (3/4)");
    save(&s, "cohorts", 140, 40);
    CHECK(HAS("location   local") && HAS("location   remote") && HAS("host       alpha") && HAS("host       beta"), "location and host cohorts");
    CHECK(HAS("placement  auto") && HAS("placement  pinned") && HAS("generation generation 2") && HAS("generation generation unknown"), "placement and generation cohorts");
    CHECK(HAS("provider   claude") && HAS("provider   codex") && HAS("provider   no agent receipt"), "provider cohorts");
    CHECK(HAS("2 no telemetry block") && HAS("3 no task receipt") && HAS("1 receipt unverified") && HAS("1 peak_rss_bytes=rss_not_distinguishable"), "unmeasured reasons");
    CHECK(HAS("unknown     unknown     unknown"), "unmeasured host shows unknown, not zero");
    CHECK(!HAS(" 0s "), "no zero stands in for an unmeasured wall time");
    S("L");
    U("location: local");
    U("3 local steps, 0 remote steps on 0 hosts");
    S("L");
    U("location: remote");
    U("0 local steps, 4 remote steps on 2 hosts");
    S("L");
    U("location: alpha");
    U("0 local steps, 3 remote steps on 2 hosts");
    S("jj");
    kt_pump(&s, .15);
    CHECK(HAS(">host       beta"), "j scrolls the cohort table");
    S("0");
    U("location: local and remote");
    for (i = 0; i < 3; i++) {
        if (s.screen.cols != sizes[i][0] || s.screen.rows != sizes[i][1])
            kt_resize(&s, sizes[i][0], sizes[i][1]);
        U("REMOTE COHORTS");
        U("q quit");
        CHECK(!s.screen.overflow, "cohorts resize overflow");
        save(&s, "cohorts", sizes[i][0], sizes[i][1]);
    }
    CHECK(HAS("4 remote") && HAS("wall 227s (2/4)"), "compact cohorts keep coverage");
    kt_resize(&s, 140, 40);
    S("C");
    U("RUNS / newest creation first");
    S("L");
    U("Recorded workflow runs · all recorded · all work / location: local");
    U("1/1 / Enter evidence");
    S("LL");
    U("location: alpha");
    U("1/1 / Enter evidence");
    kt_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FIXTURE"), "reset fixture environment");
    puts("PASS remote cohorts: location/host/placement/generation/provider, unknown reasons, L filter, three sizes");
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
    fleet_statistics();
    metric_pages();
    session_statistics();
    remote_cohorts();
    return 0;
}
