#include "fleet/review_result.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Presentation of review_result. Section headings are upper-case words at the
 * start of a line and diff lines are indented four spaces, so a terminal can
 * style them without a second protocol. Unknown values are named, never 0. */
struct out {
    void (*emit)(void *context, const char *line);
    void *context;
};

/* Length of an optional array; anything else counts as empty. */
static size_t rr_length(json_object *array)
{
    return json_object_is_type(array, json_type_array) ? json_object_array_length(array) : 0U;
}

static void say(struct out *o, const char *format, ...)
{
    char line[8192];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    o->emit(o->context, line);
}

static const char *text(json_object *object, const char *key, const char *fallback)
{
    const char *value = f_string(object, key);
    return value && *value ? value : fallback;
}

static bool number(json_object *object, const char *key, long long *out)
{
    json_object *value = f_field(object, key);
    if (!json_object_is_type(value, json_type_int))
        return false;
    *out = (long long)json_object_get_int64(value);
    return true;
}

static void count_text(json_object *object, const char *key, char *out, size_t size)
{
    long long n;
    int written;
    if (!number(object, key, &n))
        written = snprintf(out, size, "unknown");
    else if (n >= 1000000)
        written = snprintf(out, size, "%.2fM", (double)n / 1e6);
    else if (n >= 1000)
        written = snprintf(out, size, "%.1fk", (double)n / 1e3);
    else
        written = snprintf(out, size, "%lld", n);
    if (written < 0 || (size_t)written >= size)
        out[0] = 0;
}

static void duration_text(json_object *object, char *out, size_t size)
{
    long long seconds;
    int written;
    if (!number(object, "seconds", &seconds))
        written = snprintf(out, size, "time unknown");
    else if (seconds >= 60)
        written = snprintf(out, size, "%lldm %02llds", seconds / 60, seconds % 60);
    else
        written = snprintf(out, size, "%llds", seconds);
    if (written < 0 || (size_t)written >= size)
        out[0] = 0;
}

static void indented(struct out *o, const char *prefix, const char *body)
{
    const char *line = body;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        int n = (int)(end ? (size_t)(end - line) : strlen(line));
        say(o, "%s%.*s", prefix, n, line);
        line = end ? end + 1 : NULL;
    }
}

static const char *verdict_word(const char *verdict)
{
    if (!strcmp(verdict, "pass"))
        return "PASS (verified)";
    if (!strcmp(verdict, "fail"))
        return "FAILED";
    if (!strcmp(verdict, "not_verified"))
        return "NOT VERIFIED";
    if (!strcmp(verdict, "pending"))
        return "PENDING (run not finished)";
    return "UNKNOWN";
}

static void header(struct out *o, json_object *result)
{
    say(o, "RESULT  %s  -  verdict %s  -  run %s", text(result, "workflow", "unnamed plan"),
        verdict_word(text(result, "verdict", "unknown")), text(result, "run_state", "state unknown"));
    say(o, "Objective: %s", text(result, "objective", "not recorded"));
}

static void deliverables(struct out *o, json_object *rows)
{
    say(o, "");
    say(o, "WHAT WAS PRODUCED");
    if (!rr_length(rows))
        say(o, "  The plan declares no deliverable.");
    for (size_t i = 0; i < rr_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        long long bytes = 0;
        bool sized = number(row, "bytes", &bytes);
        say(o, "  %s - %s (step %s, %s)", text(row, "id", "?"), text(row, "description", "no description"),
            text(row, "step", "?"), text(row, "state", "unavailable"));
        if (sized)
            say(o, "    %lld bytes%s", bytes,
                json_object_get_boolean(f_field(row, "truncated")) ? "; the first 4096 are shown" : "");
        if (f_string(row, "text"))
            indented(o, "    | ", f_string(row, "text"));
    }
}

static const char *requirement_word(const char *state)
{
    if (!strcmp(state, "pass"))
        return "PASS   ";
    if (!strcmp(state, "fail"))
        return "FAIL   ";
    if (!strcmp(state, "pending"))
        return "PENDING";
    if (!strcmp(state, "unverified"))
        return "UNVERIFIED";
    return "NOT REPORTED";
}

static void requirements(struct out *o, json_object *rows)
{
    say(o, "");
    say(o, "WHAT WAS CHECKED");
    if (!rr_length(rows))
        say(o, "  The plan declares no requirement.");
    for (size_t i = 0; i < rr_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        say(o, "  %s %s - %s (check: %s)", requirement_word(text(row, "state", "")), text(row, "id", "?"),
            text(row, "criterion", "no criterion"), text(row, "check", "?"));
    }
}

