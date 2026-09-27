#include "fleet/review_result.h"
#include "fleet/plan/plan.h"
#include "fleet/review_contract.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/task/task.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Every excerpt is bounded independently; the framed review caps the total. */
#define RR_TEXT 4096U
#define RR_LOG (256U * 1024U)
#define RR_DIFF (96U * 1024U)
#define RR_DIFF_LINES 1500U
#define RR_COMMITS 50U
#define RR_FILES 200U
#define RR_SUMMARY 12U

struct rr {
    const char *run, *project;
    json_object *plan, *delivery;
    char repo[F_PATH];
};

/* Length of an optional array; anything else counts as empty. */
static size_t rr_length(json_object *array)
{
    return json_object_is_type(array, json_type_array) ? json_object_array_length(array) : 0U;
}

/* Read at most limit bytes of a regular, non-linked file. Non-text bytes are
 * replaced so that excerpts can be shown in a terminal. */
static char *bounded_text(const char *path, size_t limit, size_t *total)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    struct stat st;
    char *text;
    ssize_t n;
    if (fd < 0)
        return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || !(text = malloc(limit + 1U))) {
        close(fd);
        return NULL;
    }
    n = read(fd, text, limit);
    close(fd);
    if (n < 0) {
        free(text);
        return NULL;
    }
    for (ssize_t i = 0; i < n; i++)
        if (((unsigned char)text[i] < 32U && text[i] != '\n' && text[i] != '\t') || text[i] == 127)
            text[i] = '?';
    text[n] = 0;
    if (total)
        *total = (size_t)st.st_size;
    return text;
}

static void add_int(json_object *out, const char *key, long long number)
{
    json_object_object_add(out, key, json_object_new_int64(number));
}

static bool epoch_at(const char *directory, const char *name, long long *out)
{
    char *text = review_scalar(directory, name), *end = NULL;
    bool ok = text && *text && strspn(text, "0123456789") == strlen(text);
    if (ok)
        *out = strtoll(text, &end, 10);
    free(text);
    return ok && *out > 0;
}

static json_object *plan_step(const struct rr *r, const char *id)
{
    json_object *steps = f_field(r->plan, "steps");
    for (size_t i = 0; id && i < rr_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        if (!strcmp(f_string(step, "id") ? f_string(step, "id") : "", id))
            return step;
    }
    return NULL;
}

/* The exec run that a workflow attempt delegated to, from its recorded
 * output: a JSON exec envelope or the human "Exec run run_ID" heading. */
static bool exec_run_id(const char *attempt, char run[64])
{
    char path[F_PATH], *text, *at;
    size_t n;
    bool ok = false;
    if (f_path(path, sizeof(path), attempt, "stdout") || !(text = bounded_text(path, RR_TEXT, NULL)))
        return false;
    at = strstr(text, "\"run_id\":\"run_");
    at = at ? at + 10 : strstr(text, "Exec run run_");
    if (at && !strncmp(at, "Exec run ", 9))
        at += 9;
    if (at) {
        n = strspn(at + 4, "0123456789abcdef") + 4U;
        ok = n > 4U && n < 64U && snprintf(run, 64, "%.*s", (int)n, at) < 64;
    }
    free(text);
    return ok;
}

static bool exec_head_dir(const char *project, const char *run, char out[F_PATH])
{
    char base[F_PATH];
    DIR *dir;
    struct dirent *entry;
    bool found = false;
    if (snprintf(base, sizeof(base), "%s/exec/%s", project, run) >= (int)sizeof(base) || !(dir = opendir(base)))
        return false;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "head_", 5) || !f_name(entry->d_name))
            continue;
        if (found || f_path(out, F_PATH, base, entry->d_name)) {
            found = false;
            break;
        }
        found = true;
    }
    closedir(dir);
    return found;
}

static void copy_count(json_object *out, const char *key, json_object *usage, const char *field)
{
    json_object *value = f_field(usage, field);
    json_object_object_add(out, key,
                           json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0
                               ? json_object_new_int64(json_object_get_int64(value))
                               : NULL);
}

static void copy_text_field(json_object *out, const char *key, const char *value)
{
    json_object_object_add(out, key, value && *value ? json_object_new_string(value) : NULL);
}

