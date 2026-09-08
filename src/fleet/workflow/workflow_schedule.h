#ifndef HYDRA_WORKFLOW_SCHEDULE_H
#define HYDRA_WORKFLOW_SCHEDULE_H
#include "fleet/support/json.h"
/* The decision function is pure: graph order breaks ties, and the recorded
 * state vector and parallelism bound are its only observations. */
json_object *ws_observe(const char *run, json_object *graph);
int ws_decide(json_object *graph, json_object *observations, int parallelism, char choice[65]);
int ws_next(const char *run);
json_object *ws_replay(const char *run);
#endif
