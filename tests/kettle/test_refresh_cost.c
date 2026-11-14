#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* What the periodic refresh costs when the control centre reads the state
 * store itself (as `hydra tui` starts it). git, tmux and the CLI are wrappers
 * that log every start, so the log is the count: the same handful of
 * processes per refresh for 4 heads and for 24, none of them the CLI. The
 * rationing of git is observed too: the selected head and a worktree whose
 * index changed are read at the next refresh, every worktree is read within a
 * bounded number of refreshes, and a read that runs out of time leaves the
 * last good snapshot on screen, marked stale. Workflow runs are asked of the
 * CLI only when their records changed or one has not ended. Observing head
 * activity rides on the refresh's one tmux command: the CLI is started once
 * per change of a head's activity and once in a while to say that someone is
 * watching, never per refresh or per head. */

static char root[4096], build[4096], tui[4096], base[4096], hydra[4096], calls[4096], slow[4096];
static char slow_record[4096], activity[4096];
static char real_git[4096];
static char log_text[1 << 20];

#define S(keys) kt_send(&s, (keys))
#define U(marker, timeout) kt_until(&s, (marker), (timeout))
#define HAS(marker) kt_contains(&s, (marker))

/* A project with <count> running heads, each on its own real worktree. */
static const char project_script[] =
    "set -eu\n"
    "base=\"$1\"; count=\"$2\"; repo=\"$base/repo\"; home=\"$base/home\"\n"
    "project=project_00000000000000c0\n"
    "put() { printf '%s\\n' \"$2\" > \"$1\"; }\n"
    "mkdir -p \"$repo\" \"$home\"\n"
    "git -C \"$repo\" init -q -b main\n"
    "put \"$repo/tracked.txt\" one\n"
    "git -C \"$repo\" add tracked.txt\n"
    "git -C \"$repo\" -c user.name=Test -c user.email=test@example.com commit -q -m fixture\n"
    "mkdir -p \"$repo/.git/hydra\"\n"
    "put \"$repo/.git/hydra/project-id\" \"$project\"\n"
    "records=\"$home/state/v2/projects/$project\"\n"
    "mkdir -p \"$records/heads\"\n"
    "put \"$home/state/v2/schema-version\" 2\n"
    "put \"$records/project-id\" \"$project\"\n"
    "put \"$records/repo-root\" \"$repo\"\n"
    "i=1\n"
    "while [ \"$i\" -le \"$count\" ]; do\n"
    "    id=\"$(printf '%016x' \"$i\")\"\n"
    "    head=\"$records/heads/head_$id\"; instance=\"$head/instances/instance_$id\"\n"
    "    mkdir -p \"$instance\" \"$head/events\"\n"
    "    git -C \"$repo\" worktree add -q \"$base/trees/$i\" -b \"work-$i\"\n"
    "    put \"$head/head-id\" \"head_$id\"\n"
    "    put \"$head/branch\" \"work-$i\"\n"
    "    put \"$head/session\" \"session-$i\"\n"
    "    put \"$head/terminal-mode\" interactive\n"
    "    put \"$head/profile\" none\n"
    "    put \"$head/group\" -\n"
    "    put \"$head/created-at\" 100\n"
    "    put \"$head/dependencies\" -\n"
    "    put \"$head/pr\" -\n"
    "    put \"$head/current-instance\" \"instance_$id\"\n"
    "    put \"$head/desired-state\" running\n"
    "    put \"$head/worktree\" \"$base/trees/$i\"\n"
    "    put \"$instance/instance-id\" \"instance_$id\"\n"
    "    put \"$instance/session\" \"session-$i\"\n"
    "    put \"$instance/terminal-mode\" interactive\n"
    "    put \"$instance/observed-status\" idle\n"
    "    put \"$instance/observed-confidence\" exact\n"
    "    : > \"$head/events/events.jsonl\"\n"
    "    i=$((i + 1))\n"
    "done\n";

