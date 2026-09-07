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
int plan_lower(json_object *plan, const char *directory);
json_object *plan_compile(json_object *plan, json_object *policy, const char *source, json_object *errors);
int plan_digest(json_object *value, char digest[65]);
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
