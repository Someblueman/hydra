#ifndef HYDRA_FLEET_H
#define HYDRA_FLEET_H
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#define F_LIMIT (8U * 1024U * 1024U)
#define F_PATH 4096
#define F_PROTOCOL 1
#define F_VERSION "2.0.0"
struct f_capture { char *out, *err; int status; bool timeout; };
struct f_remote { char name[128], target[256], hydra[F_PATH], home[F_PATH]; bool multiplex; };
extern volatile sig_atomic_t f_stopped;
extern const char *f_home, *f_hydra;
/* Returned JSON objects and capture buffers are caller-owned. */
json_object *f_parse(const char *text);
json_object *f_error(const char *command, const char *code, const char *message);
json_object *f_success(const char *command, json_object *data);
bool f_number_is(json_object *obj, const char *key, int expected);
const char *f_string(json_object *obj, const char *key);
json_object *f_field(json_object *obj, const char *key);
void f_string_add(json_object *obj, const char *key, const char *value);
int f_emit(json_object *obj);
int f_path(char *dst, size_t size, const char *a, const char *b);
int f_mkdirs(const char *path);
char *f_read(const char *path, size_t limit);
int f_write(const char *path, const char *data, size_t size, bool replace);
int f_copy(char *dst, size_t size, const char *value);
bool f_name(const char *value);
bool f_target(const char *value);
void f_capture_free(struct f_capture *cap);
int f_run(char *const argv[], const char *input, size_t size, unsigned seconds, struct f_capture *cap);
char *f_quote(const char *value);
int f_ssh(const struct f_remote *remote, const char *command, const char *input, size_t size, unsigned seconds, bool tty, struct f_capture *cap);
int f_remote_load(const char *name, struct f_remote *remote);
int f_remote_save(const struct f_remote *remote);
json_object *f_remotes(void);
json_object *f_remote_cli(int argc, char **argv);
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds);
json_object *f_handshake(void);
json_object *f_serve(json_object *request);
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds);
json_object *f_aggregate(const char *action, unsigned seconds, unsigned jobs);
json_object *f_bundle_export(const char *project, json_object *files, const char *run);
json_object *f_bundle_import(const char *project, json_object *bundle);
json_object *f_package(const char *source, const char *binary);
json_object *f_bootstrap(struct f_remote *remote, const char *file, const char *digest, unsigned seconds);
json_object *f_install(json_object *package, const char *digest);
int f_hash(const char *path, char digest[65]);
char *f_hex_read(const char *path);
int f_remove_tree(const char *path);
int f_hex_write(const char *path, const char *hex, unsigned mode);
json_object *f_cli(int argc, char **argv);
int f_tui_data(unsigned seconds, unsigned jobs);
#endif
