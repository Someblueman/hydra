#ifndef HYDRA_FLEET_SETUP_H
#define HYDRA_FLEET_SETUP_H
/*
 * Guided remote setup (U10): shared contract for every setup workstream.
 *
 * Files and owners: setup.h, setup_cli.c, setup_state.c, setup_plan.c,
 * finish.c (verify/alias), hostkey.c and preflight.c belong to the setup core;
 * provision.c (and assets.c) to provisioning; agents.c, signin.c and recipes.c
 * to agent inventory, installers and sign-in. A workstream edits only its own
 * files; anything else it needs from the core is requested as a contract change.
 *
 * JSON ownership (json-c reference counting):
 *   - ctx->state is owned by the context. Steps read it through the accessors
 *     below (borrowed values, valid until the next setup_state_step call or
 *     setup_state_close) and change it only through setup_state_step.
 *   - Every setup_step_* function returns a caller-owned envelope, never NULL.
 *   - "Borrowed" arguments stay owned by the caller; "takes ownership" means the
 *     callee releases the value on every path, including failure.
 *   - Never let one json_object be reachable from both ctx->state and a
 *     returned envelope: the CLI adds members to envelope data. Return a
 *     json_object_deep_copy of anything also recorded as step detail.
 *
 * Envelopes: success is f_success(ctx->command, data). Failures use
 * setup_error(), which is f_error() plus an explicit recovery and an optional
 * top-level "data" object next to "error" (for example data.plan,
 * data.plan_sha256 and data.next of approval_required, or data.missing[] of
 * prerequisite_missing). The CLI rewrites "command" to the invoked command and
 * adds data.setup_schema, data.steps[] and data.next to every result, so a step
 * may return its own detail object as data and leave those members out.
 *
 * Exit status (setup_exit_status): 0 ok; 3 approval_required; 4
 * outcome_unknown (reconcile first); 128+n when the command or an interactive
 * child ended by signal n (error code "cancelled", optional integer
 * data.exit_status); 1 for every other error.
 */
#include "fleet/fleet.h"
#include "fleet/transport/remote.h"
#include <json-c/json.h>

#define SETUP_SCHEMA 1
/* setup_state_open flags. */
#define SETUP_CREATE 1U   /* create a new state file when none exists */
#define SETUP_READONLY 2U /* status views: no lock, setup_state_step fails */

/*
 * Step ids, in guided order: "host_key", "preflight", "provision", "agents",
 * then for each agent A listed in the agents step detail "selected" array:
 * "install_agent:A" and "sign_in:A"; finally "verify" and "alias".
 *
 * Step status values: pending (never recorded), in_progress (persisted before
 * any remote side effect), done, skipped, approval_required, blocked,
 * outcome_unknown and failed. The guided orchestrator re-runs every step whose
 * status is not done or skipped; a step called while its own status is
 * in_progress or outcome_unknown MUST reconcile the earlier attempt (for
 * provisioning: f_bootstrap_reconcile) and never replay a side effect.
 */
struct setup_ctx {
    /* Binding and connection identity. remote.name is NAME, remote.target is
     * DEST and remote.ssh_config the absolute -F file (or empty). Steps update
     * hydra/home/accepted_host_key/principal/project in place; every
     * setup_state_step persists the struct as state.remote, and the alias
     * step finally publishes it with f_remote_enrolled (no overwrite). */
    struct f_remote remote;
    json_object *state;  /* owned; see the accessors below */
    const char *name;    /* == remote.name */
    const char *command; /* envelope command, e.g. "remote-provision" */
    bool interactive;    /* prompts allowed (setup_interactive) */
    bool json;           /* --json given; next.argv then repeats --json */
    unsigned seconds;    /* SSH connect timeout for this command (--timeout) */
    /* Guided options; strings borrow argv and may be NULL. */
    const char *binary;
    /* Private to setup_state.c. */
    int lock_fd;
    bool readonly;
    char path[F_PATH];
};

