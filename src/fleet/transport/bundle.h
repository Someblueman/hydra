#ifndef HYDRA_FLEET_TRANSPORT_BUNDLE_H
#define HYDRA_FLEET_TRANSPORT_BUNDLE_H
#include "fleet/fleet.h"
#include <json-c/json.h>

struct f_remote;
/* Inputs are borrowed; returned JSON belongs to the caller. */
json_object *f_bundle_export(const char *project, json_object *files, const char *run);
json_object *f_bundle_import(const char *project, json_object *bundle);
json_object *f_package(const char *source, const char *binary);
json_object *f_bootstrap(struct f_remote *remote, const char *file, const char *digest, unsigned seconds);
json_object *f_install(json_object *package, const char *digest);
#endif
