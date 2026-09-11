#ifndef HYDRA_PLAN_INSPECT_H
#define HYDRA_PLAN_INSPECT_H
#include "fleet/plan/plan.h"

/* Borrowed validated plan/estimates; returned JSON belongs to the caller. */
json_object *pi_explain(json_object *compiled, const char *digest);
json_object *pi_metrics(json_object *compiled, json_object *estimates);
bool pi_estimates_valid(json_object *estimates, json_object *compiled);
void pi_reach(json_object *steps, bool reach[PLAN_STEPS][PLAN_STEPS]);
#endif
