/* Pure planning helpers of the native TUI: task-name branches, the profile
 * named by the guided local policy, and readable validation failures. */
#include "../../src/tui/task_name.h"
#include "../../src/tui/plan_diagnostics.h"
#include "../../src/tui/plan_summary.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

static void branch_case(const char *name, const char *expected, const char *label) {
    char out[256] = "sentinel";
    int ok = task_branch_name(name, out, sizeof(out));
    if (!expected) {
        check(!ok && !out[0], label);
        return;
    }
    check(ok && !strcmp(out, expected) && task_branch_valid(out), label);
    if (!ok || strcmp(out, expected)) printf("    got \"%s\", expected \"%s\"\n", out, expected);
}

static void branch_cases(void) {
    char long_name[300], out[256];
    branch_case("add kill dry run", "add-kill-dry-run", "spaces become single separators");
    branch_case("add\tkill \t  dry\n run", "add-kill-dry-run", "tabs and whitespace runs collapse");
    branch_case("Add Kill Dry-Run", "add-kill-dry-run", "uppercase letters are lowercased");
    branch_case("fix: kill --dry-run (v2.1)!", "fix-kill-dry-run-v2-1", "punctuation runs collapse to one separator");
    branch_case("  --leading and trailing--  ", "leading-and-trailing", "leading and trailing separators are trimmed");
    branch_case("feat/add kill", "feat/add-kill", "slashes keep branch components");
    branch_case("feat // a..b", "feat/a-b", "slash runs never repeat and dots never survive");
    branch_case("caf\xc3\xa9 r\xc3\xa9sum\xc3\xa9", "caf-r-sum", "UTF-8 bytes are separators, not transliterated");
    branch_case("bad\xff\xfe" "bytes", "bad-bytes", "invalid bytes are separators");
    branch_case("\xe2\x9c\x93 !!! ---", NULL, "a name without letters or digits is refused");
    branch_case("   ", NULL, "whitespace only is refused");
    branch_case("", NULL, "empty input is refused");
    branch_case("feature/add-kill_v2.1", "feature/add-kill_v2.1", "an already valid name is unchanged");
    branch_case("Feature-X", "Feature-X", "valid mixed case is kept as typed");
    branch_case("x.lock", "x-lock", "Git's .lock suffix is rewritten");
    branch_case(".hidden", "hidden", "leading dots are rewritten");
    branch_case("a/.b", "a/b", "components never start with a dot");
    branch_case("-flag", "flag", "a leading dash is rewritten");
    memset(long_name, 'a', sizeof(long_name) - 1U);
    long_name[sizeof(long_name) - 1U] = '\0';
    long_name[10] = ' ';
    check(task_branch_name(long_name, out, sizeof(out)) && strlen(out) == TASK_BRANCH_SLUG_MAX &&
          out[TASK_BRANCH_SLUG_MAX - 1U] != '-', "derived branches are bounded without a trailing separator");
    memset(long_name, 'a', 200U); long_name[200] = '\0';
    check(task_branch_name(long_name, out, sizeof(out)) && strlen(out) == 200U, "long valid names are kept");
    check(task_branch_name("a b c", out, 3U) && !strcmp(out, "a"), "a small output buffer bounds the slug");
    check(!task_branch_name("a b", out, 0U), "a zero-sized output is refused");
    check(!task_branch_valid("a b") && !task_branch_valid("a//b") && !task_branch_valid("a/") &&
          !task_branch_valid("a@{b") && !task_branch_valid("a.") && task_branch_valid("a_b.c/d-e"),
          "validity mirrors Hydra's spawn branch rules");
}

static void profile_cases(void) {
    check(task_profile_tool("codex") && task_profile_tool("claude-code") && task_profile_tool("pi2"),
          "plan IDs name a profile tool");
    check(!task_profile_tool("none") && !task_profile_tool("") && !task_profile_tool("-") && !task_profile_tool(NULL),
          "absent profiles authorize no agent tool");
    check(!task_profile_tool("Bad") && !task_profile_tool("bad name") && !task_profile_tool("trail-") &&
          !task_profile_tool("a--b") && !task_profile_tool("9lives") && !task_profile_tool("a\"b"),
          "profiles outside the plan ID grammar are not named");
}

