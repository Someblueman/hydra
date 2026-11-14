#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <stdio.h>
#include <string.h>

/* The short plan form ("plan": "simple-1") an agent can write without the full
 * schema: an objective, agent and command steps, and what counts as done. It is
 * lowered into an ordinary schema 1 plan before validation, so the digest,
 * admission, runtime, evidence and review all work on that plan. Lowering is a
 * pure function of the draft and the policy's envelope. */

#define SIMPLE_SPAWN "spawn-worker"
#define SIMPLE_AGENT_TIMEOUT 1800
#define SIMPLE_RUN_TIMEOUT 600
#define SIMPLE_ID_LENGTH 24U

struct simple {
    json_object *draft, *policy, *errors, *steps, *done;
    char id[65], worker[65], profile[65];
    size_t count;
    bool reach[PLAN_STEPS][PLAN_STEPS];
    int subject[PLAN_STEPS];
    bool check[PLAN_STEPS];
};

bool plan_simple(json_object *draft) {
    const char *form = f_string(draft, "plan");
    return form && !strcmp(form, PLAN_SIMPLE_FORM);
}

static json_object *texts(const char *first, const char *second) {
    json_object *array = json_object_new_array();
    if (first) json_object_array_add(array, json_object_new_string(first));
    if (second) json_object_array_add(array, json_object_new_string(second));
    return array;
}
static void add_int(json_object *object, const char *key, int64_t value) {
    json_object_object_add(object, key, json_object_new_int64(value));
}
static bool is_letter(char c) { return c >= 'a' && c <= 'z'; }
static bool is_alnum(char c) { return is_letter(c) || (c >= '0' && c <= '9'); }

/* A plan ID from the objective's first whole words: lowercase letters and
 * digits joined by single hyphens, at most 24 characters, starting with a
 * letter ("Add a greeting file and a test" gives add-a-greeting-file-and). */
static void objective_slug(const char *text, char out[65]) {
    size_t n = 0, word = 0; bool gap = false;
    for (const char *p = text ? text : ""; *p; p++) {
        char c = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
        if (!is_alnum(c)) { gap = n > 0; continue; }
        if (gap) { word = n; out[n++] = '-'; gap = false; }
        if (n >= SIMPLE_ID_LENGTH) { n = word; break; }
        out[n++] = c;
    }
    out[n] = '\0';
    if (!n) (void)f_copy(out, 65, "plan");
    else if (!is_letter(out[0])) { char copy[65]; (void)f_copy(copy, sizeof(copy), out); (void)snprintf(out, 65, "plan-%.59s", copy); }
}

static bool simple_header(struct simple *s) {
    const char *const keys[] = {"plan", "id", "objective", "worker", "context", "questions", "steps", "done_when", NULL};
    json_object *draft = s->draft;
    if (!task_keys(draft, keys)) {
        plan_error(s->errors, "$", "invalid_plan", "a simple-1 plan has only plan, objective, steps and done_when, and optionally id, worker, context and questions; see hydra plan schema");
        return false;
    }
    if (!plan_text(f_field(draft, "objective"))) plan_error(s->errors, "objective", "invalid_plan", "objective must be text of 1 to 8192 bytes");
    if (f_field(draft, "id") && !plan_id(f_string(draft, "id"))) plan_error(s->errors, "id", "invalid_plan", "id must be lowercase letters and digits separated by single - or _, at most 64 characters");
    if (f_field(draft, "worker") && !plan_id(f_string(draft, "worker"))) plan_error(s->errors, "worker", "invalid_plan", "worker must be a branch name of lowercase letters and digits separated by single - or _");
    if (f_field(draft, "id")) (void)f_copy(s->id, sizeof(s->id), f_string(draft, "id"));
    else objective_slug(f_string(draft, "objective"), s->id);
    if (f_field(draft, "worker")) (void)f_copy(s->worker, sizeof(s->worker), f_string(draft, "worker"));
    else (void)snprintf(s->worker, sizeof(s->worker), "%.59s-work", s->id);
    s->steps = f_field(draft, "steps"); s->done = f_field(draft, "done_when");
    if (!plan_list(s->steps, 1, PLAN_STEPS - 1)) { plan_error(s->errors, "steps", "invalid_steps", "expected 1 to 63 steps"); return false; }
    s->count = json_object_array_length(s->steps);
    return true;
}

