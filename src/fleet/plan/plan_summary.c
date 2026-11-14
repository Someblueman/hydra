#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>

/* The readable plan: what runs in what order, the worker's instructions and
 * the commands in full, what may change, the limits in minutes, what counts as
 * done and the text of every script the plan carries. One JSON projection
 * (schema 1, documented in CONTRACTS.md) feeds both the text and the TUI. */

static void add_int(json_object *object, const char *key, int64_t value) { json_object_object_add(object, key, json_object_new_int64(value)); }
static int64_t minutes(int64_t seconds) { return (seconds + 59) / 60; }

static json_object *step_kind(json_object *step, const char **kind) {
    json_object *args = f_field(step, "args");
    const char *value = f_string(step, "kind");
    if (value && !strcmp(value, "spawn")) *kind = "worker";
    else if (f_string(args, "profile")) *kind = "agent";
    else if (f_field(args, "argv")) *kind = "command";
    else *kind = value ? value : "step";
    return args;
}
static void command_line(json_object *argv, char *out, size_t size) {
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < json_object_array_length(argv) && used + 1 < size; i++) {
        const char *arg = f_text(json_object_array_get_idx(argv, i));
        int n = !strncmp(arg, PLAN_INPUT_PREFIX, strlen(PLAN_INPUT_PREFIX)) ?
            snprintf(out + used, size - used, "%s<script %s>", i ? " " : "", arg + strlen(PLAN_INPUT_PREFIX)) :
            snprintf(out + used, size - used, "%s%s", i ? " " : "", arg);
        if (n < 0) break;
        used += (size_t)n;
    }
}
static json_object *step_checks(json_object *plan, const char *step) {
    json_object *requirements = f_field(plan, "requirements"), *checks = f_field(plan, "checks"), *ids = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        if (strcmp(f_string(check, "step") ? f_string(check, "step") : "", step)) continue;
        for (size_t j = 0; j < json_object_array_length(requirements); j++) {
            json_object *requirement = json_object_array_get_idx(requirements, j);
            const char *owner = f_string(requirement, "check");
            if (owner && !strcmp(owner, f_string(check, "id"))) json_object_array_add(ids, json_object_new_string(f_string(requirement, "id")));
        }
    }
    return ids;
}
static void step_text(json_object *row, const char *kind, json_object *args, json_object *steps) {
    char text[1400], command[1024];
    const char *head = f_string(args, "head") ? f_string(args, "head") : f_string(args, "branch");
    int64_t limit = minutes(json_object_get_int64(f_field(args, "timeout")));
    if (!strcmp(kind, "worker"))
        (void)snprintf(text, sizeof(text), "Create the worker branch %s%s", head, f_string(args, "terminal_mode") &&
                       !strcmp(f_string(args, "terminal_mode"), "headless") ? " (no terminal)" : "");
    else if (!strcmp(kind, "agent"))
        (void)snprintf(text, sizeof(text), "The %s agent works on %s, for at most %lld minute%s%s", f_string(args, "profile"), head,
                       (long long)limit, limit == 1 ? "" : "s", plan_spawns(steps, head) ? "; then Hydra commits what it changed" : "");
    else if (!strcmp(kind, "command")) {
        command_line(f_field(args, "argv"), command, sizeof(command));
        (void)snprintf(text, sizeof(text), "Runs %s on %s, for at most %lld minute%s", command, head ? head : "the checkout", (long long)limit, limit == 1 ? "" : "s");
    } else (void)snprintf(text, sizeof(text), "A %s step", kind);
    f_string_add(row, "text", text);
}
static json_object *summary_step(json_object *compiled, json_object *step) {
    json_object *plan = f_field(compiled, "plan"), *row = json_object_new_object(), *args;
    const char *kind, *head;
    args = step_kind(step, &kind);
    head = f_string(args, "head") ? f_string(args, "head") : f_string(args, "branch");
    f_string_add(row, "id", f_string(step, "id")); f_string_add(row, "kind", kind);
    if (head) f_string_add(row, "head", head);
    json_object_object_add(row, "needs", json_object_get(f_field(step, "needs")));
    if (f_field(args, "timeout")) add_int(row, "minutes", minutes(json_object_get_int64(f_field(args, "timeout"))));
    step_text(row, kind, args, f_field(plan, "steps"));
    if (!strcmp(kind, "agent")) {
        f_string_add(row, "profile", f_string(args, "profile"));
        if (f_string(args, "prompt")) f_string_add(row, "instructions", f_string(args, "prompt"));
        else f_string_add(row, "instructions_input", f_string(args, "prompt_input"));
        json_object_object_add(row, "commits", json_object_new_boolean(plan_spawns(f_field(plan, "steps"), head)));
    }
    if (f_field(args, "argv")) json_object_object_add(row, "argv", json_object_get(f_field(args, "argv")));
    json_object_object_add(row, "checks", step_checks(plan, f_string(step, "id")));
    return row;
}
static json_object *summary_steps(json_object *compiled) {
    json_object *steps = f_field(f_field(compiled, "plan"), "steps"), *rows = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(steps); i++) json_object_array_add(rows, summary_step(compiled, json_object_array_get_idx(steps, i)));
    return rows;
}
static json_object *summary_done(json_object *plan) {
    json_object *requirements = f_field(plan, "requirements"), *rows = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(requirements); i++) {
        json_object *requirement = json_object_array_get_idx(requirements, i), *row = json_object_new_object();
        f_string_add(row, "id", f_string(requirement, "id")); f_string_add(row, "criterion", f_string(requirement, "criterion"));
        f_string_add(row, "check", f_string(requirement, "check"));
        json_object_array_add(rows, row);
    }
    return rows;
}
/* Checks whose verdict a plan's own command reports, and how. */
static json_object *summary_checks(json_object *plan) {
    json_object *checks = f_field(plan, "checks"), *rows = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i), *row = json_object_new_object();
        f_string_add(row, "id", f_string(check, "id")); f_string_add(row, "method", f_string(check, "method"));
        f_string_add(row, "step", f_string(check, "step")); f_string_add(row, "definition", f_string(check, "definition"));
        json_object_array_add(rows, row);
    }
    return rows;
}
/* Whether every declared write is a whole head the plan creates. */
static bool spawned_only(json_object *plan) {
    json_object *writes = f_field(f_field(plan, "envelope"), "writes");
    for (size_t i = 0; i < json_object_array_length(writes); i++) {
        const char *scope = f_text(json_object_array_get_idx(writes, i));
        const char *colon = scope ? strchr(scope, ':') : NULL;
        char head[65];
        if (!colon || strcmp(colon, ":*") || (size_t)(colon - scope) >= sizeof(head)) return false;
        memcpy(head, scope, (size_t)(colon - scope)); head[colon - scope] = '\0';
        if (!plan_spawns(f_field(plan, "steps"), head)) return false;
    }
    return true;
}
static json_object *summary_changes(json_object *plan) {
    json_object *out = json_object_new_object(), *branches = json_object_new_array(), *steps = f_field(plan, "steps");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *kind = f_string(step, "kind"), *branch = f_string(f_field(step, "args"), "branch");
        if (kind && !strcmp(kind, "spawn") && branch) json_object_array_add(branches, json_object_new_string(branch));
    }
    json_object_object_add(out, "branches", branches);
    json_object_object_add(out, "writes", json_object_get(f_field(f_field(plan, "envelope"), "writes")));
    json_object_object_add(out, "only_new_branches", json_object_new_boolean(spawned_only(plan)));
    return out;
}
static json_object *summary_limits(json_object *plan, json_object *policy) {
    json_object *env = f_field(plan, "envelope"), *allowed = f_field(policy, "envelope"), *out = json_object_new_object(), *granted = json_object_new_object();
    add_int(out, "minutes", minutes(json_object_get_int64(f_field(env, "timeout_seconds"))));
    add_int(out, "heads", json_object_get_int64(f_field(env, "max_heads")));
    add_int(out, "parallel_steps", json_object_get_int64(f_field(env, "parallelism")));
    add_int(out, "retries", 0); add_int(out, "repairs", json_object_get_int64(f_field(env, "repair_budget")));
    json_object_object_add(out, "tools", json_object_get(f_field(env, "tools")));
    json_object_object_add(granted, "tools", json_object_get(f_field(allowed, "tools")));
    json_object_object_add(granted, "writes", json_object_get(f_field(allowed, "writes")));
    add_int(granted, "minutes", minutes(json_object_get_int64(f_field(allowed, "timeout_seconds"))));
    add_int(granted, "heads", json_object_get_int64(f_field(allowed, "max_heads")));
    json_object_object_add(out, "policy", granted);
    return out;
}
/* Scripts and other files the plan carries, with their text, and repository
 * files it reads. Inline agent instructions appear with their step instead. */
