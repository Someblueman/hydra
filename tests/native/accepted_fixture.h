#ifndef HYDRA_ACCEPTED_FIXTURE_H
#define HYDRA_ACCEPTED_FIXTURE_H
#include "support.h"
#include <glob.h>
/* Caller owns glob results and must globfree. Operate on selected local
 * fixtures. */
void af_glob(glob_t *paths, const char *base, const char *suffix);
void af_run_path(char out[F_PATH], const char *home);
void af_environment(const char *run, const char *home, const char *fleet);
void af_hash_json(json_object *value, char digest[65], bool canonical);
void af_old(const char *directory);
#endif