json_object *review_attempt_agent(const char *project, const char *attempt, char exec_log[F_PATH])
{
    char run[64], head[F_PATH], path[F_PATH];
    json_object *record, *out, *usage, *cost;
    exec_log[0] = 0;
    if (!exec_run_id(attempt, run) || !exec_head_dir(project, run, head))
        return NULL;
    if (!f_path(path, sizeof(path), head, "stdout"))
        (void)f_copy(exec_log, F_PATH, path);
    if (f_path(path, sizeof(path), head, "agent.json") || !(record = f_read_json(path, F_LIMIT)))
        return NULL;
    out = json_object_new_object();
    usage = f_field(record, "usage");
    copy_text_field(out, "profile", f_string(record, "profile"));
    copy_text_field(out, "executable_version", f_string(f_field(record, "probe"), "executable_version"));
    copy_text_field(out, "model", f_string(record, "model"));
    copy_text_field(out, "effort", f_string(record, "effort"));
    copy_text_field(out, "state", f_string(record, "state"));
    copy_count(out, "tokens_in", usage, "input_tokens");
    copy_count(out, "tokens_cached", usage, "cached_input_tokens");
    copy_count(out, "tokens_out", usage, "output_tokens");
    cost = f_field(usage, "cost_usd");
    json_object_object_add(out, "cost_usd",
                           (json_object_is_type(cost, json_type_double) || json_object_is_type(cost, json_type_int)) &&
                                   isfinite(json_object_get_double(cost)) && json_object_get_double(cost) >= 0
                               ? json_object_new_double(json_object_get_double(cost))
                               : NULL);
    json_object_put(record);
    return out;
}

static bool attempt_dir(const struct rr *r, const char *step, char out[F_PATH])
{
    return step && !plan_attempt_directory(r->run, step, out);
}

static void step_timing(json_object *row, const char *run, const char *id, const char *attempt)
{
    char directory[F_PATH];
    long long started, completed;
    if (snprintf(directory, sizeof(directory), "%s/steps/%s", run, id) >= (int)sizeof(directory))
        return;
    {
        char *state = review_scalar(directory, "state"), *attempts = review_scalar(directory, "attempts");
        copy_text_field(row, "state", state);
        copy_text_field(row, "attempts", attempts);
        free(state);
        free(attempts);
    }
    if (attempt && epoch_at(directory, "started-at", &started) && epoch_at(attempt, "completed-at", &completed) &&
        completed >= started) {
        add_int(row, "seconds", completed - started);
        add_int(row, "completed_at", completed);
    }
}

static json_object *step_row(const struct rr *r, json_object *step)
{
    const char *id = f_string(step, "id");
    json_object *row = json_object_new_object(), *args = f_field(step, "args"), *agent;
    char attempt[F_PATH], log[F_PATH];
    bool has_attempt = attempt_dir(r, id, attempt);
    copy_text_field(row, "id", id);
    copy_text_field(row, "kind", f_string(step, "kind"));
    copy_text_field(row, "role", f_string(step, "role"));
    copy_text_field(row, "head", f_string(args, "head") ? f_string(args, "head") : f_string(args, "branch"));
    step_timing(row, r->run, id, has_attempt ? attempt : NULL);
    if (has_attempt && f_string(args, "profile") && (agent = review_attempt_agent(r->project, attempt, log)))
        json_object_object_add(row, "agent", agent);
    return row;
}

static json_object *review_steps(const struct rr *r)
{
    json_object *rows = json_object_new_array(), *steps = f_field(r->plan, "steps");
    for (size_t i = 0; i < rr_length(steps); i++)
        json_object_array_add(rows, step_row(r, json_object_array_get_idx(steps, i)));
    return rows;
}

static json_object *deliverable_row(const struct rr *r, json_object *d)
{
    json_object *row = json_object_new_object(), *sealed = f_field(f_field(r->delivery, "deliverables"), f_string(d, "id"));
    char attempt[F_PATH], path[F_PATH];
    size_t total = 0;
    char *text = NULL;
    copy_text_field(row, "id", f_string(d, "id"));
    copy_text_field(row, "description", f_string(d, "description"));
    copy_text_field(row, "step", f_string(d, "step"));
    copy_text_field(row, "output", f_string(d, "output"));
    if (attempt_dir(r, f_string(d, "step"), attempt) && plan_id(f_string(d, "output")) &&
        snprintf(path, sizeof(path), "%s/artifacts/%s", attempt, f_string(d, "output")) < (int)sizeof(path))
        text = bounded_text(path, RR_TEXT, &total);
    f_string_add(row, "state", sealed ? "verified" : text ? "recorded_unverified" : "unavailable");
    if (text) {
        f_string_add(row, "text", text);
        add_int(row, "bytes", (long long)total);
        json_object_object_add(row, "truncated", json_object_new_boolean(total > RR_TEXT));
    }
    free(text);
    return row;
}

