#ifndef HYDRA_PLAN_H
#define HYDRA_PLAN_H
#include "fleet/fleet.h"
#include "fleet/support/json.h"

#define PLAN_LIMIT (256U * 1024U)
#define PLAN_STEPS 64U
#define PLAN_COMPILER "hydra-plan-1"
/* All returned JSON objects are owned by the caller. Diagnostics accumulate in
 * a caller-owned array; no validation function executes proposed operations. */
json_object *plan_cli(int argc, char **argv);
json_object *plan_read(const char *path);
json_object *plan_canonical(json_object *value);
void plan_error(json_object *errors, const char *path, const char *code, const char *message);
bool plan_id(const char *text);
bool plan_list(json_object *value, size_t minimum, size_t maximum);
bool plan_text(json_object *value);
bool plan_has(json_object *array, const char *text);
int plan_index(json_object *steps, const char *id);
int plan_validate(json_object *plan, json_object *policy, json_object *errors);
int plan_graph(json_object *plan, json_object *errors);
int plan_evidence_graph(json_object *plan, bool reach[PLAN_STEPS][PLAN_STEPS], json_object *errors);
int plan_lower(json_object *plan, const char *directory);
json_object *plan_task_bindings(json_object *plan, json_object *data, json_object *source, const char *scratch, json_object *errors);
json_object *plan_validation_context(json_object *compiled, const char *step);
int plan_step_check(const char *run, const char *step);
int plan_validation_write(const char *run, const char *step, const char *directory, const char *name);
json_object *plan_artifact(json_object *compiled, const char *run, const char *step, const char *name, char path[F_PATH]);
json_object *plan_compile(json_object *plan, json_object *policy, const char *source, json_object *errors);
int plan_digest(json_object *value, char digest[65]);
/* Borrowed JSON inputs. Digest is caller-owned; reports retain no references.
 * INVALID is malformed/stale evidence, distinct from a valid negative verdict. */
enum plan_verdict { PLAN_INVALID, PLAN_PASS, PLAN_FAIL, PLAN_INCONCLUSIVE };
int plan_check_digest(json_object *compiled, const char *check, char digest[65]);
enum plan_verdict plan_report(json_object *compiled, json_object *check,
                              json_object *report, const char *subject);
int plan_materialize(json_object *compiled, const char *directory);
int plan_admit(json_object *compiled, const char *source, const char *accepted);
int plan_heads_available(json_object *compiled, const char *source);
int plan_finish(const char *run);
json_object *plan_delivery(const char *run);
int plan_preview(json_object *compiled);
#endif
