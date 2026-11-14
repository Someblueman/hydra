#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static struct hf_fixture f;
static char evidence[4096], snapshot[1048576];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define REFUSE(...) hf_run(&f, NULL, -2, (const char *[]){__VA_ARGS__, NULL})
#define S(keys) kt_send(&s, (keys))
#define U(marker, seconds) kt_until(&s, (marker), (seconds))
static json_object *field(json_object *o, const char *key) {
    json_object *v = NULL;
    CHECK(o && json_object_object_get_ex(o, key, &v), key);
    return v;
}
static void projection_digest(const char *projection, char *out, size_t capacity) {
    const char *p = projection, *end;
    while (strncmp(p, "P\t", 2)) {
        p = strchr(p, '\n');
        CHECK(p, "plan projection row");
        p++;
    }
    p += 2;
    end = strchr(p, '\t');
    CHECK(end, "plan projection digest");
    CHECK((size_t)(end - p) < capacity, "digest capacity");
    memcpy(out, p, (size_t)(end - p));
    out[end - p] = 0;
}
static void save(struct kt_session *s, const char *name, int width, int height) {
    char path[4096];
    kt_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, width, height);
    kt_save(s, path);
}
static bool state_is(const char *run_dir, const char *expected) {
    char path[4096], value[256];
    kt_format(path, sizeof(path), "%s/state", run_dir);
    kt_read(path, value, sizeof(value));
    hf_trim(value);
    return !strcmp(value, expected);
}
/* The approval shows the revision and a short digest; the CLI still receives
 * the full digest, which the launch receipt directory name proves. Only the
 * dialog's inside is read, where its text wraps; the view behind is not. */
static bool dialog_bounds(struct kt_session *s, int *top, int *left, int *right) {
    int y, x;
    for (y = 0; y < s->screen.rows; y++) {
        char row[2048];
        if (!strstr(kt_row(s, y, row, sizeof(row)), "Execute plan")) continue;
        for (x = 0; x < s->screen.cols && strcmp(s->screen.cells[y * s->screen.cols + x].text, "\xe2\x95\xad"); x++) {}
        *left = x;
        for (x = *left + 1; x < s->screen.cols && strcmp(s->screen.cells[y * s->screen.cols + x].text, "\xe2\x95\xae"); x++) {}
        *right = x; *top = y;
        return *left < *right && *right < s->screen.cols;
    }
    return false;
}
/* The dialog's text without blanks, so wrapped sentences read whole. */
static size_t row_compact(struct kt_session *s, int y, int left, int right, char *out, size_t used, size_t capacity) {
    int x;
    for (x = left + 1; x < right; x++) {
        const char *t = s->screen.cells[y * s->screen.cols + x].text;
        size_t len = strlen(t);
        if (!len || !strcmp(t, " ") || used + len + 1 >= capacity) continue;
        memcpy(out + used, t, len);
        used += len;
    }
    return used;
}
static void dialog_compact(struct kt_session *s, char *compact, size_t capacity) {
    size_t n = 0;
    int top = 0, left = 0, right = 0, y;
    CHECK(dialog_bounds(s, &top, &left, &right), "approval dialog on screen");
    for (y = top + 1; y < s->screen.rows; y++) n = row_compact(s, y, left, right, compact, n, capacity);
    compact[n] = 0;
}
/* Whether the dialog says text (compared without blanks). */
static bool dialog_says(struct kt_session *s, const char *text) {
    static char compact[131072];
    char wanted[1024];
    size_t n = 0;
    for (; *text && n + 1 < sizeof(wanted); text++) if (*text != ' ') wanted[n++] = *text;
    wanted[n] = 0;
    dialog_compact(s, compact, sizeof(compact));
    if (!strstr(compact, wanted)) fprintf(stderr, "The dialog at %dx%d lacks %s:\n%s\n", s->screen.cols, s->screen.rows, wanted, compact);
    return strstr(compact, wanted) != NULL;
}
static void check_digest(struct kt_session *s, const char *digest) {
    char marker[64];
    kt_format(marker, sizeof(marker), "digest%.12s?", digest);
    CHECK(dialog_says(s, marker), "short digest visible at all sizes");
}
/* A draft or policy change while the approval is open invalidates it. */
static void refuse_changed(struct kt_session *s, const char *changed, const char *runs) {
    kt_send(s, "E");
    kt_until(s, "Execute plan", 3);
    kt_append(changed, "\n");
    kt_pump(s, 2.5);
    kt_send(s, "y");
    kt_until(s, "Execution not submitted", 3);
    CHECK(!hf_glob_count(runs), "changed revision creates no run");
}
/* The confirmation names the revision, short digest, policy and consequences
 * at every size; only an explicit y approves: other keys and paste leave it
 * open, and Enter (its default button is Cancel) declines without a run. */