/* ---- State: $HYDRA_HOME/fleet/setup/NAME.json (setup_state.c) ----
 * The directory is 0700 and owned by the user, the file 0600, the lock file
 * NAME.lock 0600 and held with flock(LOCK_EX|LOCK_NB) until
 * setup_state_close. Files are opened O_NOFOLLOW and checked for type, owner
 * and mode; content is replaced atomically (temp + fsync + rename + directory
 * fsync). NAME must satisfy f_name, be shorter than 128 bytes and not be the
 * reserved word "status".
 *
 * setup_state_open initialises *ctx (all fields, including remote) and returns
 * NULL on success or a caller-owned error envelope with command ctx->command
 * (which the caller sets before the call; NULL means "remote-setup"):
 *   invalid_input          bad NAME/DEST/ssh_config
 *   setup_not_started      no state and SETUP_CREATE not given (or no DEST)
 *   setup_busy             another process holds NAME.lock
 *   setup_binding_changed  DEST or ssh_config differ from the stored binding
 *   state_invalid          unsafe type/owner/mode, symlink, or bad content
 *   state_unavailable      the private directory cannot be created or used
 * dest and ssh_config may be NULL to accept the stored values. Close the
 * context with setup_state_close even after a failed open. */
json_object *setup_state_open(struct setup_ctx *ctx, const char *name, const char *dest,
                              const char *ssh_config, unsigned flags);
void setup_state_close(struct setup_ctx *ctx);
/* Records {status, detail, updated_at} for step and persists the whole state
 * (including ctx->remote) before returning. detail: takes ownership; NULL keeps
 * the previous detail. A detail object may carry a short "summary" string that
 * is shown in data.steps[].detail and in human output. Returns 0, or -1 for an
 * invalid step/status, a read-only context or an I/O failure; callers
 * then return setup_error(ctx, "state_unavailable", ...) and must not start
 * the side effect. */
int setup_state_step(struct setup_ctx *ctx, const char *step, const char *status, json_object *detail);
/* Borrowed; "pending" when the step was never recorded. */
const char *setup_state_status(struct setup_ctx *ctx, const char *step);
/* Borrowed detail object of a step, or NULL. */
json_object *setup_state_detail(struct setup_ctx *ctx, const char *step);
/* Core only: sets a top-level state member (value: takes ownership) and
 * persists like setup_state_step. Used for "upgrade" (see below). */
int setup_state_set(struct setup_ctx *ctx, const char *key, json_object *value);
/* Upgrade mode: `remote setup NAME` for an enrolled alias without setup state
 * binds to the alias target and ssh_config and records state.upgrade =
 * {"hydra": previous alias hydra, "target": alias target}. Borrowed; NULL for
 * a normal setup. Provisioning adds a new pin and keeps the old one; the alias
 * step then updates only the alias's hydra (and a newly recorded
 * accepted_host_key) after approval. */
json_object *setup_upgrade(struct setup_ctx *ctx);
bool setup_step_id(const char *step);
bool setup_status_valid(const char *status);

/* ---- Envelopes and interaction (setup_plan.c) ---- */
/* Failure envelope; data: takes ownership (NULL for none). recovery NULL uses
 * a generic "inspect ... then rerun" recovery. */
json_object *setup_error(const struct setup_ctx *ctx, const char *code, const char *message,
                         const char *recovery, json_object *data);
/* False with --json, when stdin or stderr is not a terminal, or when CI or
 * HYDRA_NONINTERACTIVE is set to a non-empty value. */
bool setup_interactive(bool json);
/* Canonical plan: a deep copy of plan (borrowed JSON object) with the members
 * schema "remote-setup-plan", schema_version 1, kind, name and destination set
 * from the context (these override plan members of the same name), plus
 * peer_fingerprint from ctx->remote.accepted_host_key when plan has none.
 * Object keys are sorted recursively and the plain serialization is hashed with
 * SHA-256. *canonical (optional) receives a caller-owned copy. Returns 0/-1. */
int setup_plan_hash(const struct setup_ctx *ctx, const char *kind, json_object *plan,
                    json_object **canonical, char digest[65]);
