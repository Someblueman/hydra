#ifndef HYDRA_PLAN_EXPANSION_H
#define HYDRA_PLAN_EXPANSION_H
#include "fleet/plan/plan.h"

/* Bounded expansion of a finished schema 1 plan run (roadmap 9F). A proposal
 * (hydra-plan-expansion-1) names new nodes, their dependencies on sealed
 * deliverables of earlier versions, new checks and the joins they feed. It is
 * lowered into an ordinary static plan whose inputs are the exact sealed
 * bytes, compiled by the one compiler under the base run's own policy and
 * recorded as version N+1 beside the earlier rounds. Nothing here executes:
 * execution is the ordinary exact-digest `workflow plan run` path, and the
 * shell owns every directory this module writes into. */
#define PLAN_EXPANSION "hydra-plan-expansion-1"
#define PLAN_EXPANSION_RECORD "hydra-plan-expansion-record-1"
#define PLAN_EXPANSION_ROUNDS 16U

/* One version of the chain: the base run (round 0) or an admitted round. */
struct px_version {
    int round;                /* 0 for the base run */
    json_object *compiled;    /* accepted compiled artifact; NULL when the round's files no longer validate */
    json_object *record;      /* expansion.json for rounds, NULL for the base */
    char directory[F_PATH];   /* round directory, empty for the base */
    char run[F_PATH];         /* run directory, empty when the round has not been executed */
    char run_id[80];
    char sha256[65];          /* compiled digest */
    const char *run_state;    /* not_executed, absent, unknown, succeeded, failed or cancelled */
};
struct px_chain {
    struct px_version versions[PLAN_EXPANSION_ROUNDS + 1];
    size_t count;
    char base_run_id[80];
    json_object *errors;      /* chain integrity diagnostics, owned by the chain */
};

/* Policy member `expansion` carries the five integer bounds; absent means no
 * expansion is authorized. Malformed bounds add invalid_policy. */
bool plan_expansion_policy(json_object *policy, json_object *errors);
/* Closed-schema proposal check; adds diagnostics and returns false on any. */
bool px_proposal_validate(json_object *proposal, json_object *errors);
/* Reads the base run and every admitted round below expansions (which need
 * not exist). Rounds whose files changed since admission add expansion_changed. */
void px_chain_load(struct px_chain *chain, const char *expansions, const char *base_run, const char *runs);
void px_chain_free(struct px_chain *chain);
/* Whether a run state is a terminal outcome (succeeded, failed or cancelled). */
bool px_terminal(const char *state);
/* Status of one deliverable in one version, recomputed from its run record:
 * pass, fail, invalid, unknown, not_executed or unknown_deliverable. When text
 * is not NULL a passing deliverable's sealed bytes are returned as a
 * caller-owned string (unsupported_artifact when they cannot travel as a
 * bundle input) and declaration borrows its output declaration. */
const char *px_deliverable_status(const struct px_version *version, const char *deliverable, json_object **text, json_object **declaration);
/* Effects of the producer closure of a deliverable: artifact_only,
 * declared_writes or agent. Recorded for transparency, never a verdict. */
const char *px_deliverable_effects(json_object *plan, const char *deliverable);
/* The version whose plan declares the deliverable or requirement, or NULL. */
const struct px_version *px_find(const struct px_chain *chain, const char *collection, const char *id);
/* Sum of exec timeouts (work seconds) of one plan. */
int64_t px_work_seconds(json_object *plan);
/* A typed refusal envelope; details (owned, may be NULL) become its data. */
json_object *px_refuse(const char *code, const char *message, const char *recovery, json_object *details);
/* Classifies the proposal's claims against the chain. NULL and caller-owned
 * record rows on success, otherwise the refusal envelope. */
json_object *px_classify_claims(const struct px_chain *chain, json_object *proposal, json_object **rows);
/* Resolves dependencies to sealed, passing deliverables: record rows, the
 * {input: text} assets and the data.inputs declarations, all caller-owned. */
json_object *px_resolve_dependencies(const struct px_chain *chain, json_object *proposal, json_object **rows, json_object **assets, json_object **inputs);
/* Admits one proposal into directory (proposal.json, compiled.json, then
 * expansion.json last) as round chain->count. expansions names the chain
 * directory the shell publishes the round into. Returns the envelope result. */
json_object *px_admit(const char *directory, const char *expansions, const struct px_chain *chain, json_object *proposal, const char *source, long recorded_at);
/* Read-only view of the chain with claim, dependency and run states. */
json_object *px_show(const struct px_chain *chain);
/* Execution gate for one round: the accepted digest must be the admitted
 * one, the chain up to that round intact, its dependencies unchanged and the
 * round not executed before. */
json_object *px_bind(const char *round, const char *accepted, const char *runs);
json_object *plan_expansion_cli(int argc, char **argv, bool *printed);
#endif