/* The single agent profile the policy authorizes, unless the step names one. */
static void policy_profile(struct simple *s) {
    json_object *tools = f_field(f_field(s->policy, "envelope"), "tools");
    size_t found = 0;
    if (!s->policy) { (void)f_copy(s->profile, sizeof(s->profile), "agent"); return; }
    for (size_t i = 0; i < json_object_array_length(tools); i++) {
        const char *tool = f_text(json_object_array_get_idx(tools, i));
        if (tool && !strncmp(tool, "profile:", 8) && plan_id(tool + 8) && found++ == 0) (void)f_copy(s->profile, sizeof(s->profile), tool + 8);
    }
    if (found != 1) s->profile[0] = '\0';
}

static int earlier_step(struct simple *s, size_t before, const char *id) {
    for (size_t j = 0; id && j < before; j++)
        if (!strcmp(f_string(json_object_array_get_idx(s->steps, j), "id"), id)) return (int)j;
    return -1;
}
static bool step_needs(struct simple *s, size_t i, const char *path) {
    json_object *needs = f_field(json_object_array_get_idx(s->steps, i), "needs");
    if (!needs) { if (i) s->reach[i][i - 1] = true; return true; }
    if (!plan_list(needs, 0, PLAN_STEPS)) { plan_error(s->errors, path, "invalid_step", "needs must list the IDs of earlier steps"); return false; }
    for (size_t k = 0; k < json_object_array_length(needs); k++) {
        int j = earlier_step(s, i, f_text(json_object_array_get_idx(needs, k)));
        if (j < 0) { plan_error(s->errors, path, "missing_dependency", "needs may only name steps listed earlier in steps"); return false; }
        s->reach[i][j] = true;
    }
    for (size_t j = 0; j < i; j++)
        if (s->reach[i][j]) for (size_t k = 0; k < j; k++) s->reach[i][k] = s->reach[i][k] || s->reach[j][k];
    return true;
}
static bool run_strings(json_object *run) {
    if (!plan_list(run, 1, 64)) return false;
    for (size_t k = 0; k < json_object_array_length(run); k++)
        if (!json_object_is_type(json_object_array_get_idx(run, k), json_type_string)) return false;
    return true;
}
static bool step_recipe(struct simple *s, json_object *step, const char *path) {
    json_object *agent = f_field(step, "agent"), *run = f_field(step, "run"), *timeout = f_field(step, "timeout");
    const char *text = f_text(agent);
    if (!agent == !run) { plan_error(s->errors, path, "invalid_step", "each step has exactly one of agent (the worker's instructions) or run (a command as an argv list)"); return false; }
    if (agent && (!json_object_is_type(agent, json_type_string) || !text || !*text || strlen(text) > PLAN_PROMPT_LIMIT)) {
        plan_error(s->errors, path, "invalid_step", "agent must be the worker's complete instructions as text, at most 32 KiB"); return false;
    }
    if (run && !run_strings(run)) { plan_error(s->errors, path, "invalid_step", "run must be a command as a list of 1 to 64 strings, for example [\"make\", \"check\"]"); return false; }
    if (timeout && (!json_object_is_type(timeout, json_type_int) || json_object_get_int64(timeout) < 1 || json_object_get_int64(timeout) > 86400)) {
        plan_error(s->errors, path, "invalid_step", "timeout is whole seconds from 1 to 86400"); return false;
    }
    if (f_field(step, "profile") && (!agent || !plan_id(f_string(step, "profile")))) { plan_error(s->errors, path, "invalid_step", "profile names an agent profile and only fits an agent step"); return false; }
    return true;
}
static bool simple_step(struct simple *s, size_t i) {
    const char *const keys[] = {"id", "agent", "run", "needs", "timeout", "profile", NULL};
    json_object *step = json_object_array_get_idx(s->steps, i);
    const char *id = f_string(step, "id");
    char path[64];
    (void)snprintf(path, sizeof(path), "steps[%zu]", i);
    if (!task_keys(step, keys) || !plan_id(id) || !strcmp(id, SIMPLE_SPAWN) || earlier_step(s, i, id) >= 0) {
        plan_error(s->errors, path, "invalid_step", "each step has a unique id (not spawn-worker) and only id, agent or run, needs, timeout and profile");
        return false;
    }
    if (!step_recipe(s, step, path) || !step_needs(s, i, path)) return false;
    if (f_field(step, "agent") && !f_field(step, "profile") && !s->profile[0])
        plan_error(s->errors, path, "unsupported_profile", "the plan policy authorizes no single agent profile; name one with profile, or ask the user to allow it with hydra plan policy");
    return true;
}
static bool agent_at(struct simple *s, size_t i) { return f_field(json_object_array_get_idx(s->steps, i), "agent") != NULL; }