static json_object *review_deliverables(const struct rr *r)
{
    json_object *rows = json_object_new_array(), *items = f_field(r->plan, "deliverables");
    for (size_t i = 0; i < rr_length(items); i++)
        json_object_array_add(rows, deliverable_row(r, json_object_array_get_idx(items, i)));
    return rows;
}

/* An unverified report is read only to explain a failure or pending state;
 * it never contributes a pass. */
static json_object *check_report(const struct rr *r, json_object *check, bool *verified)
{
    json_object *report = f_field(f_field(r->delivery, "checks"), f_string(check, "id"));
    char attempt[F_PATH], path[F_PATH];
    *verified = report != NULL;
    if (report)
        return json_object_get(report);
    if (!attempt_dir(r, f_string(check, "step"), attempt) || !plan_id(f_string(check, "report")) ||
        snprintf(path, sizeof(path), "%s/artifacts/%s", attempt, f_string(check, "report")) >= (int)sizeof(path))
        return NULL;
    return f_read_json(path, 64U * 1024U);
}

static bool listed(json_object *array, const char *value)
{
    for (size_t i = 0; value && i < rr_length(array); i++)
        if (f_text(json_object_array_get_idx(array, i)) && !strcmp(f_text(json_object_array_get_idx(array, i)), value))
            return true;
    return false;
}

static const char *requirement_state(const struct rr *r, json_object *requirement)
{
    json_object *checks = f_field(r->plan, "checks"), *report = NULL;
    const char *state = "not_reported";
    bool verified = false;
    for (size_t i = 0; i < rr_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (!f_string(check, "id") || !f_string(requirement, "check") ||
            strcmp(f_string(check, "id"), f_string(requirement, "check")))
            continue;
        report = check_report(r, check, &verified);
        break;
    }
    if (!report)
        return r->delivery ? "not_reported" : "pending";
    if (verified)
        state = listed(f_field(report, "requirements"), f_string(requirement, "id")) ? "pass" : "not_reported";
    else if (f_string(report, "verdict") && !strcmp(f_string(report, "verdict"), "fail"))
        state = "fail";
    else
        state = "unverified";
    json_object_put(report);
    return state;
}

static json_object *review_requirements(const struct rr *r)
{
    json_object *rows = json_object_new_array(), *items = f_field(r->plan, "requirements");
    for (size_t i = 0; i < rr_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *row = json_object_new_object();
        copy_text_field(row, "id", f_string(item, "id"));
        copy_text_field(row, "criterion", f_string(item, "criterion"));
        copy_text_field(row, "check", f_string(item, "check"));
        f_string_add(row, "state", requirement_state(r, item));
        json_object_array_add(rows, row);
    }
    return rows;
}

static bool summary_line(const char *line)
{
    static const char *const words[] = {"passed", "failed", "total", "tests", "summary", "error", "not ok",
                                        "# pass", "# fail", "ran ", "skipped", "fail:", "pass:"};
    char lower[256];
    size_t n = strlen(line), i;
    if (!n || n >= sizeof(lower))
        return false;
    for (i = 0; i <= n; i++)
        lower[i] = (char)(line[i] >= 'A' && line[i] <= 'Z' ? line[i] + 32 : line[i]);
    if (strstr(lower, "[pass]") || !strncmp(lower, "ok ", 3))
        return false;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (strstr(lower, words[i]))
            return true;
    return strstr(lower, "[fail]") != NULL;
}

static void count_marker(json_object *counts, const char *line)
{
    const char *key = strstr(line, "[PASS]") ? "pass" : strstr(line, "[FAIL]") ? "fail"
                      : !strncmp(line, "not ok ", 7)                        ? "not_ok"
                      : !strncmp(line, "ok ", 3)                            ? "ok"
                                                                            : NULL;
    if (key)
        add_int(counts, key, json_object_get_int64(f_field(counts, key)) + 1);
}

/* A bounded, filtered view of a check's output: marker counts plus lines that
 * look like test summaries or failures. The complete log stays referenced. */
