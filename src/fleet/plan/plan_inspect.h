#ifndef HYDRA_PLAN_INSPECT_H
#define HYDRA_PLAN_INSPECT_H
#include "fleet/plan/plan.h"

/* Borrowed validated plan/estimates; returned JSON belongs to the caller. */
/* A supported, structurally valid compiled artifact and its digest, or NULL. */
json_object *pi_read_compiled(const char *path, char digest[65]);
/* Estimates document from one run's recorded step durations, or NULL. */
json_object *plan_observed(const char *run);
/* decision-record, decision-show, decision-view and decision-list. */
json_object *plan_decision_cli(int argc, char **argv, bool *printed);
json_object *pi_explain(json_object *compiled, const char *digest);
json_object *pi_metrics(json_object *compiled, json_object *estimates);
bool pi_estimates_valid(json_object *estimates, json_object *compiled);
void pi_reach(json_object *steps, bool reach[PLAN_STEPS][PLAN_STEPS]);
/* Sections whose values differ between two compiled artifacts. */
json_object *pi_scope_differences(json_object *left, json_object *right);
/* Adds modeled_preference and reason from two pi_metrics-shaped objects. */
void pi_preference(json_object *data, bool matched, json_object *left, json_object *right);
/* Three-concern evaluation of two validated compiled artifacts; estimates may be NULL. */
json_object *pe_evaluate(json_object *left, const char *left_id, json_object *right, const char *right_id, json_object *estimates);
#endif
