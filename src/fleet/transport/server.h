#ifndef HYDRA_FLEET_TRANSPORT_SERVER_H
#define HYDRA_FLEET_TRANSPORT_SERVER_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Probe installed tmux version without allocating a terminal session. */
bool f_terminal_available(void);
/* Requests are borrowed; returned JSON belongs to the caller. */
json_object *f_handshake(void);
json_object *f_serve(json_object *request);
/* Internal shell boundary shared by ordinary and reconciled enrollment calls. */
json_object *f_run_hydra(char **argv, unsigned seconds);
#endif
