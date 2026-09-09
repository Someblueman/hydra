#ifndef HYDRA_HOST_DISCOVERY_H
#define HYDRA_HOST_DISCOVERY_H
#include "fleet/fleet.h"
#include <json-c/json.h>

#define HD_HOSTS 16
struct hd_options {
    const char *ssh[HD_HOSTS], *select[HD_HOSTS];
    size_t ssh_count, select_count;
    const char *inventory, *config, *capability;
    unsigned seconds;
    bool probe;
};
/* Inputs are borrowed. Returned JSON belongs to the caller. Config output is
 * caller-owned and must be unlinked after all SSH children have exited. */
json_object *hd_cli(int argc, char **argv);
json_object *hd_sources(const struct hd_options *options);
json_object *hd_resolve(const char *target, const char *config, unsigned seconds);
json_object *hd_probe(json_object *candidate, const char *config, const struct hd_options *options);
int hd_config(char path[F_PATH], const char *source);
bool hd_text(const char *text, size_t limit);
bool hd_keys(json_object *object, const char *const *keys);
void hd_id(char id[520], const char *target);
#endif
