#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include <stdlib.h>
#include <string.h>

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