static void argv_text(json_object *row, char *out, size_t size)
{
    json_object *argv = f_field(row, "argv");
    size_t used = 0;
    out[0] = 0;
    if (!json_object_is_type(argv, json_type_array)) {
        (void)snprintf(out, size, "%s", text(row, "command", "not recorded"));
        return;
    }
    for (size_t i = 0; i < rr_length(argv) && used < size; i++) {
        const char *word = f_text(json_object_array_get_idx(argv, i));
        int written = snprintf(out + used, size - used, "%s%s", i ? " " : "", word ? word : "?");
        if (written < 0)
            break;
        used += (size_t)written;
    }
}

static void marker_line(struct out *o, long long pass, long long fail, long long ok, long long not_ok)
{
    char line[160] = "    Output markers:";
    size_t used = strlen(line);
    const long long counts[] = {pass, fail, ok, not_ok};
    const char *const names[] = {"[PASS]", "[FAIL]", "ok", "not ok"};
    for (size_t i = 0; i < 4; i++) {
        int written;
        if (!counts[i])
            continue;
        written = snprintf(line + used, sizeof(line) - used, "%s %lld %s", used > 20 ? "," : "", counts[i], names[i]);
        if (written < 0 || (size_t)written >= sizeof(line) - used)
            break;
        used += (size_t)written;
    }
    say(o, "%s", line);
}

static void check_output(struct out *o, json_object *row)
{
    json_object *summary = f_field(row, "summary"), *counts = f_field(summary, "counts"), *lines = f_field(summary, "lines");
    long long pass = 0, fail = 0, ok = 0, not_ok = 0, bytes = 0;
    bool any = false;
    any = number(counts, "pass", &pass) || any;
    any = number(counts, "fail", &fail) || any;
    any = number(counts, "ok", &ok) || any;
    any = number(counts, "not_ok", &not_ok) || any;
    if (!f_string(row, "log")) {
        say(o, "    Output: not recorded");
        return;
    }
    if (any)
        marker_line(o, pass, fail, ok, not_ok);
    if (rr_length(lines))
        say(o, "    Summary lines from the output:");
    for (size_t i = 0; i < rr_length(lines); i++)
        say(o, "      %s", f_text(json_object_array_get_idx(lines, i)));
    if (!any && !rr_length(lines))
        say(o, "    No test summary lines were recognised in the output.");
    (void)number(row, "log_bytes", &bytes);
    say(o, "    Full log (%lld bytes): press L - %s", bytes, f_string(row, "log"));
}

static void checks(struct out *o, json_object *rows)
{
    char command[2048];
    say(o, "");
    say(o, "HOW IT WAS VERIFIED");
    for (size_t i = 0; i < rr_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        argv_text(row, command, sizeof(command));
        say(o, "  Check %s (%s): %s", text(row, "id", "?"), text(row, "method", "?"), text(row, "definition", ""));
        say(o, "    Ran: %s%s", command, strstr(command, "@input/") ? "  (@input/NAME is the run's sealed copy of that input)" : "");
        say(o, "    On head %s in step %s, exit code %s", text(row, "head", "unknown"), text(row, "step", "?"),
            text(row, "exit_code", "not recorded"));
        say(o, "    Report: %s%s - %s", text(row, "verdict", "no report"),
            json_object_get_boolean(f_field(row, "verified")) ? " (verified)" : " (not verified)",
            text(row, "evidence", "no evidence text"));
        check_output(o, row);
    }
}

static void agent_line(struct out *o, json_object *agent)
{
    char in[32], cached[32], out[32], cost[48];
    json_object *value = f_field(agent, "cost_usd");
    count_text(agent, "tokens_in", in, sizeof(in));
    count_text(agent, "tokens_cached", cached, sizeof(cached));
    count_text(agent, "tokens_out", out, sizeof(out));
    if (json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int))
        (void)snprintf(cost, sizeof(cost), "cost $%.4f", json_object_get_double(value));
    else
        (void)snprintf(cost, sizeof(cost), "cost not reported");
    say(o, "      agent %s, version %s, model %s, effort %s", text(agent, "profile", "unknown"),
        text(agent, "executable_version", "not recorded"), text(agent, "model", "not recorded"),
        text(agent, "effort", "not recorded"));
    say(o, "      tokens in %s, cached %s, out %s; %s", in, cached, out, cost);
}

