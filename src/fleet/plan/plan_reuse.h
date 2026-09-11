#ifndef HYDRA_PLAN_REUSE_H
#define HYDRA_PLAN_REUSE_H
#include "fleet/plan/plan.h"
/* Reconstructs a sealed original-attempt proof from current durable records.
 * The caller owns the result. Every reconstruction verifies the original bundle. */
json_object *pr_snapshot(const char *run, json_object *compiled, const char *id);
bool pr_environment(const char *path, json_object *binding);
#endif
