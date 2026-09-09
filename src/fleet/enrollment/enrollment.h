#ifndef HYDRA_FLEET_ENROLLMENT_H
#define HYDRA_FLEET_ENROLLMENT_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Review creates an immutable, operator-selected intent. Apply consumes only
 * that intent after an exact digest confirmation and reconciles each mutation. */
json_object *enrollment_cli(int argc, char **argv);
#endif
