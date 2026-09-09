#ifndef HYDRA_FLEET_CLI_H
#define HYDRA_FLEET_CLI_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* argv is borrowed; returned JSON belongs to the caller. */
json_object *f_cli(int argc, char **argv);
int f_tui_data(unsigned seconds, unsigned jobs, bool hosts);
#endif
