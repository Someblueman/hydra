#ifndef HYDRA_NATIVE_TEST_SUPPORT_H
#define HYDRA_NATIVE_TEST_SUPPORT_H
#define _POSIX_C_SOURCE 200809L
#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Concrete CLI fixture utilities. Returned JSON/strings belong to caller. */
void nt_write(const char *path, const char *text);
void nt_json_write(const char *path, json_object *value);
json_object *nt_clone(json_object *value);
struct f_capture nt_run(char *const argv[]);
json_object *nt_output(struct f_capture *capture, int status);
void nt_equal(json_object *actual, const char *expected);
char *nt_replace(const char *text, const char *old, const char *replacement);
void nt_path(char *out, const char *base, const char *name);
void nt_copy_tree(const char *from, const char *to);
void nt_repo(const char *from, const char *to);
void nt_finish(const char *folder, const char *message);
/* Async fixture children are caller-owned and must be collected with nt_wait.
 */
struct nt_child {
  pid_t pid;
  FILE *out, *err;
};
struct nt_child nt_start(char *const argv[], const char *input);
struct f_capture nt_wait(struct nt_child *child, unsigned seconds);
void nt_await_file(const char *path, unsigned seconds);

#endif
