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
    U("HYDRA / PLAN TOGETHER");
    /* Tab order: selected work, dependencies, then activity. */
    S("j\tjj\t\tjjj");
    kt_pump(&s, .2);
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
        U("HYDRA / STATISTICS");
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
        U("HYDRA / STATISTICS");
    }
    kt_resize(&s, 140, 40);
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
    U("Find host:");
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
    return 0;
}
