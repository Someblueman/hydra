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
struct task_control {
    struct f_control process;
    const char *directory, *digest;
    json_object *state;
    bool cancel_seen;
};
int task_control_open(struct task_control *control, const char *directory, const char *digest, json_object *state, json_object *limits);
void task_control_close(struct task_control *control);
void task_cancel_view(const char *directory, const char *digest, json_object *state);
json_object *task_cancel(const char *id);
json_object *task_logs(const char *id, json_object *request);
int task_git(const char *directory, char *const args[], struct f_capture *cap);
int task_file_copy(const char *root, const char *relative, const char *destination);
json_object *task_result(const char *id);
json_object *task_result_heads(const char *directory, json_object *state, const char *scratch);
int task_result_files(json_object *files, const char *root, const char *relative, const char *scratch, size_t *remaining);
int task_result_evidence(json_object *files, const char *directory, json_object *state, json_object *heads, const char *scratch);
json_object *task_result_verify(json_object *envelope);
int task_result_seal(const char *directory, json_object *state);
int task_result_bindings(json_object *result, const char *scratch);
int task_owned_directory(int parent, const char *name, bool create);
int task_collection_root(const char *project, char root[F_PATH], bool create);
json_object *task_collect(const char *project, json_object *envelope);
json_object *task_collected(const char *project, const char *id);
json_object *task_collection_cli(int argc, char **argv);
int task_collection_refs(const char *project, const char *id, json_object *result, const char *bundle, bool install);
int task_collection_file(const char *root, const char *relative, const char *hex);
#endif
