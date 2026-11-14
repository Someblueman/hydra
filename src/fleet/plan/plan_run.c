#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Facts the run driver needs about a compiled plan's steps, and the commit a
 * worker head reached. Only already admitted artifacts are read here. */

static const char *step_head(json_object *step) { return f_string(f_field(step, "args"), "head"); }
static bool kind_is(json_object *step, const char *kind) {
    const char *value = f_string(step, "kind");
    return value && !strcmp(value, kind);
}
static bool agent_step(json_object *step) { return kind_is(step, "exec") && f_string(f_field(step, "args"), "profile"); }
static bool argv_step(json_object *step) { return kind_is(step, "exec") && f_field(f_field(step, "args"), "argv"); }

/* A head is the plan's own when one of its spawn steps creates it. */
bool plan_spawns(json_object *steps, const char *head) {
    if (!head) return false;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *branch = f_string(f_field(step, "args"), "branch");
        if (kind_is(step, "spawn") && branch && !strcmp(branch, head)) return true;
    }
    return false;
}

const char *plan_command_check(json_object *plan, const char *step) {
    json_object *checks = f_field(plan, "checks");
    for (size_t i = 0; step && i < json_object_array_length(checks); i++) {
        json_object *check = json_object_array_get_idx(checks, i);
        const char *owner = f_string(check, "step"), *method = f_string(check, "method");
        if (owner && method && !strcmp(owner, step) && !strcmp(method, "command")) return f_string(check, "id");
    }
    return NULL;
}

/* One line per exec step on a head the plan spawns: "step<TAB>agent<TAB>head"
 * for agent steps, whose changes Hydra commits afterwards;
 * "step<TAB>command<TAB>head<TAB>check" for a step whose report Hydra writes;
 * "step<TAB>run<TAB>head" for any other command. */
int plan_hooks(json_object *compiled, FILE *out) {
    json_object *plan = f_field(compiled, "plan"), *steps = f_field(plan, "steps");
    if (!plan_list(steps, 1, PLAN_STEPS)) return -1;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *id = f_string(step, "id"), *head = step_head(step), *check;
        if (!plan_id(id) || !plan_id(head) || !plan_spawns(steps, head)) continue;
        if (agent_step(step)) fprintf(out, "%s\tagent\t%s\n", id, head);
        else if (argv_step(step) && (check = plan_command_check(plan, id))) fprintf(out, "%s\tcommand\t%s\t%s\n", id, head, check);
        else if (argv_step(step)) fprintf(out, "%s\trun\t%s\n", id, head);
    }
    return ferror(out) ? -1 : 0;
}

/* Whether step a needs step b, directly or through other steps. */
static bool needs_step(json_object *steps, int a, int b, unsigned depth) {
    json_object *needs = f_field(json_object_array_get_idx(steps, (size_t)a), "needs");
    if (depth > PLAN_STEPS) return false;
    for (size_t i = 0; i < json_object_array_length(needs); i++) {
        int index = plan_index(steps, f_text(json_object_array_get_idx(needs, i)));
        if (index == b || (index >= 0 && needs_step(steps, index, b, depth + 1))) return true;
    }
    return false;
}

/* A commit a succeeded step recorded in its authoritative attempt. */
static bool recorded_commit(const char *run, const char *step, const char *name, char commit[65]) {
    char directory[F_PATH], path[F_PATH], *text = NULL;
    bool found = false;
    if (!wd_producer_directory(run, step, directory) && !f_path(path, sizeof(path), directory, name) &&
        (text = f_read(path, 128))) {
        text[strcspn(text, "\r\n")] = '\0';
        found = (task_hex(text, 40) || task_hex(text, 64)) && !f_copy(commit, 65, text);
    }
    free(text);
    return found;
}

/* Every command that ran after the agent step on the same head must have
 * started and ended on that agent step's commit. */
static bool commands_on(const char *run, json_object *steps, int agent, const char *branch, const char *commit) {
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *head = step_head(step), *id = f_string(step, "id");
        char before[65], after[65];
        if (!argv_step(step) || !head || strcmp(head, branch) || !needs_step(steps, (int)i, agent, 0)) continue;
        if (!recorded_commit(run, id, "head-commit", before)) continue;
        if (strcmp(before, commit) || !recorded_commit(run, id, "head-commit-after", after) || strcmp(after, commit)) return false;
    }
    return true;
}

int plan_head_commit(const char *run, json_object *plan, const char *branch, char commit[65]) {
    json_object *steps = f_field(plan, "steps");
    int agent = -1;
    commit[0] = '\0';
    if (!branch || !plan_list(steps, 1, PLAN_STEPS)) return -1;
    for (size_t i = 0; i < json_object_array_length(steps); i++) {
        json_object *step = json_object_array_get_idx(steps, i);
        const char *head = step_head(step);
        char value[65];
        if (!agent_step(step) || !head || strcmp(head, branch)) continue;
        if (recorded_commit(run, f_string(step, "id"), "commit", value)) { (void)f_copy(commit, 65, value); agent = (int)i; }
    }
    if (agent < 0) return -1;
    return commands_on(run, steps, agent, branch, commit) ? 0 : -2;
}

