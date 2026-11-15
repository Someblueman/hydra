#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_drive.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>

/* graph.tsv is fixed for the life of a run and read once. Step states are
 * not: workers, approvals and other shell writers change them, so every scan
 * reads them again, as the shell's one-pass awk scans do. */

#define GRAPH_LIMIT (1U << 20)
#define FIELDS 24
#define DEP_SKIP (-1)
#define DEP_UNKNOWN (-2)

static size_t split(char *line, char **fields) {
    size_t count = 0;
    fields[count++] = line;
    for (; *line && count < FIELDS; line++)
        if (*line == '\t') { *line = '\0'; fields[count++] = line + 1; }
    return count;
}

static char *field(char **fields, size_t count, size_t index) { return index < count ? fields[index] : (char *)""; }

static int grow(void **items, size_t *capacity, size_t count, size_t size) {
    void *grown;
    if (count < *capacity) return 0;
    *capacity = *capacity ? *capacity * 2 : 32;
    grown = realloc(*items, *capacity * size);
    if (!grown) return -1;
    *items = grown;
    return 0;
}

static int add_step(struct wdr_run *run, size_t *capacity, char **fields, size_t count) {
    struct wdr_step *step;
    if (grow((void **)&run->steps, capacity, run->count, sizeof(*run->steps))) return -1;
    step = &run->steps[run->count++];
    memset(step, 0, sizeof(*step));
    step->id = field(fields, count, 1); step->kind = field(fields, count, 2); step->needs = field(fields, count, 3);
    step->retry = field(fields, count, 4); step->idem = field(fields, count, 5);
    return 0;
}

static int add_policy(struct wdr_run *run, size_t *capacity, char **fields, size_t count) {
    struct wdr_policy *policy;
    if (grow((void **)&run->policies, capacity, run->policy_count, sizeof(*run->policies))) return -1;
    policy = &run->policies[run->policy_count++];
    policy->id = field(fields, count, 1); policy->classes = field(fields, count, 2); policy->delay = field(fields, count, 3);
    return 0;
}

long wdr_find(const struct wdr_run *run, const char *id) {
    size_t i;
    for (i = 0; i < run->count; i++) if (!strcmp(run->steps[i].id, id)) return (long)i;
    return -1;
}

/* Dependencies split as the shell splits with IFS=",": a trailing comma ends
 * the list instead of naming an empty dependency; "-" names none. */
static void resolve_deps(struct wdr_run *run, struct wdr_step *step) {
    const char *cursor = step->needs;
    char name[256];
    step->dep_count = 0;
    while (*cursor && step->dep_count < WDR_MAX_DEPS) {
        size_t length = strcspn(cursor, ",");
        long index;
        if (length >= sizeof(name)) length = sizeof(name) - 1;
        memcpy(name, cursor, length); name[length] = '\0';
        index = !strcmp(name, "-") ? DEP_SKIP : wdr_find(run, name);
        step->deps[step->dep_count++] = index < 0 && index != DEP_SKIP ? DEP_UNKNOWN : (int)index;
        cursor += strcspn(cursor, ",");
        if (*cursor == ',') cursor++;
    }
}

static void mark_tasks(struct wdr_run *run, char *const *task_ids, size_t task_count) {
    size_t i;
    for (i = 0; i < task_count; i++) {
        long index = wdr_find(run, task_ids[i]);
        if (index >= 0) run->steps[index].task = true;
    }
}

static int parse(struct wdr_run *run) {
    size_t step_capacity = 0, policy_capacity = 0, task_count = 0, task_capacity = 0, i;
    char **task_ids = NULL, *line = run->graph; int status = 0;
    while (line && *line && !status) {
        char *next = strchr(line, '\n'), *fields[FIELDS]; size_t count;
        if (next) *next++ = '\0';
        count = split(line, fields);
        if (!strcmp(fields[0], "step")) status = add_step(run, &step_capacity, fields, count);
        else if (!strcmp(fields[0], "retry_policy")) status = add_policy(run, &policy_capacity, fields, count);
        else if (!strcmp(fields[0], "task_args")) {
            run->has_tasks = true;
            status = grow((void **)&task_ids, &task_capacity, task_count, sizeof(char *));
            if (!status) task_ids[task_count++] = field(fields, count, 1);
        }
        line = next;
    }
    if (!status) mark_tasks(run, task_ids, task_count);
    run->task_ids = task_ids; run->task_count = task_count;
    for (i = 0; i < run->count && !status; i++) resolve_deps(run, &run->steps[i]);
    return status;
}

int wdr_load(struct wdr_run *run, const char *dir) {
    char path[F_PATH];
    memset(run, 0, sizeof(*run));
    run->dir = dir;
    if (wdr_join(path, dir, "graph.tsv") || !(run->graph = f_read(path, GRAPH_LIMIT))) return -1;
    return parse(run);
}

void wdr_free(struct wdr_run *run) {
    free(run->graph); free(run->steps); free(run->policies); free(run->children); free(run->task_ids);
    memset(run, 0, sizeof(*run));
}

void wdr_read_states(struct wdr_run *run) {
    size_t i;
    for (i = 0; i < run->count; i++) wdr_step_line(run, i, "state", run->steps[i].state, sizeof(run->steps[i].state));
}

static bool listed(const char *states, const char *state) {
    size_t length = strlen(state);
    const char *cursor = states;
    if (!length) return false;
    while ((cursor = strstr(cursor, state))) {
        bool starts = cursor == states || cursor[-1] == ' ', ends = cursor[length] == ' ' || cursor[length] == '\0';
        if (starts && ends) return true;
        cursor += length;
    }
    return false;
}

/* workflow_count_states: steps (of a kind, if given) in one of the states. */
long wdr_count(struct wdr_run *run, const char *states, const char *kind) {
    size_t i; long count = 0;
    wdr_read_states(run);
    for (i = 0; i < run->count; i++)
        if ((!kind || !strcmp(run->steps[i].kind, kind)) && listed(states, run->steps[i].state)) count++;
    return count;
}

const struct wdr_policy *wdr_policy(const struct wdr_run *run, const char *id) {
    size_t i;
    for (i = 0; i < run->policy_count; i++) if (!strcmp(run->policies[i].id, id)) return &run->policies[i];
    return NULL;
}
