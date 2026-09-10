#ifndef HYDRA_FLEET_ENROLLMENT_H
#define HYDRA_FLEET_ENROLLMENT_H
#include "fleet/fleet.h"
#include <json-c/json.h>
#define ENROLL_HOSTS 100
struct enrollment_options {
    const char *input, *output, *project, *package, *sha256, *prefix, *alias, *confirm;
    const char *candidates[ENROLL_HOSTS]; size_t count; unsigned seconds;
};
json_object *enrollment_cli(int argc, char **argv);
json_object *enrollment_review(const struct enrollment_options *options);
json_object *enrollment_apply(const struct enrollment_options *options);
bool enrollment_path(const char *path);
bool enrollment_digest(const char *value);
int enrollment_hash(json_object *value, char hash[65]);
int enrollment_write(const char *path, json_object *value);
bool enrollment_file_matches(const char *path, const char *hash);
json_object *enrollment_host_apply(json_object *host, json_object *row, json_object *progress, const char *path, unsigned seconds);
#endif