/* Each check runs after an agent step and checks the commit the last such step left. */
static void simple_subjects(struct simple *s) {
    for (size_t i = 0; i < s->count; i++) {
        s->subject[i] = -1;
        for (size_t j = 0; j < i; j++) if (s->reach[i][j] && agent_at(s, j)) s->subject[i] = (int)j;
    }
}
static bool simple_done(struct simple *s) {
    const char *const keys[] = {"criterion", "check", NULL};
    if (!plan_list(s->done, 1, 64)) { plan_error(s->errors, "done_when", "invalid_collection", "done_when lists 1 to 64 criteria, each with the run step that checks it"); return false; }
    for (size_t i = 0; i < json_object_array_length(s->done); i++) {
        json_object *item = json_object_array_get_idx(s->done, i);
        int step = earlier_step(s, s->count, f_string(item, "check"));
        char path[64];
        (void)snprintf(path, sizeof(path), "done_when[%zu]", i);
        if (!task_keys(item, keys) || !plan_text(f_field(item, "criterion")) || step < 0 || agent_at(s, (size_t)step)) {
            plan_error(s->errors, path, "invalid_record", "each criterion is text and check names a run step"); return false;
        }
        if (s->subject[step] < 0) { plan_error(s->errors, path, "invalid_verification", "a check must come after an agent step, so it checks the commit that step left"); return false; }
        s->check[step] = true;
    }
    return true;
}

