#ifndef FLEET_RECOVERY_PTY_SUPPORT_H
#define FLEET_RECOVERY_PTY_SUPPORT_H
#include "pty_support.h"
#include <json-c/json.h>
extern char root[4096], build[4096], base[4096], source[4096], bin[4096], tui[4096], client[4096],
    output[1048576];
extern bool succeeded, cleaned, cleanup_failed;
extern const char workflow[];
json_object *field(json_object *o, const char *key);
json_object *optional(json_object *o, const char *key);
const char *string(json_object *o);
void addstr(json_object *o, const char *key, const char *value);
json_object *parse(const char *text);
void save_json(const char *name, json_object *value);
void trim(char *value);
const char *run_at(const char *cwd, const char *const argv[]);
#define RUN(...) run_at(source, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(bin, __VA_ARGS__)
json_object *status(const char *host, const char *task);
bool runtime_is(json_object *data, const char *key, const char *expected);
json_object *wait_runtime(const char *host, const char *task, const char *key, const char *expected,
                          double timeout);
void check_runtime(const char *host, const char *task, const char *key, const char *expected);
void cleanup(void);
void open_observer(struct tv_session *s);
void select_task(struct tv_session *s, const char *task);
size_t acceptance_count(const char *name);
json_object *workflow_logs(const char *task, long long offset, const char *kind, int limit);
void attempt_exact(json_object *rows, const char *state);
bool work_running(json_object *document);
void sequences(json_object *events, json_object *seen);
void unhex(const char *hex, char *out, size_t cap);
#endif