static void steps(struct out *o, json_object *rows)
{
    say(o, "");
    say(o, "STEPS  (state, duration, attempts)");
    for (size_t i = 0; i < rr_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        char duration[32];
        duration_text(row, duration, sizeof(duration));
        say(o, "  %-18s %-6s %-10s %-12s attempts %s%s%s", text(row, "id", "?"), text(row, "kind", "?"),
            text(row, "state", "unknown"), duration, text(row, "attempts", "?"), f_string(row, "head") ? ", head " : "",
            text(row, "head", ""));
        if (f_field(row, "agent"))
            agent_line(o, f_field(row, "agent"));
    }
}

static void file_totals(json_object *files, long long *added, long long *deleted)
{
    for (size_t i = 0; i < rr_length(files); i++) {
        json_object *row = json_object_array_get_idx(files, i);
        *added += strtoll(text(row, "added", "0"), NULL, 10);
        *deleted += strtoll(text(row, "deleted", "0"), NULL, 10);
    }
}

static void commits(struct out *o, json_object *rows)
{
    size_t later = 0;
    for (size_t i = 0; i < rr_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        bool after = json_object_get_boolean(f_field(row, "after_step"));
        later += after;
        say(o, "  commit %s  %s  (%s)%s", text(row, "commit", "?"), text(row, "subject", ""), text(row, "author", "?"),
            after ? "  [made after the step finished]" : "");
    }
    if (later)
        say(o, "  Note: %zu commit%s landed on the branch after the step finished.", later, later == 1 ? "" : "s");
}

static void changes(struct out *o, json_object *changes)
{
    json_object *files = f_field(changes, "files"), *rows = f_field(changes, "commits");
    long long added = 0, deleted = 0;
    say(o, "");
    if (strcmp(text(changes, "state", ""), "observed")) {
        say(o, "CHANGES ON %s", text(changes, "branch", "the worker branch"));
        say(o, "  %s", text(changes, "state", "unavailable"));
        return;
    }
    file_totals(files, &added, &deleted);
    say(o, "CHANGES ON %s  (%zu commits, %zu files, +%lld -%lld; the branch as it is now)", text(changes, "branch", "?"),
        rr_length(rows), rr_length(files), added, deleted);
    say(o, "  Base %.12s -> tip %.12s", text(changes, "base", "?"), text(changes, "tip", "?"));
    commits(o, rows);
    if (json_object_get_boolean(f_field(changes, "uncommitted_changes")))
        say(o, "  Note: the worker head has uncommitted changes that are not shown here.");
    for (size_t i = 0; i < rr_length(files); i++) {
        json_object *row = json_object_array_get_idx(files, i);
        say(o, "  file  +%-5s -%-5s %s", text(row, "added", "?"), text(row, "deleted", "?"), text(row, "path", "?"));
    }
    if (!f_string(changes, "diff"))
        return;
    say(o, "");
    say(o, "DIFF  %s", json_object_get_boolean(f_field(changes, "diff_truncated")) ? "(bounded; the first part is shown)"
                                                                               : "(complete)");
    indented(o, "    ", f_string(changes, "diff"));
}

static void next(struct out *o, json_object *next, const char *verdict)
{
    json_object *cleanup = f_field(next, "cleanup");
    say(o, "");
    say(o, "NEXT");
    if (f_string(next, "land")) {
        say(o, "  %s the worker branch yourself; Hydra never merges for you:",
            strcmp(verdict, "pass") ? "When it is ready, land" : "Land");
        say(o, "    %s", f_string(next, "land"));
    }
    if (rr_length(cleanup))
        say(o, "  After landing, remove the plan's heads:");
    for (size_t i = 0; i < rr_length(cleanup); i++)
        say(o, "    %s", f_text(json_object_array_get_idx(cleanup, i)));
    say(o, "  Dismiss: Esc returns to the attention list, where s marks the item seen.");
}

void review_result_text(json_object *result, void (*emit)(void *context, const char *line), void *context)
{
    struct out o = {emit, context};
    header(&o, result);
    deliverables(&o, f_field(result, "deliverables"));
    requirements(&o, f_field(result, "requirements"));
    checks(&o, f_field(result, "checks"));
    steps(&o, f_field(result, "steps"));
    changes(&o, f_field(result, "changes"));
    next(&o, f_field(result, "next"), text(result, "verdict", ""));
    say(&o, "");
}
