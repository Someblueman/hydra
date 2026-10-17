#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static char root[4096], build[4096], tui[4096], fake[4096], evidence[4096];
#define U(marker) tv_until(&s, (marker), 3)
#define S(keys) tv_send(&s, (keys))
#define HAS(marker) tv_contains(&s, (marker))
static void save(struct tv_session *s, const char *name, int cols, int rows) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, cols, rows);
    tv_save(s, path);
}
static void local_statistics(void) {
    struct tv_session s;
    char base[4096], failure[4096];
    int sizes[][2] = {{80, 24}, {40, 10}, {140, 40}};
    size_t i;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", NULL};
    tv_temp(base, sizeof(base), "hydra-statistics");
    tv_format(failure, sizeof(failure), "%s/fail", base);
    CHECK(!setenv("HYDRA_TEST_STATS_FAIL_FILE", failure, 1), "stats failure environment");
    tv_open(&s, argv, 140, 40, NULL);
    U("HYDRA / PLAN TOGETHER");
    /* Tab order: selected work, dependencies, then activity. */
    S("j\tjj\t\tjjj");
    tv_pump(&s, .2);
    CHECK(HAS("scroll 2") && HAS("scroll 3"), "workspace scroll setup");
    S("D");
    U("HYDRA / STATISTICS");
    U("Latest attempts average 95.0s over 6 timed steps, longest 120s");
    CHECK(HAS("Attempts 8/11"), "attempt coverage");
    save(&s, "statistics", 140, 40);
    S("T");
    U("Recorded workflow runs · 24 hours");
    CHECK(HAS("1 outside range"), "date exclusion");
    U("1/4 / Enter evidence");
    S("0/");
    U("Find workflow, run or project:");
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
    U("HYDRA / PLAN TOGETHER");
    CHECK(HAS("scroll 2") && HAS("scroll 3"), "preserved workspace scroll");
    S("D");
    U("Recorded workflow runs · all recorded · recovery");
    S("0/");
    U("Find workflow, run or project:");
    S("visualization\r");
    U("1/1 / Enter evidence");
    S("g");
    U("[Workflows]");
    U("prepare");
    S("\033");
    U("HYDRA / STATISTICS");
    U("Recorded workflow runs · all recorded · visualization");
    S("0");
    U("1/8 / Enter evidence");
    S("\033[<0;40;23M");
    tv_pump(&s, .15);
    U("3/8 / Enter evidence");
    S("!");
    U("all work / attention");
    S("0");
    tv_write(failure, "");
    S("r");
    U("STATISTICS (STALE)");
    CHECK(HAS("95.0s"), "last good statistics retained");
    CHECK(!unlink(failure), "remove failure trigger");
    S("r");
    tv_pump(&s, .25);
    CHECK(!HAS("STATISTICS (STALE)"), "refresh recovers");
    for (i = 0; i < 3; i++) {
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        U("HYDRA / STATISTICS");
        U("q quit");
        CHECK(!s.screen.overflow, "statistics resize overflow");
        CHECK(s.screen.clears >= 1, "resize invalidates presentation");
        save(&s, "statistics", sizes[i][0], sizes[i][1]);
    }
    tv_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FAIL_FILE"), "reset failure environment");
    CHECK(!rmdir(base), "remove owned stats fixture");
    puts("PASS statistics: scope/time, evidence and graph, preserved scroll, mouse, stale "
         "recovery, sizes, termios");
}
static void metric_pages(void) {
    struct tv_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    int sizes[][2] = {{140, 40}, {80, 24}, {40, 10}};
    const char *titles[] = {"QUEUE DELAY", "TOTAL EXECUTION", "RECORDED VERIFICATION",
                            "OWNER RECOVERIES"};
    const char *coverage[] = {"known 7/10", "known 4/5", "known 2/3", "known 6/8"};
    const char *names[] = {"queue-delay", "total-execution", "recorded-verification",
                           "owner-recoveries"};
    size_t i, j;
    tv_open(&s, argv, 140, 40, NULL);
    U("Latest attempts average");
    for (i = 0; i < 3; i++) {
        if (s.screen.cols != sizes[i][0] || s.screen.rows != sizes[i][1])
            tv_resize(&s, sizes[i][0], sizes[i][1]);
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
        U("HYDRA / STATISTICS");
    }
    tv_resize(&s, 140, 40);
    S("MMMM/");
    U("Find workflow, run or project:");
    S("recovery\r");
    U("known 2/2");
    S("\r");
    U("failed / 2");
    S("D");
    U("HYDRA / PLAN TOGETHER");
    S("D");
    U("OWNER RECOVERIES");
    U("known 2/2");
    tv_close(&s, "q", 0, 0);
    puts("PASS metrics: four pages, coverage, percentiles, freshness, filters, evidence, three "
         "sizes");
}
static void fleet_statistics(void) {
    struct tv_session s;
    const char *argv[] = {tui,       "--hydra", fake,         "--theme", "dark",
                          "--fleet", "--view",  "statistics", NULL};
    tv_open(&s, argv, 140, 40, NULL);
    U("HOST STATISTICS");
    U("2 responded   1 failed   1 reported heads");
    CHECK(HAS("offline") && HAS("-- heads"), "unknown failed-host heads");
    S("\r");
    U("/work/project");
    S("\033");
    U("> builder");
    S("/");
    U("Find host:");
    S("offline\r");
    U("0 responded   1 failed   -- reported heads");
    CHECK(HAS("Failed hosts have unknown head counts"), "unknown-host explanation");
    save(&s, "statistics-fleet", 140, 40);
    S("0");
    tv_resize(&s, 80, 14);
    S("k");
    tv_pump(&s, .1);
    CHECK(HAS("> empty"), "selection scrolls into view");
    S("j");
    U("> offline");
    tv_pump(&s, .1);
    tv_resize(&s, 40, 10);
    U("3/3 offline");
    U("q quit");
    CHECK(!s.screen.overflow, "small fleet statistics overflow");
    S("\r");
    U("No head evidence");
    tv_resize(&s, 140, 40);
    S("W");
    U("Hosts: 3 observed");
    S("\t\tz");
    U("offline · failed · unknown heads");
    CHECK(HAS("empty · responded · 0 heads"), "empty distinct from unknown");
    S("H");
    U("HOSTS / latest list response");
    tv_close(&s, "q", 0, 0);
    puts("PASS fleet statistics: responded/failed/empty, known counts, filters, drill-down");
}
/* Mixed local and remote runs (schema 5): the cohorts page, the location
 * filter and the side panel say what the receipts measured and what stays
 * unknown with its reason; nothing unmeasured is shown as zero. */