static json_object *check_record(json_object *plan, const char *id) {
    int index = plan_index(f_field(plan, "checks"), id);
    return index < 0 ? NULL : json_object_array_get_idx(f_field(plan, "checks"), (size_t)index);
}
static json_object *owned_requirements(json_object *plan, const char *check) {
    json_object *requirements = f_field(plan, "requirements"), *ids = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(requirements); i++) {
        json_object *requirement = json_object_array_get_idx(requirements, i);
        const char *owner = f_string(requirement, "check");
        if (owner && !strcmp(owner, check)) json_object_array_add(ids, json_object_new_string(f_string(requirement, "id")));
    }
    return ids;
}
/* The command text for the evidence line, bounded. */
static void command_text(json_object *step, char *out, size_t size) {
    json_object *argv = f_field(f_field(step, "args"), "argv");
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < json_object_array_length(argv) && used + 1 < size; i++) {
        int n = snprintf(out + used, size - used, "%s%s", i ? " " : "", f_text(json_object_array_get_idx(argv, i)));
        if (n < 0) break;
        used += (size_t)n;
    }
}
static bool recorded_equals(const char *attempt, const char *name, const char *commit) {
    char path[F_PATH], *value = f_path(path, sizeof(path), attempt, name) ? NULL : f_read(path, 128);
    bool equal = false;
    if (value) {
        value[strcspn(value, "\r\n")] = '\0';
        equal = !strcmp(value, commit);
    }
    free(value);
    return equal;
}
/* The commit the subject names, when the step started and ended on it. */
static bool subject_commit(const char *attempt, const char *input, char commit[65]) {
    char path[F_PATH], *text = NULL;
    bool bound;
    if (snprintf(path, sizeof(path), "%s/inputs/%s", attempt, input) >= (int)sizeof(path) || !(text = f_read(path, 128))) return false;
    text[strcspn(text, "\r\n")] = '\0';
    bound = (task_hex(text, 40) || task_hex(text, 64)) && !f_copy(commit, 65, text);
    free(text);
    return bound && recorded_equals(attempt, "head-commit", commit) && recorded_equals(attempt, "head-commit-after", commit);
}
static json_object *command_evidence(json_object *plan, json_object *check, json_object *step, const char *commit, const char *subject) {
    json_object *report = json_object_new_object();
    char command[1024], evidence[1200];
    command_text(step, command, sizeof(command));
    (void)snprintf(evidence, sizeof(evidence), "%s exited 0 on commit %s", command, commit);
    json_object_object_add(report, "schema_version", json_object_new_int(1));
    f_string_add(report, "verdict", "pass");
    f_string_add(report, "subject_sha256", subject);
    json_object_object_add(report, "requirements", owned_requirements(plan, f_string(check, "id")));
    f_string_add(report, "evidence", evidence);
    return report;
}
static int write_report(json_object *report, const char *path) {
    const char *text = json_object_to_json_string_ext(report, JSON_C_TO_STRING_PLAIN);
    return f_write(path, text, strlen(text), true);
}

/* Called only after the step's command exited 0. The report path is removed
 * first, so whatever the command wrote there never stands as the report. */
int plan_command_report(const char *run, const char *step, const char *attempt) {
    json_object *compiled = plan_run_definition(run), *plan = f_field(compiled, "plan"), *check, *declaration, *report = NULL;
    char path[F_PATH], input[F_PATH], commit[65], subject[65];
    const char *id = plan_command_check(plan, step), *file, *name;
    int status = -1, index = plan_index(f_field(plan, "steps"), step);
    check = id ? check_record(plan, id) : NULL;
    name = f_string(check, "input");
    declaration = f_field(f_field(f_field(f_field(f_field(compiled, "data"), "steps"), step), "outputs"), f_string(check, "report"));
    file = f_string(declaration, "path");
    if (!check || index < 0 || !file || !task_path(file) || snprintf(path, sizeof(path), "%s/outputs/%s", attempt, file) >= (int)sizeof(path)) goto done;
    if (unlink(path) && errno != ENOENT) goto done;
    if (!plan_id(name) || !subject_commit(attempt, name, commit) ||
        snprintf(input, sizeof(input), "%s/inputs/%s", attempt, name) >= (int)sizeof(input) || f_hash(input, subject)) goto done;
    report = command_evidence(plan, check, json_object_array_get_idx(f_field(plan, "steps"), (size_t)index), commit, subject);
    status = write_report(report, path);
done:
    json_object_put(report); json_object_put(compiled);
    return status;
}