/* One recorded run of two steps in the project, in the given state. */
static const char run_script[] =
    "set -eu\n"
    "run=\"$1/home/state/v2/projects/project_00000000000000c0/workflows/runs/run_00000000000000a1\"\n"
    "put() { printf '%s\\n' \"$2\" > \"$1.new\"; mv \"$1.new\" \"$1\"; }\n"
    "mkdir -p \"$run/steps/build\" \"$run/steps/check\"\n"
    "put \"$run/workflow-id\" release\n"
    "put \"$run/state\" \"$2\"\n"
    "put \"$run/created-at\" 100\n"
    "put \"$run/project-id\" project_00000000000000c0\n"
    "printf 'step\\tbuild\\tcommand\\t-\\t\\t\\t-\\t-\\t\\t-\\nstep\\tcheck\\tcommand\\tbuild\\t\\t\\t-\\t-\\t\\t-\\n' > \"$run/graph.tsv\"\n"
    "put \"$run/steps/build/state\" succeeded\n"
    "put \"$run/steps/build/attempts\" 1\n"
    "put \"$run/steps/check/state\" succeeded\n"
    "put \"$run/steps/check/attempts\" 1\n";

static const char git_script[] = "#!/bin/sh\nprintf 'git %s\\n' \"$*\" >> \"$RC_CALLS\"\nexec \"$RC_GIT\" \"$@\"\n";
/* An activity recorder also logs when it has finished, and can be slowed
 * down so that one is still running when the control centre quits. */
static const char hydra_script[] =
    "#!/bin/sh\n"
    "printf 'hydra %s\\n' \"$*\" >> \"$RC_CALLS\"\n"
    "[ \"${1:-}:${2:-}\" = adapter:activity ] || exec \"$RC_HYDRA\" \"$@\"\n"
    "if [ -f \"$RC_SLOW_RECORD\" ]; then sleep 1; fi\n"
    "\"$RC_HYDRA\" \"$@\"\n"
    "status=$?\n"
    "printf 'recorded %s %s\\n' \"$3\" \"$status\" >> \"$RC_CALLS\"\n"
    "exit \"$status\"\n";
static const char tmux_script[] =
    "#!/bin/sh\n"
    "printf 'tmux %s\\n' \"$*\" >> \"$RC_CALLS\"\n"
    "if [ \"${1:-}\" = list-sessions ] && [ -f \"$RC_SLOW\" ]; then sleep 30; fi\n"
    "exec \"$RC_TMUX\" \"$@\"\n";

static void environment(const char *name, const char *value) {
    CHECK(!setenv(name, value, 1), "fixture environment");
}

static void executable(const char *name, const char *text) {
    char path[8192];
    kt_format(path, sizeof(path), "%s/bin/%s", base, name);
    kt_write(path, text);
    CHECK(!chmod(path, 0755), "fixture script is executable");
}

/* The wrappers, once: they outlive each project built under base. */
static void wrappers(void) {
    char value[16384];
    const char *find[] = {"sh", "-c", "command -v git", NULL};
    kt_paths(root, sizeof(root), build, sizeof(build));
    kt_format(tui, sizeof(tui), "%s/hydra-tui", build);
    kt_temp(base, sizeof(base), "hydra-refresh-cost");
    CHECK(kt_command(NULL, NULL, real_git, sizeof(real_git), 5, find) == 0, "git available");
    real_git[strcspn(real_git, "\n")] = 0;
    kt_format(value, sizeof(value), "%s/bin", base);
    kt_mkdir(value);
    executable("git", git_script);
    executable("tmux", tmux_script);
    executable("hydra", hydra_script);
    kt_format(hydra, sizeof(hydra), "%s/bin/hydra", base);
    kt_format(calls, sizeof(calls), "%s/calls", base);
    kt_format(slow, sizeof(slow), "%s/tmux-is-slow", base);
    environment("RC_GIT", real_git);
    environment("RC_CALLS", calls);
    environment("RC_SLOW", slow);
    kt_format(slow_record, sizeof(slow_record), "%s/recording-is-slow", base);
    environment("RC_SLOW_RECORD", slow_record);
    kt_format(activity, sizeof(activity), "%s/window-activity", base);
    kt_format(value, sizeof(value), "%s/bin/hydra", root);
    environment("RC_HYDRA", value);
    kt_format(value, sizeof(value), "%s/tests/fixtures/tui/fake-bin/tmux", root);
    environment("RC_TMUX", value);
    kt_format(value, sizeof(value), "%s/bin:%s", base, getenv("PATH") ? getenv("PATH") : "");
    environment("PATH", value);
    environment("TERM", "xterm-256color");
    environment("HYDRA_NONINTERACTIVE", "1");
    unsetenv("TMUX");
    unsetenv("HYDRA_STATE_V2_ROOT");
}