static void review_approval(struct kt_session *s, const char *digest) {
    int sizes[][2] = {{40, 10}, {80, 24}, {140, 40}};
    size_t i;
    kt_send(s, "E");
    kt_until(s, "Execute plan", 3);
    kt_until(s, "Execute imported plan revision 1", 3);
    for (i = 0; i < 3; i++) {
        kt_resize(s, sizes[i][0], sizes[i][1]);
        kt_pump(s, .3);
        check_digest(s, digest);
        CHECK(!s->screen.overflow, "approval resize");
        save(s, "approval", sizes[i][0], sizes[i][1]);
    }
    CHECK(dialog_says(s, "Runs 3 steps, spawns 1 head, up to 3 minutes; declares no repository writes."),
          "approval states the consequences");
    CHECK(dialog_says(s, "Policy: tools sh; parallelism 1; at most 1 head; 3 min wall time"),
          "approval summarizes the policy");
    kt_send(s, "x\033[200~y\033[201~");
    kt_pump(s, .5);
    CHECK(kt_contains(s, "Execute plan"), "other keys and paste cannot approve");
    kt_send(s, "\r");
    kt_until(s, "Execution not approved", 3);
    /* The confirmation is part of the frame: opening it, resizing under it
     * and closing it overwrite cells and never erase the display. */
    kt_pump(s, .3);
    CHECK(!kt_contains(s, "Execute plan"), "approval closed");
    CHECK(hf_count(s->raw, "\033[2J") == 1, "a confirmation opens, resizes and closes without erasing the display");
}
static void sanitize_path(const char *input, char *output, size_t capacity) {
    size_t i;
    CHECK(strlen(input) < capacity, "path bound");
    for (i = 0; input[i]; i++)
        output[i] = (input[i] == '\t' || input[i] == '\n') ? ' ' : input[i];
    output[i] = 0;
}
int main(void) {
    struct kt_session s;
    char path[4096], draft[4096], policy[4096], compiled[4096], digest[128], pattern[4096],
        receipt[4096], run_id[256], run_dir[4096], artifact[4096], expected[65536], links[65536],
        moved[65536], project[256], old_repo[4096], new_repo[4096], resolved[4096], sanitized[4096],
        row[8192];
    int sizes[][2] = {{40, 10}, {80, 24}, {140, 40}};
    size_t i;
    double deadline;
    json_object *result;
    kt_init();
    hf_init(&f, "hydra-plan-launch", "repo\tline", true, true);
    hf_file(&f, "compose.sh", path, sizeof(path));
    kt_read(path, expected, sizeof(expected));
    {
        const char *marker = strstr(expected, "set -eu");
        size_t offset;
        CHECK(marker, "compose strict-mode line");
        offset = (size_t)(marker - expected) + strlen("set -eu");
        kt_format(snapshot, sizeof(snapshot),
                  "%.*s\nsleep 5\nprintf 'compose-output-proof\\nVERIFIED ARTIFACTS from untrusted "
                  "output\\n'%s",
                  (int)offset, expected, expected + offset);
        kt_write(path, snapshot);
    }
    setenv("TMPDIR", f.base, 1);
    kt_format(draft, sizeof(draft), "%s/draft.json", f.base);
    kt_format(policy, sizeof(policy), "%s/policy.json", f.base);
    kt_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
    kt_copy(path, draft, false);
    kt_format(path, sizeof(path), "%s/tests/fixtures/plan/policy.json", f.root);
    kt_copy(path, policy, false);
    hf_commit_init(&f);
    hf_open(&f, &s);
    U("PLAN TOGETHER", 3);
    S(":import\r");
    U("Draft JSON path", 3);
    S(draft);
    S("\r");
    U("Policy JSON path", 3);
    S(policy);
    S("\r");
    U("Revision 1 / DRAFT", 3);
    S("VBz");
    U("READY / awaiting approval", 30);
    kt_format(pattern, sizeof(pattern), "%s/hydra-ui-plan.*/compiled-1.json", f.base);
    hf_glob_one(pattern, compiled, sizeof(compiled));
    projection_digest(H("workflow", "plan", "tui-data", compiled), digest, sizeof(digest));
    kt_read(compiled, snapshot, sizeof(snapshot));
    kt_format(evidence, sizeof(evidence), "%s/plan-launch-evidence", f.build);
    kt_mkdir(evidence);
    review_approval(&s, digest);
    kt_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/runs/*", f.home);
    CHECK(!hf_glob_count(pattern), "declined approval creates no run");
    refuse_changed(&s, draft, pattern);
    S("V");
    U("READY / awaiting approval", 30);
    refuse_changed(&s, policy, pattern);
    S("V");
    U("READY / awaiting approval", 30);
    kt_format(pattern, sizeof(pattern), "%s/hydra-ui-plan.*/compiled-3.json", f.base);
    hf_glob_one(pattern, compiled, sizeof(compiled));
    kt_read(compiled, snapshot, sizeof(snapshot));
    projection_digest(H("workflow", "plan", "tui-data", compiled), digest, sizeof(digest));
    /* A launch that ends before creating a run shows the owner's reason and
     * leaves the same revision launchable once the cause is removed. */
    RUN("git", "-C", f.repo, "branch", "plan-smoke");
    S("E");
    U("Execute plan", 3);
    U("revision 3", 3);
    S("y");
    U("Launch of revision 3 failed; nothing ran, E launches it again", 30);
    U("the spawn branch plan-smoke already exists", 3);
    U("LAUNCH FAILED", 3);
    kt_format(path, sizeof(path), "%s/state/v2/projects/*/workflows/runs/*", f.home);
    CHECK(!hf_glob_count(path), "a launch refused by admission creates no run");
    CHECK(!kt_contains(&s, "already submitted"), "a failed launch is not reported as submitted");
    RUN("git", "-C", f.repo, "branch", "-D", "plan-smoke");
    S("E");
    U("Execute plan", 3);
    U("revision 3", 3);
    S("y");
    kt_format(pattern, sizeof(pattern), "%s/state/v2/projects/*/workflows/launches/%s/run-id",
              f.home, digest);
    deadline = kt_now() + 30;
    while (!hf_glob_count(pattern) && kt_now() < deadline)
        kt_pump(&s, .1);
    hf_glob_one(pattern, receipt, sizeof(receipt));
    kt_read(receipt, run_id, sizeof(run_id));
    hf_trim(run_id);
    kt_format(run_dir, sizeof(run_dir), "%s", receipt);
    {
        char *start = strstr(run_dir, "/launches/");
        CHECK(start, "receipt workflow root");
        *start = 0;
        kt_format(path, sizeof(path), "%s/runs/%s", run_dir, run_id);
        kt_format(run_dir, sizeof(run_dir), "%s", path);
    }
    kt_format(expected, sizeof(expected), "Run %s / launch owner starting", run_id);
    U(expected, 10);
    CHECK(!state_is(run_dir, "succeeded") && !state_is(run_dir, "failed"),
          "receipt observed during execution");
    S("E");
    U("This revision was already submitted", 3);
    CHECK(!kt_contains(&s, "Execute plan"), "duplicate launch form refused");
    kt_close(&s, "q", 0, 0);
    CHECK(!kt_exists(compiled), "UI temp snapshot cleaned");
    hf_run(&f, snapshot, -2,
           (const char *[]){f.hydra, "workflow", "plan", "--workspace-owner", digest, NULL});
    deadline = kt_now() + 40;
    while (!state_is(run_dir, "succeeded") && !state_is(run_dir, "failed") && kt_now() < deadline)
        kt_sleep(.2);
    if (!state_is(run_dir, "succeeded"))
        fprintf(stderr, "%s\n", H("workflow", "status", run_id));
    CHECK(state_is(run_dir, "succeeded"), "detached run succeeds");
    kt_format(pattern, sizeof(pattern), "%s/../run_*", run_dir);
    CHECK(hf_glob_count(pattern) == 1, "single run receipt");
    result = json_tokener_parse(H("workflow", "plan", "result", run_id));
    CHECK(result, "result JSON");
    CHECK(!strcmp(json_object_get_string(field(field(result, "data"), "verdict")), "pass"),
          "public result pass");
    CHECK(!strcmp(json_object_get_string(field(field(result, "data"), "plan_sha256")), digest),
          "result digest binding");
    json_object_put(result);
    kt_format(artifact, sizeof(artifact), "%s/steps/compose/attempt-1/artifacts/report", run_dir);
    hf_file(&f, "expected.txt", path, sizeof(path));
    kt_read(path, expected, sizeof(expected));
    CHECK(kt_file_equals(artifact, expected), "exact sealed artifact");
    REFUSE(f.hydra, "workflow", "--workspace-evidence", run_id, "../");
    hf_open(&f, &s);
    U("PLAN TOGETHER", 3);
    kt_format(links, sizeof(links), "%s", H("workflow", "--workspace-links"));
    CHECK(!strncmp(links, "HYDRA_WORKSPACE_LINKS\t1\n", 24) && strlen(links) >= 2 &&
              !strcmp(links + strlen(links) - 2, "Z\n"),
          "workspace links framing");
    {
        char *p = strchr(links, '\n'), *end;
        CHECK(p, "workspace header");
        p++;
        CHECK(!strncmp(p, "P\t", 2), "workspace project row");
        p += 2;
        end = strchr(p, '\t');
        CHECK(end && (size_t)(end - p) < sizeof(project), "project ID");
        memcpy(project, p, (size_t)(end - p));
        project[end - p] = 0;
        CHECK(realpath(f.repo, resolved), "resolved repo");
        sanitize_path(resolved, sanitized, sizeof(sanitized));
        kt_format(expected, sizeof(expected), "P\t%s\t%s\n", project, sanitized);
        CHECK(!strncmp(strchr(links, '\n') + 1, expected, strlen(expected)), "escaped tab path");
        kt_format(expected, sizeof(expected), "L\t%s\t", run_id);
        CHECK(hf_count(links, expected) == 1, "unique run link");
    }
    S("z");
    U("Project: repo line", 3);
    U("plan-fixture · succeeded", 15);
    U("plan-smoke", 3);
    /* Head and run snapshots arrive separately. The imported plan's worker
     * head belongs to its run: move from the head up to the run only after
     * both are visible. */
    U("worker plan-smoke", 15);
    S("k");
    U("Recorded branch reference", 3);
    U("enter open", 3); /* the generated footer: Enter opens the run's evidence */
    CHECK(!kt_contains(&s, "worker plan-smoke"), "a run collapses when its head is no longer selected");
    S("kh");
    kt_pump(&s, 2.5);
    CHECK(!kt_contains(&s, "plan-fixture · succeeded"), "collapsed historical refs");
    S("l");
    U("plan-fixture · succeeded", 3);
    S("j");
    U("enter open", 3); /* the generated footer: Enter opens the run's evidence */
    for (i = 0; i < 3; i++) {
        kt_resize(&s, sizes[i][0], sizes[i][1]);
        kt_pump(&s, .3);
        CHECK(!s.screen.overflow, "navigation resize");
        save(&s, "navigation", sizes[i][0], sizes[i][1]);
    }
    S("\rz");
    U("VERIFIED ARTIFACTS", 15);
    kt_repeat(&s, "j", 200);
    U("VERIFIED: result retrieval", 3);
    save(&s, "verified", 140, 40);
    kt_write(artifact, "tampered\n");
    U("VERIFICATION REFUSED", 15);
    CHECK(strstr(kt_row(&s, 3, row, sizeof(row)), "ARTIFACTS REFUSED"),
          "trusted provenance refuses artifact");
    CHECK(!kt_contains(&s, "VERIFIED: result retrieval"), "stale verified claim removed");
    S("\t\t\tj\t");
    U("Selected step: compose", 15);
    U("compose-output-proof", 3);
    CHECK(kt_contains(&s, "VERIFIED ARTIFACTS from untrusted output"),
          "untrusted output shown separately");
    CHECK(strstr(kt_row(&s, 3, row, sizeof(row)), "ARTIFACTS REFUSED"),
          "untrusted output cannot spoof provenance");
    CHECK(!kt_contains(&s, "Creating worktree for branch"), "selected output provenance");
    for (i = 0; i < 3; i++) {
        kt_resize(&s, sizes[i][0], sizes[i][1]);
        kt_pump(&s, .3);
        CHECK(!s.screen.overflow, "refused artifact resize");
        save(&s, "verification-refused", sizes[i][0], sizes[i][1]);
    }
    kt_close(&s, "q", 0, 0);
    kt_format(old_repo, sizeof(old_repo), "%s", f.repo);
    kt_format(new_repo, sizeof(new_repo), "%s/repo\tline\nbreak", f.base);
    CHECK(!rename(old_repo, new_repo), "move fixture to newline path");
    kt_format(f.repo, sizeof(f.repo), "%s", new_repo);
    kt_format(moved, sizeof(moved), "%s", H("workflow", "--workspace-links"));
    CHECK(realpath(f.repo, resolved), "moved real path");
    sanitize_path(resolved, sanitized, sizeof(sanitized));
    kt_format(expected, sizeof(expected), "P\t%s\t%s\n", project, sanitized);
    CHECK(!strncmp(strchr(moved, '\n') + 1, expected, strlen(expected)), "newline path escaped");
    {
        char *a = strchr(strchr(links, '\n') + 1, '\n'), *b = strchr(strchr(moved, '\n') + 1, '\n');
        CHECK(a && b && !strcmp(a, b), "moving path preserves run links");
    }
    hf_open(&f, &s);
    U("PLAN TOGETHER", 3);
    S("z");
    U("Project: repo line break", 3);
    U("plan-fixture · succeeded", 15);
    kt_close(&s, "q", 0, 0);
    CHECK(!rename(new_repo, old_repo), "restore fixture path");
    kt_format(f.repo, sizeof(f.repo), "%s", old_repo);
    hf_finish(&f);
    puts("PASS plan launch: confirmed exact-revision approval, stale draft/policy refusal, receipt, detached execution, dedup, "
         "artifact/provenance refusal, escaped paths");
    return 0;
}