static void summary_inputs(json_object *compiled, json_object *out) {
    json_object *inputs = f_field(f_field(compiled, "data"), "inputs"), *scripts = json_object_new_array(), *files = json_object_new_array();
    json_object_object_foreach(inputs, name, declaration) {
        const char *path = f_string(declaration, "path"), *origin = f_string(declaration, "source");
        json_object *row = json_object_new_object();
        f_string_add(row, "name", name); f_string_add(row, "path", path);
        if (origin && !strcmp(origin, "bundle") && path && !strncmp(path, "prompts/", 8)) { json_object_put(row); continue; }
        if (origin && !strcmp(origin, "bundle") && path && !strncmp(path, "assets/", 7))
            f_string_add(row, "text", f_string(f_field(compiled, "assets"), path + 7));
        json_object_array_add(origin && !strcmp(origin, "bundle") ? scripts : files, row);
    }
    json_object_object_add(out, "scripts", scripts);
    json_object_object_add(out, "repository_files", files);
}

json_object *plan_summary(json_object *compiled) {
    json_object *plan = f_field(compiled, "plan"), *errors = json_object_new_array(), *out = NULL;
    char digest[65];
    if (!f_number_is(compiled, "schema_version", 1) || !f_string(compiled, "compiler") || strcmp(f_string(compiled, "compiler"), PLAN_COMPILER) ||
        plan_validate(plan, f_field(compiled, "policy"), errors) || plan_digest(compiled, digest)) goto done;
    out = json_object_new_object();
    add_int(out, "schema_version", 1);
    f_string_add(out, "plan", f_string(plan, "id")); f_string_add(out, "objective", f_string(plan, "objective"));
    f_string_add(out, "sha256", digest);
    json_object_object_add(out, "questions", json_object_get(f_field(plan, "questions")));
    json_object_object_add(out, "context", json_object_get(f_field(plan, "context")));
    json_object_object_add(out, "steps", summary_steps(compiled));
    json_object_object_add(out, "done_when", summary_done(plan));
    json_object_object_add(out, "checks", summary_checks(plan));
    json_object_object_add(out, "changes", summary_changes(plan));
    json_object_object_add(out, "limits", summary_limits(plan, f_field(compiled, "policy")));
    summary_inputs(compiled, out);
done:
    json_object_put(errors);
    return out;
}