/* Builds the project under <base>/<name>; every head's session is live. */
static void build_project(const char *name, int heads, char *project, size_t capacity) {
    char count[16], sessions[4096] = "", home[8192];
    const char *make[] = {"sh", "-c", project_script, "sh", project, count, NULL};
    int i;
    kt_format(project, capacity, "%s/%s", base, name);
    kt_format(count, sizeof(count), "%d", heads);
    kt_mkdir(project);
    kt_command_ok(NULL, make);
    for (i = 1; i <= heads; i++) {
        size_t used = strlen(sessions);
        kt_format(sessions + used, sizeof(sessions) - used, "%ssession-%d", i > 1 ? "," : "", i);
    }
    environment("FAKE_TMUX_SESSIONS", sessions);
    kt_format(home, sizeof(home), "%s/home", project);
    environment("HYDRA_HOME", home);
}

static void open_session(struct kt_session *s, const char *project) {
    const char *argv[] = {tui, "--hydra", hydra, "--store", "--view", "heads", NULL};
    char repo[8192];
    kt_format(repo, sizeof(repo), "%s/repo", project);
    kt_write(calls, "");
    kt_open_terminal(s, argv, 140, 40, repo);
    kt_until(s, "work-1", 10);
}

static void open_project(struct kt_session *s, const char *name, int heads, char *project, size_t capacity) {
    build_project(name, heads, project, capacity);
    open_session(s, project);
}

static void record_run(const char *project, const char *state) {
    const char *make[] = {"sh", "-c", run_script, "sh", project, state, NULL};
    kt_command_ok(NULL, make);
}

/* Lines of the call log that begin with prefix and contain needle. */
static int logged(const char *prefix, const char *needle) {
    const char *line;
    int count = 0;
    kt_read(calls, log_text, sizeof(log_text));
    for (line = log_text; *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : line + strlen(line)) {
        const char *end = strchr(line, '\n');
        const char *found = strstr(line, needle);
        if (!strncmp(line, prefix, strlen(prefix)) && found && (!end || found < end)) count++;
    }
    return count;
}

struct cost { int refreshes, git, tmux, hydra; };

/* Watches whole refreshes: the log is cleared between two of them and read
 * between two later ones. */
static struct cost watch(struct kt_session *s, double seconds) {
    struct cost c;
    kt_pump(s, 1.0);
    kt_write(calls, "");
    kt_pump(s, seconds);
    c.refreshes = logged("tmux ", "list-sessions");
    c.git = logged("git ", "");
    c.tmux = logged("tmux ", "");
    c.hydra = logged("hydra ", "");
    return c;
}

static void cost_bounded(const struct cost *c) {
    CHECK(c->tmux == c->refreshes, "one tmux command per refresh, for every session at once");
    CHECK(c->hydra == 0, "the CLI is not started for a refresh");
    CHECK(c->git <= 5 * c->refreshes, "git reads at most the selected worktree and four others per refresh");
    CHECK(c->git + c->tmux < 10 * c->refreshes, "a refresh starts fewer than ten processes");
}