static json_object *log_summary(const char *text)
{
    json_object *out = json_object_new_object(), *counts = json_object_new_object(), *lines = json_object_new_array();
    const char *line = text;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        char copy[256];
        if (n < sizeof(copy)) {
            memcpy(copy, line, n);
            copy[n] = 0;
            count_marker(counts, copy);
            if (summary_line(copy) && rr_length(lines) < RR_SUMMARY)
                json_object_array_add(lines, json_object_new_string(copy));
        }
        line = end ? end + 1 : NULL;
    }
    json_object_object_add(out, "counts", counts);
    json_object_object_add(out, "lines", lines);
    return out;
}

static void check_command(json_object *row, json_object *step, const char *attempt)
{
    json_object *args = f_field(step, "args"), *argv = f_field(args, "argv");
    char *exit_code = attempt ? review_scalar(attempt, "exit-code") : NULL;
    copy_text_field(row, "head", f_string(args, "head"));
    if (json_object_is_type(argv, json_type_array))
        json_object_object_add(row, "argv", json_object_get(argv));
    copy_text_field(row, "command", f_string(args, "command"));
    copy_text_field(row, "exit_code", exit_code);
    free(exit_code);
}

static void check_log(json_object *row, const struct rr *r, const char *attempt)
{
    char log[F_PATH], path[F_PATH];
    size_t total = 0;
    char *text;
    json_object *agent = review_attempt_agent(r->project, attempt, log);
    json_object_put(agent);
    if (!log[0] && !f_path(path, sizeof(path), attempt, "stdout"))
        (void)f_copy(log, sizeof(log), path);
    if (!log[0] || !(text = bounded_text(log, RR_LOG, &total)))
        return;
    f_string_add(row, "log", log);
    add_int(row, "log_bytes", (long long)total);
    json_object_object_add(row, "summary", log_summary(text));
    free(text);
}

static json_object *check_row(const struct rr *r, json_object *check)
{
    json_object *row = json_object_new_object(), *step = plan_step(r, f_string(check, "step")), *report;
    char attempt[F_PATH];
    bool verified = false, has_attempt = attempt_dir(r, f_string(check, "step"), attempt);
    copy_text_field(row, "id", f_string(check, "id"));
    copy_text_field(row, "method", f_string(check, "method"));
    copy_text_field(row, "definition", f_string(check, "definition"));
    copy_text_field(row, "step", f_string(check, "step"));
    check_command(row, step, has_attempt ? attempt : NULL);
    report = check_report(r, check, &verified);
    copy_text_field(row, "verdict", f_string(report, "verdict"));
    copy_text_field(row, "evidence", f_string(report, "evidence"));
    json_object_object_add(row, "verified", json_object_new_boolean(verified));
    json_object_put(report);
    if (has_attempt)
        check_log(row, r, attempt);
    return row;
}

static json_object *review_checks_detail(const struct rr *r)
{
    json_object *rows = json_object_new_array(), *items = f_field(r->plan, "checks");
    for (size_t i = 0; i < rr_length(items); i++)
        json_object_array_add(rows, check_row(r, json_object_array_get_idx(items, i)));
    return rows;
}

static char *git_text(const char *repo, char *const args[], size_t limit)
{
    char *argv[16];
    size_t n = 0;
    struct f_capture capture = {0};
    char *text = NULL;
    argv[n++] = "git";
    argv[n++] = "-C";
    argv[n++] = (char *)repo;
    argv[n++] = "-c";
    argv[n++] = "core.quotepath=off";
    while (*args && n < 15)
        argv[n++] = *args++;
    argv[n] = NULL;
    if (review_git_environment_clean() && !f_run(argv, NULL, 0, 30, &capture) && !capture.status && capture.out) {
        if (capture.out_bytes > limit)
            capture.out[limit] = 0;
        text = capture.out;
        capture.out = NULL;
    }
    f_capture_free(&capture);
    return text;
}

/* Heads are matched by their recorded branch; the base is the commit the head
 * was created from. Both come from state v2, never from the caller. */
static bool head_for_branch(const char *project, const char *branch, char head[F_PATH])
{
    char heads[F_PATH];
    DIR *dir;
    struct dirent *entry;
    bool found = false;
    if (!branch || f_path(heads, sizeof(heads), project, "heads") || !(dir = opendir(heads)))
        return false;
    while (!found && (entry = readdir(dir))) {
        char *value;
        if (strncmp(entry->d_name, "head_", 5) || f_path(head, F_PATH, heads, entry->d_name))
            continue;
        value = review_scalar(head, "branch");
        found = value && !strcmp(value, branch);
        free(value);
    }
    closedir(dir);
    return found;
}