static json_object *lowered_envelope(struct simple *s, int64_t seconds) {
    json_object *env = json_object_new_object(), *tools = json_object_new_array(), *allowed = f_field(s->policy, "envelope");
    char worker[80];
    for (size_t i = 0; i < s->count; i++) {
        json_object *step = json_object_array_get_idx(s->steps, i);
        char tool[80];
        if (agent_at(s, i)) (void)snprintf(tool, sizeof(tool), "profile:%s", f_string(step, "profile") ? f_string(step, "profile") : s->profile);
        else (void)f_copy(tool, sizeof(tool), f_text(json_object_array_get_idx(f_field(step, "run"), 0)));
        if (!plan_has(tools, tool)) json_object_array_add(tools, json_object_new_string(tool));
    }
    (void)snprintf(worker, sizeof(worker), "%s:*", s->worker);
    json_object_object_add(env, "hosts", texts("local", NULL)); json_object_object_add(env, "tools", tools);
    json_object_object_add(env, "effects", texts("execute", "worktree")); json_object_object_add(env, "writes", texts(worker, NULL));
    add_int(env, "parallelism", 1); add_int(env, "timeout_seconds", seconds);
    add_int(env, "artifact_bytes", f_field(allowed, "artifact_bytes") ? json_object_get_int64(f_field(allowed, "artifact_bytes")) : 1048576);
    add_int(env, "max_heads", 1);
    add_int(env, "disk_mb", f_field(allowed, "disk_mb") ? json_object_get_int64(f_field(allowed, "disk_mb")) : 1024);
    add_int(env, "retry_budget", 0); add_int(env, "repair_budget", 0);
    return env;
}
static json_object *lowered_needs(struct simple *s, size_t i) {
    json_object *needs = texts(SIMPLE_SPAWN, NULL), *declared = f_field(json_object_array_get_idx(s->steps, i), "needs");
    if (!declared && i) json_object_array_add(needs, json_object_new_string(f_string(json_object_array_get_idx(s->steps, i - 1), "id")));
    for (size_t k = 0; declared && k < json_object_array_length(declared); k++) json_object_array_add(needs, json_object_get(json_object_array_get_idx(declared, k)));
    /* A check consumes its subject's commit directly. */
    if (s->check[i]) {
        const char *subject = f_string(json_object_array_get_idx(s->steps, (size_t)s->subject[i]), "id");
        if (!plan_has(needs, subject)) json_object_array_add(needs, json_object_new_string(subject));
    }
    return needs;
}
static bool subject_of_check(struct simple *s, size_t agent) {
    for (size_t i = 0; i < s->count; i++) if (s->check[i] && s->subject[i] == (int)agent) return true;
    return false;
}
static json_object *lowered_step(struct simple *s, size_t i, int64_t *seconds) {
    json_object *draft = json_object_array_get_idx(s->steps, i), *step = json_object_new_object(), *args = json_object_new_object();
    bool agent = agent_at(s, i);
    int64_t timeout = f_field(draft, "timeout") ? json_object_get_int64(f_field(draft, "timeout")) : agent ? SIMPLE_AGENT_TIMEOUT : SIMPLE_RUN_TIMEOUT;
    char writes[80];
    (void)snprintf(writes, sizeof(writes), "%s:*", s->worker);
    f_string_add(step, "id", f_string(draft, "id"));
    f_string_add(step, "role", s->check[i] ? "verify" : agent && subject_of_check(s, i) ? "compose" : "work");
    f_string_add(step, "kind", "exec");
    json_object_object_add(step, "needs", lowered_needs(s, i));
    json_object_object_add(step, "writes", s->check[i] ? json_object_new_array() : texts(writes, NULL));
    f_string_add(args, "head", s->worker);
    if (agent) {
        f_string_add(args, "profile", f_string(draft, "profile") ? f_string(draft, "profile") : s->profile);
        f_string_add(args, "prompt", f_string(draft, "agent")); f_string_add(args, "result_file", "summary");
    } else json_object_object_add(args, "argv", json_object_get(f_field(draft, "run")));
    add_int(args, "timeout", timeout); *seconds += timeout;
    json_object_object_add(step, "args", args);
    return step;
}
static json_object *output(const char *type, const char *path, int64_t bytes) {
    json_object *declaration = json_object_new_object();
    f_string_add(declaration, "type", type); f_string_add(declaration, "path", path); add_int(declaration, "max_bytes", bytes);
    return declaration;
}
static json_object *lowered_data(struct simple *s) {
    json_object *data = json_object_new_object(), *steps = json_object_new_object();
    add_int(data, "schema_version", 1); json_object_object_add(data, "inputs", json_object_new_object());
    for (size_t i = 0; i < s->count; i++) {
        json_object *row = json_object_new_object(), *outputs = json_object_new_object();
        const char *id = f_string(json_object_array_get_idx(s->steps, i), "id");
        if (agent_at(s, i)) {
            json_object_object_add(outputs, "summary", output("file", "summary", 4096));
            json_object_object_add(outputs, "commit", output("file", "commit", 128));
        } else if (s->check[i]) {
            json_object *inputs = json_object_new_object(), *subject = json_object_new_object();
            f_string_add(subject, "step", f_string(json_object_array_get_idx(s->steps, (size_t)s->subject[i]), "id"));
            f_string_add(subject, "output", "commit");
            json_object_object_add(inputs, "subject", subject); json_object_object_add(row, "inputs", inputs);
            json_object_object_add(outputs, "report", output("object", "report.json", 4096));
        }
        if (json_object_object_length(outputs)) json_object_object_add(row, "outputs", outputs);
        else json_object_put(outputs);
        if (json_object_object_length(row)) json_object_object_add(steps, id, row);
        else json_object_put(row);
    }
    json_object_object_add(data, "steps", steps);
    return data;
}
static void join_argv(json_object *argv, char *out, size_t size) {
    size_t used = 0; out[0] = '\0';
    for (size_t k = 0; k < json_object_array_length(argv) && used < size; k++) {
        int n = snprintf(out + used, size - used, "%s%s", k ? " " : "", f_text(json_object_array_get_idx(argv, k)));
        if (n < 0) break;
        used += (size_t)n;
    }
}
/* Deliverables are the commits checks examine; each check reports on one. */
static void lowered_acceptance(struct simple *s, json_object *plan) {
    json_object *deliverables = json_object_new_array(), *checks = json_object_new_array(), *requirements = json_object_new_array();
    for (size_t i = 0; i < s->count; i++) {
        json_object *step = json_object_array_get_idx(s->steps, i);
        char text[4200], command[4096];
        if (agent_at(s, i) && subject_of_check(s, i)) {
            json_object *d = json_object_new_object();
            (void)snprintf(text, sizeof(text), "The commit branch %s reached after step %s", s->worker, f_string(step, "id"));
            f_string_add(d, "id", f_string(step, "id")); f_string_add(d, "description", text); f_string_add(d, "step", f_string(step, "id"));
            f_string_add(d, "output", "commit"); f_string_add(d, "destination", "run-artifact");
            json_object_array_add(deliverables, d);
        }
        if (!s->check[i]) continue;
        json_object *c = json_object_new_object();
        join_argv(f_field(step, "run"), command, sizeof(command));
        (void)snprintf(text, sizeof(text), "%s exits 0 on the worker's commit", command);
        f_string_add(c, "id", f_string(step, "id")); f_string_add(c, "method", "command"); f_string_add(c, "definition", text);
        f_string_add(c, "step", f_string(step, "id")); f_string_add(c, "input", "subject"); f_string_add(c, "report", "report");
        f_string_add(c, "deliverable", f_string(json_object_array_get_idx(s->steps, (size_t)s->subject[i]), "id"));
        json_object_array_add(checks, c);
    }
    for (size_t i = 0; i < json_object_array_length(s->done); i++) {
        json_object *item = json_object_array_get_idx(s->done, i), *r = json_object_new_object();
        int step = earlier_step(s, s->count, f_string(item, "check"));
        char id[32];
        (void)snprintf(id, sizeof(id), "done-%zu", i + 1);
        f_string_add(r, "id", id); f_string_add(r, "criterion", f_string(item, "criterion"));
        f_string_add(r, "deliverable", f_string(json_object_array_get_idx(s->steps, (size_t)s->subject[step]), "id"));
        f_string_add(r, "check", f_string(item, "check"));
        json_object_array_add(requirements, r);
    }
    json_object_object_add(plan, "deliverables", deliverables); json_object_object_add(plan, "checks", checks);
    json_object_object_add(plan, "requirements", requirements);
}
static json_object *spawn_step(struct simple *s) {
    json_object *step = json_object_new_object(), *args = json_object_new_object();
    f_string_add(step, "id", SIMPLE_SPAWN); f_string_add(step, "role", "work"); f_string_add(step, "kind", "spawn");
    json_object_object_add(step, "needs", json_object_new_array()); json_object_object_add(step, "writes", json_object_new_array());
    f_string_add(args, "branch", s->worker); f_string_add(args, "terminal_mode", "headless");
    json_object_object_add(step, "args", args);
    return step;
}
static json_object *optional_list(json_object *draft, const char *key) {
    json_object *value = f_field(draft, key);
    return value ? json_object_get(value) : json_object_new_array();
}
static json_object *lowered_plan(struct simple *s) {
    json_object *plan = json_object_new_object(), *steps = json_object_new_array();
    int64_t seconds = 0;
    add_int(plan, "schema_version", 1); f_string_add(plan, "id", s->id);
    json_object_object_add(plan, "objective", json_object_get(f_field(s->draft, "objective")));
    json_object_object_add(plan, "context", optional_list(s->draft, "context"));
    json_object_object_add(plan, "assumptions", json_object_new_array());
    json_object_object_add(plan, "questions", optional_list(s->draft, "questions"));
    json_object_array_add(steps, spawn_step(s));
    for (size_t i = 0; i < s->count; i++) json_object_array_add(steps, lowered_step(s, i, &seconds));
    json_object_object_add(plan, "envelope", lowered_envelope(s, seconds));
    json_object_object_add(plan, "steps", steps);
    lowered_acceptance(s, plan);
    json_object_object_add(plan, "data", lowered_data(s));
    return plan;
}

json_object *plan_simple_lower(json_object *draft, json_object *policy, json_object *errors) {
    static struct simple zero;
    struct simple s = zero;
    size_t before = json_object_array_length(errors);
    s.draft = draft; s.policy = policy; s.errors = errors;
    if (!simple_header(&s)) return NULL;
    policy_profile(&s);
    for (size_t i = 0; i < s.count; i++) if (!simple_step(&s, i)) return NULL;
    simple_subjects(&s);
    if (!simple_done(&s) || json_object_array_length(errors) != before) return NULL;
    return lowered_plan(&s);
}

json_object *plan_lowered(json_object *draft, json_object *policy, json_object *errors) {
    return plan_simple(draft) ? plan_simple_lower(draft, policy, errors) : json_object_get(draft);
}