static struct cost steady_cost(const char *name, int heads) {
    struct kt_session s;
    struct cost c;
    char project[4096];
    open_project(&s, name, heads, project, sizeof(project));
    c = watch(&s, 6.0);
    printf("%d heads: %d refreshes started %d git, %d tmux and %d CLI processes\n", heads, c.refreshes, c.git, c.tmux, c.hydra);
    CHECK(c.refreshes >= 2 && c.refreshes <= 4, "the refresh keeps its two-second cadence");
    cost_bounded(&c);
    kt_close(&s, "q", 0, 0);
    return c;
}

static void changed_index(const char *project, int tree) {
    char path[8192];
    const char *add[] = {real_git, "-C", path, "add", "staged.txt", NULL};
    kt_format(path, sizeof(path), "%s/trees/%d/staged.txt", project, tree);
    kt_write(path, "staged\n");
    kt_format(path, sizeof(path), "%s/trees/%d", project, tree);
    kt_command_ok(NULL, add);
}

static int tree_reads(const char *project, int tree) {
    char needle[8192];
    kt_format(needle, sizeof(needle), "-C %s/trees/%d status", project, tree);
    return logged("git ", needle);
}

/* Every worktree comes round: 23 unselected ones, four per refresh. */
static void every_worktree_in_turn(struct kt_session *s, const char *project) {
    struct cost c = watch(s, 14.0);
    int tree, unread = 0;
    for (tree = 1; tree <= 24; tree++) unread += tree_reads(project, tree) == 0;
    printf("24 heads over %d refreshes: %d git processes, %d worktrees never read\n", c.refreshes, c.git, unread);
    CHECK(c.refreshes >= 6 && unread == 0, "every worktree is read again within six refreshes");
    CHECK(tree_reads(project, 1) == c.refreshes, "the selected head's worktree is read at every refresh");
    cost_bounded(&c);
}

/* The rules git is rationed by, at 24 heads. */
static void rationing(void) {
    struct kt_session s;
    char project[4096], path[8192];
    open_project(&s, "rationed", 24, project, sizeof(project));
    every_worktree_in_turn(&s, project);
    /* An uncommitted file in the selected worktree shows at the next refresh. */
    kt_format(path, sizeof(path), "%s/trees/1/untracked.txt", project);
    kt_write(path, "new\n");
    U("1 changed file", 4);
    /* Three worktrees whose index changed are all read at the next refresh. */
    kt_pump(&s, 1.0);
    changed_index(project, 20);
    changed_index(project, 21);
    changed_index(project, 22);
    kt_write(calls, "");
    kt_pump(&s, 2.6);
    CHECK(tree_reads(project, 20) && tree_reads(project, 21) && tree_reads(project, 22),
          "a worktree whose index changed is read at the next refresh");
    kt_close(&s, "q", 0, 0);
    puts("PASS rationing: selected head every refresh, changed index next refresh, every worktree in turn");
}

/* tmux does not answer: the refresh gives up after its budget, and says so. */
static void slow_read(void) {
    struct kt_session s;
    char project[4096];
    open_project(&s, "slow", 2, project, sizeof(project));
    U("\xe2\x97\x8f live", 5);
    kt_write(slow, "");
    U("last good snapshot", 16);
    CHECK(HAS("state read timed out; showing last good snapshot"), "a read that ran out of time is named");
    CHECK(HAS("work-1") && HAS("work-2"), "the last good snapshot stays on screen");
    S("?");
    U("KEYBOARD HELP", 1);
    S("?");
    CHECK(!unlink(slow), "tmux answers again");
    U("\xe2\x97\x8f live", 16);
    kt_close(&s, "q", 0, 0);
    puts("PASS slow read: stale after the budget, last good snapshot kept, keys answered, live again");
}

/* A run that has ended is read from the CLI once and again only when its
 * records change; a run that has not ended is read at every refresh. */
