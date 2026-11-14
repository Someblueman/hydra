#define _POSIX_C_SOURCE 200809L
#include "plan_draft.h"
#include "setup_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Room for a draft the size the plan schema allows; a larger one is not
 * summarised, and the page says so. */
#define DRAFT_TOKENS 8192

static void append(char *out, size_t size, const char *text) {
    size_t used = strlen(out);
    if (used + 1U < size) snprintf(out + used, size - used, "%s", text);
}

/* The string items of an array joined with ", " (argv with spaces). */
static void joined(const struct sj_doc *d, int array, const char *separator, char *out, size_t size) {
    char item[512];
    int i, token;
    out[0] = '\0';
    for (i = 0; (token = sj_item(d, array, i)) >= 0; i++) {
        if (!sj_text(d, token, item, sizeof(item))) continue;
        if (out[0]) append(out, size, separator);
        append(out, size, item);
    }
}

static void text_of(const struct sj_doc *d, int object, const char *key, char *out, size_t size) {
    if (object < 0 || !sj_path_text(d, object, key, out, size)) out[0] = '\0';
}

/* "runs sh compose.sh in plan-smoke" */
static void command_text(const struct sj_doc *d, int argv, const char *head, char *out, size_t size) {
    char command[768];
    joined(d, argv, " ", command, sizeof(command));
    snprintf(out, size, "runs %s%s%s", command, head[0] ? " in " : "", head);
}

/* An agent step: "codex in plan-smoke: <its instructions>". */
static void agent_text(const struct sj_doc *d, int args, const char *head, char *out, size_t size) {
    char profile[64], prompt[768], input[128];
    text_of(d, args, "profile", profile, sizeof(profile));
    text_of(d, args, "prompt", prompt, sizeof(prompt));
    text_of(d, args, "prompt_input", input, sizeof(input));
    snprintf(out, size, "%s%s%s%s%s", profile[0] ? profile : "an agent", head[0] ? " in " : "", head,
             prompt[0] ? ": " : input[0] ? ", instructions from " : "", prompt[0] ? prompt : input);
}

/* A workflow-schema step: what its kind and args say it does. */
static void schema_step(const struct sj_doc *d, int step, struct plan_draft_step *s) {
    int args = sj_find(d, step, "args"), argv = args >= 0 ? sj_find(d, args, "argv") : -1;
    char head[128], branch[128];
    text_of(d, args, "head", head, sizeof(head));
    text_of(d, args, "branch", branch, sizeof(branch));
    if (!strcmp(s->kind, "spawn")) snprintf(s->what, sizeof(s->what), "starts a head on branch %s", branch[0] ? branch : "(unnamed)");
    else if (argv >= 0) command_text(d, argv, head, s->what, sizeof(s->what));
    else if (args >= 0 && sj_find(d, args, "profile") >= 0) agent_text(d, args, head, s->what, sizeof(s->what));
    else s->what[0] = '\0';
}

/* A simple-form step: "agent" is the worker's instructions, "run" a command. */
static void simple_step(const struct sj_doc *d, int step, struct plan_draft_step *s) {
    int agent = sj_find(d, step, "agent"), run = sj_find(d, step, "run");
    if (run >= 0 && agent < 0) {
        snprintf(s->kind, sizeof(s->kind), "run");
        command_text(d, run, "", s->what, sizeof(s->what));
        return;
    }
    snprintf(s->kind, sizeof(s->kind), "agent");
    if (agent < 0 || !sj_text(d, agent, s->what, sizeof(s->what))) s->what[0] = '\0';
}

static void read_step(const struct sj_doc *d, int step, struct plan_draft_step *s) {
    memset(s, 0, sizeof(*s));
    text_of(d, step, "id", s->id, sizeof(s->id));
    text_of(d, step, "kind", s->kind, sizeof(s->kind));
    text_of(d, step, "role", s->role, sizeof(s->role));
    if (s->kind[0]) schema_step(d, step, s);
    else simple_step(d, step, s);
}

/* The declared envelope: tools, repository writes and the time limit. */
static void read_envelope(const struct sj_doc *d, int root, struct plan_draft *out) {
    int envelope = sj_find(d, root, "envelope");
    char seconds[32];
    if (envelope < 0) return;
    joined(d, sj_find(d, envelope, "tools"), ", ", out->tools, sizeof(out->tools));
    joined(d, sj_find(d, envelope, "writes"), ", ", out->writes, sizeof(out->writes));
    text_of(d, envelope, "timeout_seconds", seconds, sizeof(seconds));
    if (seconds[0]) out->minutes = (unsigned)((strtoul(seconds, NULL, 10) + 59UL) / 60UL);
}

bool plan_draft_parse(const char *text, size_t length, struct plan_draft *out) {
    struct sj_token *tokens = calloc(DRAFT_TOKENS, sizeof(*tokens));
    struct sj_doc d;
    int steps, step, i;
    bool ok = false;
    memset(out, 0, sizeof(*out));
    if (!tokens || !text || sj_parse(&d, text, length, tokens, DRAFT_TOKENS) || sj_type_of(&d, 0) != SJ_OBJECT) goto done;
    steps = sj_find(&d, 0, "steps");
    if (sj_type_of(&d, steps) != SJ_ARRAY) goto done;
    for (i = 0; (step = sj_item(&d, steps, i)) >= 0; i++) {
        out->steps_total++;
        if (out->step_count == PLAN_DRAFT_STEPS) continue;
        read_step(&d, step, &out->steps[out->step_count]);
        if (!strcmp(out->steps[out->step_count].kind, "spawn")) out->heads++;
        out->step_count++;
    }
    read_envelope(&d, 0, out);
    ok = true;
done:
    free(tokens);
    return ok;
}
