#ifndef HYDRA_FLEET_ENROLLMENT_RECEIVER_H
#define HYDRA_FLEET_ENROLLMENT_RECEIVER_H
#include "fleet/fleet.h"
#include <json-c/json.h>

json_object *enrollment_apply_request(json_object *request, char **argv, unsigned seconds);
json_object *enrollment_preflight(json_object *request);
#endif
