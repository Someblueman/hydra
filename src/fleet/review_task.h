#ifndef HYDRA_REVIEW_TASK_H
#define HYDRA_REVIEW_TASK_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Borrowed CLI arguments; caller owns returned JSON (json_object_put).
 * review-data emits the shared text projection and returns NULL on success. */
json_object *review_task_cli(int argc, char **argv, bool text_projection);
#endif