static void workflow_reads(void) {
    struct kt_session s;
    char project[4096];
    build_project("runs", 2, project, sizeof(project));
    record_run(project, "finished");
    open_session(&s, project);
    kt_pump(&s, 3.0);
    kt_write(calls, "");
    kt_pump(&s, 6.0);
    CHECK(logged("hydra ", "") == 0, "an ended, unchanged run is not asked of the CLI again");
    /* The workspace reads its links once, then only when a graph changes. */
    S("W");
    U("PLAN TOGETHER", 3);
    kt_pump(&s, 3.0);
    kt_write(calls, "");
    kt_pump(&s, 6.0);
    CHECK(logged("hydra ", "") == 0, "unchanged workspace links are not asked of the CLI again");
    S("w");
    U("release / finished / run 1 of 1", 4);
    record_run(project, "failed");
    U("release / failed / run 1 of 1", 5);
    /* The CLI calls a run without a live owner stale: only it can tell. */
    record_run(project, "running");
    U("release / stale / run 1 of 1", 5);
    kt_pump(&s, 1.0);
    kt_write(calls, "");
    kt_pump(&s, 4.6);
    printf("a run that has not ended: %d workflow reads in two refreshes\n", logged("hydra ", "workflow tui-data"));
    CHECK(logged("hydra ", "workflow tui-data") >= 2, "a run that has not ended is read at every refresh");
    kt_close(&s, "q", 0, 0);
    puts("PASS workflow reads: ended runs once, again when a record changes; unended runs every refresh");
}

/* The activity.changed events in one head instance's activity log that
 * carry text. */
static int recorded_events(const char *project, int head, const char *text) {
    static char log[1 << 16];
    char path[8192];
    const char *at;
    int count = 0;
    kt_format(path, sizeof(path),
              "%s/home/state/v2/projects/project_00000000000000c0/heads/head_%016x/instances/instance_%016x/activity.jsonl",
              project, (unsigned)head, (unsigned)head);
    if (!kt_exists(path)) return 0;
    kt_read(path, log, sizeof(log));
    for (at = strstr(log, "\"type\":\"activity.changed\""); at; at = strstr(at + 1, "\"type\":\"activity.changed\"")) {
        const char *end = strchr(at, '\n'), *found = strstr(at, text);
        if (found && (!end || found < end)) count++;
    }
    return count;
}

/* When each session's window last produced output, as the fake tmux says it. */
static void window_activity(const char *first, long long others) {
    char text[512];
    kt_format(text, sizeof(text), "session-1 %s\nsession-2 %lld\nsession-3 %lld\n", first, others, others - 1);
    kt_write(activity, text);
}

/* Three silent heads are each recorded as quiet once; after that refreshes
 * go on and the CLI is asked only to note that someone is watching. */
static void activity_steady(struct kt_session *s, const char *project, long long silent_since) {
    char since[64];
    kt_format(since, sizeof(since), "\"state\":\"quiet\",\"since\":%lld,", silent_since);
    kt_pump(s, 3.0);
    CHECK(logged("recorded ", " 0") == 3, "the first state of every head is recorded once");
    CHECK(recorded_events(project, 1, since) == 1, "a quiet head is recorded with the moment its output stopped");
    CHECK(recorded_events(project, 2, "\"source\":\"tmux\",\"confidence\":\"inferred\"") == 1, "the record says it is inferred from tmux");
}

static void activity_unchanged(struct kt_session *s) {
    struct cost c = watch(s, 6.0);
    CHECK(c.refreshes >= 2 && c.tmux == c.refreshes, "observing three heads adds no tmux command to a refresh");
    CHECK(logged("hydra ", "adapter activity") == 0, "an unchanged head starts no recorder");
    CHECK(c.hydra <= 1 && c.hydra == logged("hydra ", "adapter watch"), "the CLI is started only to note the observer, not per refresh");
}

static void activity_changed_once(const char *project);