static json_object *commit_rows(const char *repo, const char *range, long long finished)
{
    char *args[] = {"log", "--no-color", "--format=%h%x09%ct%x09%an%x09%s", "-n", "51", (char *)range, NULL};
    char *text = git_text(repo, args, 64U * 1024U), *line, *save = NULL;
    json_object *rows = json_object_new_array();
    for (line = text ? strtok_r(text, "\n", &save) : NULL; line && rr_length(rows) < RR_COMMITS;
         line = strtok_r(NULL, "\n", &save)) {
        char *fields[4], *cursor = line;
        json_object *row;
        size_t i;
        for (i = 0; i < 4 && cursor; i++) {
            fields[i] = cursor;
            cursor = i < 3 ? strchr(cursor, '\t') : NULL;
            if (cursor)
                *cursor++ = 0;
        }
        if (i < 4)
            continue;
        row = json_object_new_object();
        f_string_add(row, "commit", fields[0]);
        f_string_add(row, "author", fields[2]);
        f_string_add(row, "subject", fields[3]);
        add_int(row, "time", strtoll(fields[1], NULL, 10));
        json_object_object_add(row, "after_step",
                               json_object_new_boolean(finished > 0 && strtoll(fields[1], NULL, 10) > finished));
        json_object_array_add(rows, row);
    }
    free(text);
    return rows;
}

static json_object *file_rows(const char *repo, const char *range)
{
    char *args[] = {"diff", "--no-color", "--no-ext-diff", "--numstat", (char *)range, NULL};
    char *text = git_text(repo, args, 64U * 1024U), *line, *save = NULL;
    json_object *rows = json_object_new_array();
    for (line = text ? strtok_r(text, "\n", &save) : NULL; line && rr_length(rows) < RR_FILES;
         line = strtok_r(NULL, "\n", &save)) {
        char *first = strchr(line, '\t'), *second = first ? strchr(first + 1, '\t') : NULL;
        json_object *row;
        if (!second)
            continue;
        *first = 0;
        *second = 0;
        row = json_object_new_object();
        f_string_add(row, "path", second + 1);
        f_string_add(row, "added", line);
        f_string_add(row, "deleted", first + 1);
        json_object_array_add(rows, row);
    }
    free(text);
    return rows;
}

static void diff_text(json_object *out, const char *repo, const char *range)
{
    char *args[] = {"diff", "--no-color", "--no-ext-diff", (char *)range, NULL};
    char *text = git_text(repo, args, RR_DIFF + 1U), *cursor = text;
    size_t lines = 0;
    bool truncated = text && strlen(text) > RR_DIFF;
    if (!text)
        return;
    while (cursor && (cursor = strchr(cursor, '\n')) && ++lines < RR_DIFF_LINES)
        cursor++;
    if (cursor && lines >= RR_DIFF_LINES) {
        cursor[1] = 0;
        truncated = true;
    }
    if (strlen(text) > RR_DIFF)
        text[RR_DIFF] = 0;
    f_string_add(out, "diff", text);
    json_object_object_add(out, "diff_truncated", json_object_new_boolean(truncated));
    free(text);
}

static void worktree_state(json_object *out, const char *head)
{
    char *worktree = review_scalar(head, "worktree"), *text = NULL;
    char *args[] = {"status", "--porcelain", "--untracked-files=normal", NULL};
    struct stat st;
    if (worktree && !lstat(worktree, &st) && S_ISDIR(st.st_mode))
        text = git_text(worktree, args, 64U * 1024U);
    json_object_object_add(out, "uncommitted_changes", text ? json_object_new_boolean(*text != 0) : NULL);
    free(text);
    free(worktree);
}

static const char *changes_branch(const struct rr *r)
{
    json_object *items = f_field(r->plan, "deliverables");
    json_object *first = rr_length(items) ? json_object_array_get_idx(items, 0) : NULL;
    json_object *step = plan_step(r, f_string(first, "step"));
    return f_string(f_field(step, "args"), "head");
}

static long long step_finished(const struct rr *r)
{
    json_object *items = f_field(r->plan, "deliverables");
    json_object *first = rr_length(items) ? json_object_array_get_idx(items, 0) : NULL;
    char attempt[F_PATH];
    long long at = 0;
    if (attempt_dir(r, f_string(first, "step"), attempt) && !epoch_at(attempt, "completed-at", &at))
        at = 0;
    return at;
}

