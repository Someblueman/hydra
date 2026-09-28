#ifndef HYDRA_TUI_SETUP_MODEL_H
#define HYDRA_TUI_SETUP_MODEL_H
#include <stdbool.h>
#include <stddef.h>
/* Remote setup (U10) as the control centre sees it: a parsed `hydra remote
 * setup ... --json` envelope and the screen it calls for. Pure data: nothing
 * here runs a command, and the CLI stays the authority for every decision. */
#define SETUP_STEP_MAX 40
#define SETUP_ARG_MAX 16
#define SETUP_ARG_TEXT 320
#define SETUP_ROW_MAX 40
#define SETUP_REQUIREMENT_MAX 32
#define SETUP_AGENT_MAX 24

struct setup_step { char id[80], status[24], detail[240]; };
/* One displayed plan member; nested members are flattened as "a.b". */
struct setup_row { char key[64], value[512]; };
struct setup_requirement { char name[64], status[16], detail[240], command[240]; bool blocking; };
struct setup_agent { char name[64], status[24], path[240], version[64]; };

struct setup_envelope {
    bool parsed, ok, complete;
    int exit_status;
    char command[48], name[128], destination[256];
    char code[48], message[320], recovery[480];
    struct setup_step steps[SETUP_STEP_MAX];
    size_t step_count;
    /* data.next: the CLI's own next command (argv[0] is "hydra"). */
    char next_step[80], approval[72];
    char argv[SETUP_ARG_MAX][SETUP_ARG_TEXT];
    size_t argc;
    /* approval_required / approval_mismatch: data.plan and data.plan_sha256. */
    char plan_kind[32], plan_sha256[72], fingerprint[160];
    struct setup_row plan[SETUP_ROW_MAX];
    size_t plan_count;
    struct setup_requirement requirements[SETUP_REQUIREMENT_MAX];
    size_t requirement_count;
    struct setup_agent agents[SETUP_AGENT_MAX];
    size_t agent_count;
    /* host_key_changed: the presented key and the file holding the old one. */
    char presented[160], key_file[320], key_host[256];
};

enum setup_screen {
    SETUP_SCREEN_NONE, SETUP_SCREEN_FORM, SETUP_SCREEN_RUNNING, SETUP_SCREEN_STEPS,
    SETUP_SCREEN_TRUST_KEY, SETUP_SCREEN_PLAN, SETUP_SCREEN_KEY_CHANGED, SETUP_SCREEN_PREFLIGHT,
    SETUP_SCREEN_AGENTS, SETUP_SCREEN_UNKNOWN, SETUP_SCREEN_HANDOFF, SETUP_SCREEN_ERROR, SETUP_SCREEN_DONE
};

/* Plain-language account of an error code (U7): what happened, and what Enter
 * does next. action is NULL when Enter has nothing safe to offer. */
struct setup_explanation { const char *title, *body, *action; };

/* Parses text (length bytes, caller-owned) into *e. Returns 0, or -1 when the
 * text is not a version 1 Hydra envelope; *e then has parsed == false. */
int setup_envelope_parse(struct setup_envelope *e, const char *text, size_t length, int exit_status);
enum setup_screen setup_screen_for(const struct setup_envelope *e);
void setup_explain(const char *code, struct setup_explanation *out);
/* Short user-facing names for step ids and statuses. */
void setup_step_label(const char *id, char *out, size_t size);
const char *setup_status_label(const char *status);
const char *setup_status_mark(const char *status, bool ascii);
bool setup_status_finished(const char *status);
/* First step that is neither done nor skipped, or NULL. */
const struct setup_step *setup_open_step(const struct setup_envelope *e);
/* One-line summary for a host list row. */
void setup_summary(const struct setup_envelope *e, char *out, size_t size);
/* True when data.next.argv is a `hydra remote <setup command> NAME ...` for
 * this NAME using only known setup options, and any approval token in it is
 * exactly the plan the user was shown. */
bool setup_argv_valid(const struct setup_envelope *e, const char *name);
/* Steps that must own the terminal: agent installers and provider sign-in. */
bool setup_step_needs_terminal(const char *step);
/* Form validation mirrors the CLI's syntax checks for immediate feedback; the
 * CLI still validates. Returns NULL or a message. */
const char *setup_name_problem(const char *name);
const char *setup_destination_problem(const char *destination);
const char *setup_config_problem(const char *path);
/* Suggested NAME for a destination: its host part with unusable characters
 * dropped (may be empty). */
void setup_default_name(const char *destination, char *out, size_t size);
#endif
