#ifndef HYDRA_WORKFLOW_CONTRACT_H
#define HYDRA_WORKFLOW_CONTRACT_H
#include "fleet/workflow/workflow_data.h"
/* Borrowed JSON; returned JSON is owned by caller (json_object_put). */
bool wc_schema(json_object *contract, const char *type);
bool wc_value(json_object *contract, json_object *value);
bool wc_manifest(json_object *manifest);
json_object *wc_declaration(json_object *manifest, json_object *reference);
int wc_rules(json_object *manifest, const char *step, const char *attempt, bool after);
json_object *wc_provenance(const char *run, json_object *reference);
#endif
