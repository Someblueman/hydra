#ifndef HYDRA_PLAN_H
#define HYDRA_PLAN_H
#include "fleet/fleet.h"
#include "fleet/support/json.h"

#define PLAN_LIMIT (256U * 1024U)
#define PLAN_STEPS 64U
#define PLAN_OBLIGATIONS 256U
#define PLAN_COMPILER "hydra-plan-1"
/* Inline agent prompts and proposal assets travel inside the compiled artifact. */
#define PLAN_PROMPT_LIMIT (32U * 1024U)
#define PLAN_ASSET_LIMIT (64U * 1024U)
#define PLAN_ASSETS 16U
#define PLAN_INPUT_PREFIX "@input/"
/* All returned JSON objects are owned by the caller. Diagnostics accumulate in
 * a caller-owned array; no validation function executes proposed operations. */
json_object *plan_cli(int argc, char **argv);
/* Offline inspections borrow paths and return caller-owned JSON. */
json_object *plan_inspect_cli(int argc, char **argv);
json_object *plan_read(const char *path);
/* assets may be NULL; otherwise the draft's asset references must match its files exactly. */
json_object *plan_proposal_copy(const char *input, const char *output, const char *assets);
/* Copies one bounded UTF-8 text asset without following a final symlink. */
json_object *plan_proposal_asset(const char *name, const char *file, const char *directory);
/* Well-formed UTF-8 without NUL bytes. */
bool plan_utf8(const char *text, size_t length);
/* Loads the assets referenced by plan data inputs from directory (may be NULL)
 * into a caller-owned {name: text} object; unreadable assets add diagnostics. */
json_object *plan_assets_load(json_object *plan, const char *directory, json_object *errors);
/* Returns a caller-owned plan with inline prompts and asset inputs lowered to
 * digest-bound bundle inputs; files receives {bundle-path: text}. NULL on error. */
json_object *plan_expand(json_object *plan, json_object *assets, json_object *files, json_object *errors);
/* Writes {bundle-path: text} below directory/bundle. */
int plan_bundle_write(json_object *files, const char *directory);
/* The {name: text} assets carried by files, or NULL when there are none. */
json_object *plan_bundle_assets(json_object *files);
/* Borrows NUL-terminated JSON text; checks member uniqueness and exact signed integer tokens. */
bool plan_json_unique(const char *text);
json_object *plan_canonical(json_object *value);
void plan_error(json_object *errors, const char *path, const char *code, const char *message);
void plan_obligation_error(json_object *errors, const char *path, const char *code,
                           const char *message, const char *obligation,
                           const char *counterexample);
bool plan_id(const char *text);
bool plan_list(json_object *value, size_t minimum, size_t maximum);
bool plan_text(json_object *value);
bool plan_has(json_object *array, const char *text);
int plan_index(json_object *steps, const char *id);
int plan_validate(json_object *plan, json_object *policy, json_object *errors);
int plan_relations(json_object *plan, json_object *errors);
int plan_graph(json_object *plan, json_object *errors);
int plan_evidence_graph(json_object *plan, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors);
int plan_obligations_validate(json_object *plan, json_object *errors);
int plan_obligations_graph(json_object *plan, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors);
json_object *plan_obligations_projection(json_object *plan);
json_object *plan_obligations_reviews(json_object *plan);
int plan_lower(json_object *plan, const char *directory);
json_object *plan_task_bindings(json_object *plan, json_object *data, json_object *source, const char *scratch, json_object *errors);
json_object *plan_validation_context(json_object *compiled, const char *step);
json_object *plan_run_definition(const char *run);
int plan_attempt_directory(const char *run, const char *step, char directory[F_PATH]);
json_object *plan_repair_evidence(const char *run, json_object *compiled);
int plan_reuse_validate(json_object *plan, json_object *errors);
int plan_reuse_environment(json_object *compiled, const char *scratch, json_object *errors);
json_object *plan_reuse_capture(const char *run, json_object *compiled, json_object *failures);
bool plan_reuse_verify(const char *run, json_object *compiled, json_object *evidence, int round, bool applying);
int plan_repair(const char *run, bool resume);
int plan_task_attempt(const char *run);
int plan_repair_write(const char *run, const char *directory, const char *name);
bool plan_repair_fresh(const char *run, json_object *check, const char *subject);
int plan_step_check(const char *run, const char *step);
int plan_validation_write(const char *run, const char *step, const char *directory, const char *name);
json_object *plan_artifact(json_object *compiled, const char *run, const char *step, const char *name, char path[F_PATH]);
/* assets is a borrowed {name: text} object or NULL. */
json_object *plan_compile(json_object *plan, json_object *policy, const char *source, json_object *assets, json_object *errors);
/* Adds an invalid_source diagnostic naming the checkout and failed condition. */
void plan_source_error(json_object *errors, const char *source);
int plan_digest(json_object *value, char digest[65]);
/* Borrowed JSON inputs. Digest is caller-owned; reports retain no references.
 * INVALID is malformed/stale evidence, distinct from a valid negative verdict. */
enum plan_verdict { PLAN_INVALID, PLAN_PASS, PLAN_FAIL, PLAN_INCONCLUSIVE };
enum plan_verdict plan_check_result(json_object *compiled, const char *run, json_object *check);
int plan_check_digest(json_object *compiled, const char *check, char digest[65]);
int plan_recipe_digest(json_object *compiled, const char *check, char digest[65]);
enum plan_verdict plan_report(json_object *compiled, json_object *check,
                              json_object *report, const char *subject);
int plan_materialize(json_object *compiled, const char *directory);
int plan_admit(json_object *compiled, const char *source, const char *accepted);
int plan_heads_available(json_object *compiled, const char *source);
int plan_finish(const char *run);
json_object *plan_delivery(const char *run);
/* Borrows a successful plan_delivery result; prints its verification evidence. */
void plan_delivery_view(json_object *delivery);
int plan_preview(json_object *compiled, FILE *out);
int plan_tui(json_object *compiled);
#endif