static void summary_cases(void) {
    static char out[4096];
    const char *dirty =
        "{\"schema_version\":1,\"ok\":false,\"data\":{\"diagnostics\":[{\"path\":\"source\",\"code\":\"invalid_source\","
        "\"message\":\"source \\/repo has 1 tracked change: README; commit\",\"source\":\"\\/repo\",\"condition\":\"tracked_changes\","
        "\"recovery\":\"commit or stash these changes in \\/repo, then validate again\",\"changed_paths\":[\"README\"],\"changed_count\":1},"
        "{\"path\":\"tools\",\"code\":\"unauthorized\",\"message\":\"say \\\"hi\\\"\\u0007\"}],\"coverage\":[]},"
        "\"error\":{\"code\":\"invalid_plan\",\"message\":\"plan compilation failed; see data.diagnostics\"}}";
    size_t n = plan_failure_summary(dirty, out, sizeof(out));
    check(n && strstr(out, "Validation failed; no execution occurred.\n- source: source /repo has 1 tracked change: README; commit\n"
                           "  Next: commit or stash these changes in /repo, then validate again\n- tools: say \"hi\"?") != NULL,
          "diagnostics become readable messages with recovery");
    check(n && !strstr(out, "- plan compilation failed") && strstr(out, "Compiler response:\n{\"schema_version\"") != NULL,
          "the envelope message is not repeated and raw detail follows");
    check(plan_failure_summary("{\"ok\":false,\"error\":{\"message\":\"x\"}}", out, sizeof(out)) == 0 && !out[0],
          "responses without diagnostics are shown unchanged");
    check(plan_failure_summary("{\"data\":{\"diagnostics\":[{\"path\":\"a\",\"message\":\"unterminated", out, sizeof(out)) == 0,
          "truncated responses are shown unchanged");
    check(plan_failure_summary(dirty, out, 8U) == 7U && strlen(out) == 7U, "the summary is bounded by its buffer");
}

static void execution_cases(void) {
    static const char guided[] = "Plan fixture (hydra-plan-1)\nObjective\n\nContext:\nTools: sh , make , profile:codex\n"
        "Effects: execute , worktree\nDeclared repository writes: @spawned:*\n"
        "Budgets: parallelism 1; wall time 3600 seconds; artifacts 1048576 bytes; heads 4; disk floor 1024 MiB; retries 0; repairs 0\n";
    static const char fixture[] = "Tools: sh\nDeclared repository writes:\n"
        "Budgets: parallelism 1; wall time 181 seconds; artifacts 4096 bytes; heads 1; disk floor 1 MiB; retries 0; repairs 0\n";
    struct plan_summary s;
    char out[512];
    check(plan_summary_parse(guided, sizeof(guided) - 1U, &s) && s.parallelism == 1U && s.heads == 4U && s.minutes == 60U,
          "the approval summary reads the guided policy budgets");
    plan_summary_policy(&s, out, sizeof(out));
    check(!strcmp(out, "tools sh, make, profile:codex; parallelism 1; at most 4 heads; 60 min wall time"),
          "the policy summary names tools and budgets");
    plan_summary_consequences(&s, 3U, 1U, out, sizeof(out));
    check(!strcmp(out, "Runs 3 steps, spawns 1 head, up to 60 minutes; writes only inside heads it spawns."),
          "consequences name steps, spawned heads, time and the write boundary");
    check(plan_summary_parse(fixture, sizeof(fixture) - 1U, &s) && s.minutes == 4U && !s.writes[0],
          "partial minutes round up and empty writes stay empty");
    plan_summary_consequences(&s, 1U, 0U, out, sizeof(out));
    check(!strcmp(out, "Runs 1 step, spawns 0 heads, up to 4 minutes; declares no repository writes."),
          "a plan without declared writes says so");
    check(!plan_summary_parse("Tools: sh\n", 10U, &s), "a preview without budgets has no approval summary");
}

int main(void) {
    branch_cases();
    profile_cases();
    summary_cases();
    execution_cases();
    return failures ? 1 : 0;
}