/*
 * Approval gate for one mutating step. step is the step id ("provision",
 * "install_agent:claude"); the plan kind is the id before any ':'. argv is the
 * borrowed NULL-terminated step command after "hydra remote" that repeats this
 * step without approval, e.g. {"install-agent", name, "--agent", "claude", NULL}.
 * Returns NULL when approved, otherwise a caller-owned error envelope:
 *   - approve given: it must equal the plan SHA-256 (for kind "host_key": the
 *     plan's "fingerprint" string, i.e. the --fingerprint value), else
 *     approval_mismatch. A matching approve never prompts.
 *   - interactive: the plan is printed to stderr and the user must answer y
 *     (kind "host_key": type "yes"); anything else is approval_declined.
 *   - otherwise approval_required with data {plan, plan_sha256, next{step,
 *     argv, approval_sha256}}; next.argv is ["hydra","remote",argv...,
 *     "--approve", HASH] ("--fingerprint", FP for host_key) plus "--json"
 *     when ctx->json. The step status is recorded as approval_required unless
 *     it is in_progress or outcome_unknown.
 * Every approval error carries data.plan and data.plan_sha256.
 */
json_object *setup_plan_gate(struct setup_ctx *ctx, const char *step, json_object *plan,
                             const char *approve, const char *const *argv);

/* ---- Step functions (all return a caller-owned envelope, never NULL) ----
 * Arguments other than ctx are borrowed argv strings and may be NULL when the
 * option was not given. The CLI has already validated their syntax:
 * fingerprint starts with "SHA256:", approve is 64 lowercase hex digits,
 * binary is a path, record is EXECUTABLE=/absolute/path, agent satisfies
 * f_name. On success a step records done/skipped itself; if it returns ok
 * without doing so the orchestrator records done. */
json_object *setup_step_trust_key(struct setup_ctx *ctx, const char *fingerprint);                /* hostkey.c */
/* Directories searched for agents beyond PATH (design §6), NULL-terminated,
 * in order; a leading "~/" means the remote HOME and "*" matches every nvm
 * Node version. The single native list: the preflight scan and its fallback
 * inventory use it, and it must equal the shell's agent_locate_search_dirs
 * (tests/test_remote_setup.sh compares them). */
extern const char *const setup_agent_search_dirs[];
/* setup_agent_search_dirs as a caller-owned JSON array of strings. */
json_object *setup_search_dirs_json(void);
json_object *setup_step_preflight(struct setup_ctx *ctx);                                         /* preflight.c */
json_object *setup_step_provision(struct setup_ctx *ctx, const char *binary, const char *approve); /* provision.c */
json_object *setup_step_agents(struct setup_ctx *ctx, const char *record);                        /* agents.c */
json_object *setup_step_install_agent(struct setup_ctx *ctx, const char *agent, const char *approve); /* agents.c */
json_object *setup_step_sign_in(struct setup_ctx *ctx, const char *agent);                        /* signin.c */
/* Core-owned final steps (finish.c): handshake + exact version check, then
 * no-overwrite alias publication (alias_conflict when a different alias exists).
 * In upgrade mode the alias step instead updates the existing alias through
 * the approval gate, or is skipped when the alias already uses ctx hydra;
 * approve is `remote setup NAME --approve PLAN_SHA256`. */
json_object *setup_step_verify(struct setup_ctx *ctx);
json_object *setup_step_alias(struct setup_ctx *ctx); /* == ..._approved(ctx, NULL) */
json_object *setup_step_alias_approved(struct setup_ctx *ctx, const char *approve);

/* ---- CLI (setup_cli.c) ---- */
/* True for "setup", "trust-key", "preflight", "provision", "agents",
 * "install-agent" and "sign-in" (the word after "remote"). f_remote_cli
 * dispatches these to setup_cli; "help"/"--help" as the second word prints
 * usage, and "setup status NAME" is the read-only status view. */
bool setup_command(const char *word);
/* argv starts at that word; returned JSON belongs to the caller. */
json_object *setup_cli(int argc, char **argv);
/* Writes the result: the JSON envelope on stdout with --json, otherwise human
 * lines on stderr only. Returns 0 or 1 like f_emit. */
int setup_emit(json_object *result, bool json);
int setup_exit_status(json_object *result, int status);
#endif