/* One head starts producing output and keeps going: one recorder, once. */
static void activity_change(struct kt_session *s, const char *project, long long silent_since) {
    double waited = 0;
    kt_write(calls, "");
    window_activity("now", silent_since);
    while (waited < 15 && !logged("recorded ", "work-1 0")) { kt_pump(s, 0.5); waited += 0.5; }
    CHECK(waited >= 3.5, "output shorter than four seconds is not yet a change");
    kt_pump(s, 4.5);
    activity_changed_once(project);
}

static void activity_changed_once(const char *project) {
    CHECK(logged("hydra ", "adapter activity") == 1, "a change starts exactly one recorder");
    CHECK(logged("hydra ", "adapter activity work-1") == 1, "the recorder is for the head that changed");
    CHECK(recorded_events(project, 1, "\"state\":\"active\"") == 1, "the change is in the instance's activity log");
    CHECK(recorded_events(project, 2, "\"state\":\"active\"") == 0, "the other heads record nothing");
}

/* The active head pauses for less than ten seconds and goes on: a spinner
 * pause is not a pair of changes. */
static void activity_pause(struct kt_session *s, const char *project, long long silent_since) {
    char paused[32];
    kt_write(calls, "");
    kt_format(paused, sizeof(paused), "%lld", (long long)time(NULL));
    window_activity(paused, silent_since);
    kt_pump(s, 7.0);
    window_activity("now", silent_since);
    kt_pump(s, 3.0);
    CHECK(logged("hydra ", "adapter activity") == 0, "a pause shorter than ten seconds starts no recorder");
    CHECK(recorded_events(project, 1, "\"state\":\"quiet\"") == 1, "the paused head is still recorded quiet only once");
}

/* Inferred head activity: observed with the refresh's own tmux command and
 * recorded through the CLI once per change; quitting waits for recorders that
 * are still appending and leaves none behind. */
static void activity_observed(void) {
    struct kt_session s;
    char project[4096];
    long long silent_since = (long long)time(NULL) - 300;
    char first[32];
    kt_format(first, sizeof(first), "%lld", silent_since);
    window_activity(first, silent_since);
    environment("FAKE_TMUX_ACTIVITY", activity);
    open_project(&s, "activity", 3, project, sizeof(project));
    activity_steady(&s, project, silent_since);
    activity_unchanged(&s);
    activity_change(&s, project, silent_since);
    activity_pause(&s, project, silent_since);
    kt_close(&s, "q", 0, 0);
    /* A second observer finds two heads quiet as recorded and one whose
     * output has stopped: the recorders it starts are still running when it
     * is asked to quit. */
    kt_format(first, sizeof(first), "%lld", silent_since + 200);
    window_activity(first, silent_since);
    kt_write(slow_record, "");
    open_session(&s, project);
    while (!logged("hydra ", "adapter activity")) kt_pump(&s, 0.2);
    kt_close(&s, "q", 0, 0);
    CHECK(logged("hydra ", "adapter activity") >= 1, "recorders were running at quit");
    CHECK(logged("hydra ", "adapter activity") == logged("recorded ", ""), "quitting waits for running recorders and leaves none behind");
    CHECK(recorded_events(project, 2, "\"state\":\"quiet\"") == 1, "a state another observer recorded is not recorded twice");
    CHECK(!unlink(slow_record) && !unlink(activity), "activity fixtures removed");
    unsetenv("FAKE_TMUX_ACTIVITY");
    puts("PASS activity: no process per refresh, one recorder per change, none without or for a short pause, none left at quit");
}

int main(void) {
    const char *remove[] = {"rm", "-rf", base, NULL};
    struct cost few, many;
    kt_init();
    wrappers();
    few = steady_cost("few", 4);
    many = steady_cost("many", 24);
    CHECK(many.git * few.refreshes <= (few.git + few.refreshes) * many.refreshes,
          "24 heads cost at most one more process per refresh than 4 heads");
    puts("PASS cost: a refresh starts the same few processes for 4 and for 24 heads, and never the CLI");
    rationing();
    workflow_reads();
    activity_observed();
    slow_read();
    kt_command_ok(NULL, remove);
    return 0;
}
