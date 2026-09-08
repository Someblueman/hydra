#ifndef HYDRA_WORKFLOW_DATA_H
#define HYDRA_WORKFLOW_DATA_H
#include "fleet/fleet.h"
#include "fleet/support/json.h"

#define WD_LIMIT 65536U
#define WD_NAMES 64U
#define WD_STEPS 256U
/* Objects returned by these functions are owned by the caller (json_object_put). */
json_object *wd_manifest(const char *manifest, const char *graph);
json_object *wd_file(const char *path, json_object *declaration);
json_object *wd_cli(int argc, char **argv);
int wd_fingerprint(const char *worktree, char digest[65]);
bool wd_name(const char *name);
int wd_initialize(json_object *manifest, const char *source, const char *run);
int wd_prepare(json_object *manifest, const char *run, const char *step, const char *attempt);
int wd_seal(json_object *manifest, const char *step, const char *attempt);
int wd_verify_output(json_object *manifest, const char *step, const char *attempt);
int wd_verify(json_object *manifest, const char *run);
#endif
