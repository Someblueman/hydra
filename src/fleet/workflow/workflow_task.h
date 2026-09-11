#ifndef HYDRA_WORKFLOW_TASK_H
#define HYDRA_WORKFLOW_TASK_H
#include "fleet/support/json.h"
#include "fleet/fleet.h"
/* Arguments are borrowed; returned JSON is owned by the caller. */
json_object *wt_cli(int argc, char **argv);
struct wt_transfer { int64_t calls, requests, responses; bool complete; };
json_object *wt_metric_record(const char *directory, const char *name, size_t limit);
bool wt_transfer_read(const char *directory, struct wt_transfer *transfer);
json_object *wt_metrics(const char *run);
int wt_metrics_tsv(const char *run);
json_object *wt_destination(const char *alias);
json_object *wt_bindings(const char *run);
json_object *wt_step_binding(const char *run, json_object *data, const char *id, const char *descriptor);
json_object *wt_source_binding(const char *run, json_object *bindings, json_object *binding);
bool wt_parent_receipt(const char *run, const char *step, const char *attempt, json_object *binding, json_object *receipt);
int wt_drive(const char *run);
int wt_initialize(const char *run, const char *source);
json_object *wt_execute(const char *run, const char *step, const char *attempt);
json_object *wt_request(json_object *destination, json_object *request, unsigned seconds,
                        const char *metrics_dir);
#endif
