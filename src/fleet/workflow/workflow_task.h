#ifndef HYDRA_WORKFLOW_TASK_H
#define HYDRA_WORKFLOW_TASK_H
#include "fleet/support/json.h"
#include "fleet/fleet.h"
/* Arguments are borrowed; returned JSON is owned by the caller. */
json_object *wt_cli(int argc, char **argv);
json_object *wt_destination(const char *alias);
json_object *wt_bindings(const char *run);
json_object *wt_step_binding(const char *run, json_object *data, const char *id, const char *descriptor);
json_object *wt_source_binding(const char *run, json_object *bindings, json_object *binding);
int wt_drive(const char *run);
int wt_initialize(const char *run, const char *source);
json_object *wt_execute(const char *run, const char *step, const char *attempt);
json_object *wt_request(json_object *destination, json_object *request, unsigned seconds);
#endif