/* Text, one line per call; continuation lines of multi-line text are indented. */
static void block(const char *prefix, const char *text) {
    while (text && *text) {
        size_t length = strcspn(text, "\n");
        printf("%s", prefix);
        for (size_t i = 0; i < length; i++) putchar((unsigned char)text[i] < 32 || text[i] == 127 ? ' ' : text[i]);
        putchar('\n');
        text += length + (text[length] == '\n');
    }
}
static void join(json_object *items, const char *separator) {
    for (size_t i = 0; i < json_object_array_length(items); i++) printf("%s%s", i ? separator : "", f_text(json_object_array_get_idx(items, i)));
}
static void text_steps(json_object *summary) {
    json_object *steps = f_field(summary, "steps");
    puts("\nWhat runs, in order:");
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i), *checks = f_field(step, "checks");
        printf("  %zu. %s: %s\n", i + 1, f_string(step, "id"), f_string(step, "text"));
        if (f_string(step, "instructions")) { puts("     Instructions:"); block("       | ", f_string(step, "instructions")); }
        if (f_string(step, "instructions_input")) printf("     Instructions: the plan's input %s\n", f_string(step, "instructions_input"));
        if (json_object_array_length(checks)) { printf("     Checks: "); join(checks, ", "); putchar('\n'); }
    }
}
static void text_done(json_object *summary) {
    json_object *done = f_field(summary, "done_when"), *checks = f_field(summary, "checks");
    puts("\nDone when:");
    for (size_t i = 0; i < json_object_array_length(done); i++) {
        json_object *row = json_object_array_get_idx(done, i);
        printf("  %s  %s  (checked by %s)\n", f_string(row, "id"), f_string(row, "criterion"), f_string(row, "check"));
    }
    for (size_t i = 0; i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        const char *method = f_string(check, "method");
        if (!strcmp(method, "command")) printf("  Check %s passes when %s; Hydra records that itself.\n", f_string(check, "id"), f_string(check, "definition"));
        else printf("  Check %s (%s, its own report): %s\n", f_string(check, "id"), method, f_string(check, "definition"));
    }
}
static void text_changes(json_object *summary) {
    json_object *changes = f_field(summary, "changes"), *limits = f_field(summary, "limits"), *policy = f_field(limits, "policy");
    if (json_object_get_boolean(f_field(changes, "only_new_branches")) && json_object_array_length(f_field(changes, "branches"))) {
        printf("\nChanges: only %s ", json_object_array_length(f_field(changes, "branches")) == 1 ? "branch" : "branches");
        join(f_field(changes, "branches"), ", ");
        puts(", which this plan creates. Your checkout and other branches are not touched.");
    } else { printf("\nChanges allowed in: "); join(f_field(changes, "writes"), ", "); putchar('\n'); }
    printf("Limits: at most %lld minute%s in total; %lld head%s; %lld step%s at a time; no retries\n",
           (long long)json_object_get_int64(f_field(limits, "minutes")), json_object_get_int64(f_field(limits, "minutes")) == 1 ? "" : "s",
           (long long)json_object_get_int64(f_field(limits, "heads")), json_object_get_int64(f_field(limits, "heads")) == 1 ? "" : "s",
           (long long)json_object_get_int64(f_field(limits, "parallel_steps")), json_object_get_int64(f_field(limits, "parallel_steps")) == 1 ? "" : "s");
    printf("Tools it uses: "); join(f_field(limits, "tools"), ", ");
    printf("\nPolicy (bound to this approval): tools "); join(f_field(policy, "tools"), ", ");
    printf("; at most %lld minutes and %lld heads per plan\n", (long long)json_object_get_int64(f_field(policy, "minutes")),
           (long long)json_object_get_int64(f_field(policy, "heads")));
}
static void text_inputs(json_object *summary) {
    json_object *scripts = f_field(summary, "scripts"), *files = f_field(summary, "repository_files");
    for (size_t i = 0; i < json_object_array_length(scripts); i++) {
        json_object *row = json_object_array_get_idx(scripts, i);
        printf("%sScript %s, carried by this plan and run as written:\n", i ? "" : "\n", f_string(row, "name"));
        block("  | ", f_string(row, "text"));
    }
    if (json_object_array_length(files)) {
        printf("%sRepository files it reads: ", json_object_array_length(scripts) ? "" : "\n");
        for (size_t i = 0; i < json_object_array_length(files); i++) printf("%s%s", i ? ", " : "", f_string(json_object_array_get_idx(files, i), "path"));
        putchar('\n');
    }
}
void plan_summary_text(json_object *summary) {
    json_object *questions = f_field(summary, "questions");
    printf("Plan %s\n", f_string(summary, "plan"));
    block("", f_string(summary, "objective"));
    if (json_object_array_length(questions)) { puts("\nOpen questions (resolve them before approving):"); for (size_t i = 0; i < json_object_array_length(questions); i++) block("  - ", f_text(json_object_array_get_idx(questions, i))); }
    text_steps(summary);
    text_done(summary);
    text_changes(summary);
    text_inputs(summary);
    printf("\nPlan fingerprint %.12s (the approval binds exactly this compiled plan)\n", f_string(summary, "sha256"));
}

/* summary <compiled.json> [--json]: the readable plan. */
json_object *plan_summary_cli(int argc, char **argv) {
    json_object *compiled = argc >= 2 ? plan_read(argv[1]) : NULL, *summary = plan_summary(compiled);
    bool json = argc == 3 && !strcmp(argv[2], "--json");
    json_object_put(compiled);
    if ((argc != 2 && !json) || !summary) {
        json_object_put(summary);
        return plan_failure("workflow plan summary", "invalid_or_stale_plan", "the compiled artifact is not a supported plan compilation", "pass the file workflow plan compile wrote");
    }
    if (json) return f_success("workflow plan summary", summary);
    plan_summary_text(summary);
    json_object_put(summary);
    return NULL;
}
