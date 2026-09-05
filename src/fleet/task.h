#ifndef HYDRA_FLEET_TASK_H
#define HYDRA_FLEET_TASK_H
#include "fleet.h"

/* A package fits in one bounded fleet request, including its JSON envelope. */
#define TASK_PACKAGE_LIMIT (F_LIMIT / 2)
#define TASK_FILE_LIMIT (512U * 1024U)
#define TASK_FILES 64
bool task_path(const char *path);
bool task_hex(const char *text, size_t length);
bool task_keys(json_object *object, const char *const keys[]);
const char *task_text(json_object *value);
/* Validation also creates a canonical copy; the caller owns the result. */
json_object *task_spec(json_object *input, bool prepared);
int task_json_hash(json_object *object, const char *scratch, char digest[65]);
json_object *task_prepare(const char *source, json_object *spec);
json_object *task_inspect(json_object *package);
json_object *task_cli(int argc, char **argv);
json_object *task_remote_cli(int argc, char **argv);
json_object *task_serve(json_object *request);
json_object *task_accept(json_object *package, const char *key);
json_object *task_status(const char *id);
int task_store_root(char root[F_PATH]);
int task_sync_dir(const char *path);
int task_write_json(const char *directory, const char *name, json_object *value, bool replace);
json_object *task_read_record(const char *directory, const char *name);
json_object *task_start(const char *id, const char *trust);
bool task_owner_active(const char *directory);
bool task_runtime_valid(json_object *state);
void task_execute(const char *directory, json_object *package, json_object *state);
#endif