static json_object *review_changes(const struct rr *r)
{
    json_object *out = json_object_new_object();
    const char *branch = changes_branch(r);
    char head[F_PATH], range[256], ref[160], *base = NULL, *tip = NULL;
    char *args[] = {"rev-parse", "--verify", "--quiet", ref, NULL};
    copy_text_field(out, "branch", branch);
    if (!branch || !plan_id(branch) || !head_for_branch(r->project, branch, head) || !r->repo[0]) {
        f_string_add(out, "state", "unavailable: the worker head is no longer recorded");
        return out;
    }
    base = review_scalar(head, "base-ref");
    if (snprintf(ref, sizeof(ref), "refs/heads/%s^{commit}", branch) >= (int)sizeof(ref) ||
        !(task_hex(base, 40) || task_hex(base, 64)) || !(tip = git_text(r->repo, args, 128)) ||
        snprintf(range, sizeof(range), "%s..refs/heads/%s", base, branch) >= (int)sizeof(range)) {
        f_string_add(out, "state", "unavailable: the worker branch or its base commit is missing");
        free(base);
        free(tip);
        return out;
    }
    tip[strcspn(tip, "\r\n")] = 0;
    f_string_add(out, "state", "observed");
    f_string_add(out, "base", base);
    f_string_add(out, "tip", tip);
    json_object_object_add(out, "commits", commit_rows(r->repo, range, step_finished(r)));
    json_object_object_add(out, "files", file_rows(r->repo, range));
    diff_text(out, r->repo, range);
    worktree_state(out, head);
    free(base);
    free(tip);
    return out;
}

static char *shell_word(const char *text)
{
    size_t n = strlen(text), used = 1;
    char *out = malloc(n * 4U + 3U);
    if (!out)
        return NULL;
    out[0] = '\'';
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '\'') {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        } else
            out[used++] = text[i];
    }
    out[used++] = '\'';
    out[used] = 0;
    return out;
}

/* Hydra never merges on the reviewer's behalf; it only names the commands. */
static json_object *review_next(const struct rr *r, json_object *steps)
{
    json_object *out = json_object_new_object(), *cleanup = json_object_new_array();
    const char *branch = changes_branch(r);
    char line[F_PATH * 2];
    char *repo = r->repo[0] ? shell_word(r->repo) : NULL;
    if (branch && repo && snprintf(line, sizeof(line), "cd %s && git merge --no-ff %s", repo, branch) < (int)sizeof(line))
        f_string_add(out, "land", line);
    for (size_t i = 0; i < rr_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *kind = f_string(step, "kind"), *head = f_string(step, "head");
        if (kind && head && !strcmp(kind, "spawn") && plan_id(head) &&
            snprintf(line, sizeof(line), "hydra kill %s", head) < (int)sizeof(line))
            json_object_array_add(cleanup, json_object_new_string(line));
    }
    json_object_object_add(out, "cleanup", cleanup);
    free(repo);
    return out;
}

static const char *review_verdict(const struct rr *r, const char *state)
{
    if (r->delivery)
        return "pass";
    if (!state)
        return "unknown";
    if (!strcmp(state, "succeeded"))
        return "not_verified";
    if (!strcmp(state, "failed") || !strcmp(state, "cancelled"))
        return "fail";
    return "pending";
}

json_object *review_result(const char *run, const char *project, json_object *delivery)
{
    char path[F_PATH];
    json_object *compiled, *out, *steps;
    struct rr r = {run, project, NULL, delivery, ""};
    char *state, *repo;
    if (f_path(path, sizeof(path), run, "compiled.json") || !(compiled = plan_read(path)))
        return NULL;
    r.plan = f_field(compiled, "plan");
    repo = review_scalar(project, "repo-root");
    if (repo)
        (void)f_copy(r.repo, sizeof(r.repo), repo);
    free(repo);
    out = json_object_new_object();
    state = review_scalar(run, "state");
    copy_text_field(out, "workflow", f_string(r.plan, "id"));
    copy_text_field(out, "objective", f_string(r.plan, "objective"));
    copy_text_field(out, "run_state", state);
    f_string_add(out, "verdict", review_verdict(&r, state));
    json_object_object_add(out, "deliverables", review_deliverables(&r));
    json_object_object_add(out, "requirements", review_requirements(&r));
    json_object_object_add(out, "checks", review_checks_detail(&r));
    steps = review_steps(&r);
    json_object_object_add(out, "steps", steps);
    json_object_object_add(out, "changes", review_changes(&r));
    json_object_object_add(out, "next", review_next(&r, steps));
    free(state);
    json_object_put(compiled);
    return out;
}
