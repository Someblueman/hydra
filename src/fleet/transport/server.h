#ifndef HYDRA_FLEET_TRANSPORT_SERVER_H
#define HYDRA_FLEET_TRANSPORT_SERVER_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Requests are borrowed; returned JSON belongs to the caller. */
json_object *f_handshake(void);
json_object *f_serve(json_object *request);
#endif