static void remote_cohorts(void) {
    struct tv_session s;
    const char *argv[] = {tui, "--hydra", fake, "--theme", "dark", "--view", "statistics", NULL};
    int sizes[][2] = {{140, 40}, {80, 24}, {40, 10}};
    size_t i;
    CHECK(!setenv("HYDRA_TEST_STATS_FIXTURE", "statistics-v5.tsv", 1), "schema 5 fixture environment");
    tv_open(&s, argv, 140, 40, NULL);
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
    tv_pump(&s, .15);
    CHECK(HAS(">host       beta"), "j scrolls the cohort table");
    S("0");
    U("location: local and remote");
    for (i = 0; i < 3; i++) {
        if (s.screen.cols != sizes[i][0] || s.screen.rows != sizes[i][1])
            tv_resize(&s, sizes[i][0], sizes[i][1]);
        U("REMOTE COHORTS");
        U("q quit");
        CHECK(!s.screen.overflow, "cohorts resize overflow");
        save(&s, "cohorts", sizes[i][0], sizes[i][1]);
    }
    CHECK(HAS("4 remote") && HAS("wall 227s (2/4)"), "compact cohorts keep coverage");
    tv_resize(&s, 140, 40);
    S("C");
    U("RUNS / newest creation first");
    S("L");
    U("Recorded workflow runs · all recorded · all work / location: local");
    U("1/1 / Enter evidence");
    S("LL");
    U("location: alpha");
    U("1/1 / Enter evidence");
    tv_close(&s, "q", 0, 0);
    CHECK(!unsetenv("HYDRA_TEST_STATS_FIXTURE"), "reset fixture environment");
    puts("PASS remote cohorts: location/host/placement/generation/provider, unknown reasons, L filter, three sizes");
}
int main(void) {
    tv_init();
    unsetenv("NO_COLOR");
    tv_paths(root, sizeof(root), build, sizeof(build));
    tv_format(tui, sizeof(tui), "%s/hydra-tui", build);
    tv_format(fake, sizeof(fake), "%s/tests/fixtures/tui/fake-hydra.sh", root);
    tv_format(evidence, sizeof(evidence), "%s/build/statistics-evidence", root);
    tv_mkdir(evidence);
    local_statistics();
    fleet_statistics();
    metric_pages();
    remote_cohorts();
    return 0;
}
