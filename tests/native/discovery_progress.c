#include "discovery.h"
#include <errno.h>
#include <glob.h>
#include <signal.h>
static size_t qualified(json_object *rows) {
  size_t count = 0;
  for (size_t i = 0; i < json_object_array_length(rows); i++)
    if (f_field(json_object_array_get_idx(rows, i), "qualification"))
      count++;
  return count;
}
static void batches(void) {
  char inventory[F_PATH], progress[F_PATH], names[20][32], targets[20][32];
  nt_path(inventory, dc_root, "batch.json");
  nt_path(progress, dc_root, "progress.json");
  json_object *doc =
      f_parse("{\"schema_version\":1,\"observed_at\":1,\"hosts\":[]}");
  const char *args[45];
  args[0] = "--inventory";
  args[1] = inventory;
  for (size_t i = 0; i < 20; i++) {
    assert(snprintf(names[i], sizeof names[i], "h%zu", i) > 0 &&
           snprintf(targets[i], sizeof targets[i], "host%zu", i) > 0);
    json_object *r = f_parse("{\"labels\":[]}");
    f_string_add(r, "name", names[i]);
    f_string_add(r, "target", targets[i]);
    json_object_array_add(f_field(doc, "hosts"), r);
    args[2 + 2 * i] = "--select";
    args[3 + 2 * i] = names[i];
  }
  args[42] = "--progress";
  args[43] = progress;
  args[44] = NULL;
  nt_json_write(inventory, doc);
  json_object_put(doc);
  json_object *v = dc_cli("qualify", args, true);
  assert(qualified(dc_rows(v)) == 16);
  nt_equal(f_field(f_field(v, "data"), "complete"), "false");
  nt_equal(f_field(f_field(v, "data"), "pending_count"), "4");
  assert(dc_call_count() == 16);
  json_object *r = json_object_array_get_idx(dc_rows(v), 0);
  f_string_add(r, "candidate_id", "forged");
  f_string_add(f_field(f_field(r, "qualification"), "data"), "peer_fingerprint",
               "SHA256:forged");
  nt_json_write(progress, v);
  json_object_put(v);
  v = dc_cli("qualify", args, true);
  assert(qualified(dc_rows(v)) == 20);
  nt_equal(f_field(f_field(v, "data"), "complete"), "true");
  nt_equal(f_field(f_field(v, "data"), "processed_this_batch"), "4");
  assert(dc_call_count() == 20 &&
         !strstr(json_object_to_json_string(v), "forged"));
  json_object_put(v);
  v = dc_cli("qualify", args, true);
  nt_equal(f_field(f_field(v, "data"), "processed_this_batch"), "0");
  assert(dc_call_count() == 20);
  json_object_put(v);
}
static void binding(void) {
  char inventory[F_PATH], progress[F_PATH], pattern[F_PATH];
  dc_snapshot(
      inventory, "vpn",
      f_parse_value(
          "[{\"peer\":\"selected\",\"address\":\"good\",\"labels\":[]}]"),
      1);
  nt_path(progress, dc_root, "resume.json");
  const char *args[] = {"--inventory", inventory, "--select", "selected",
                        "--progress",  progress,  NULL};
  char *original = f_read(inventory, 1000000);
  assert(original);
  json_object_put(dc_cli("qualify", args, true));
  nt_path(pattern, dc_home, "fleet/discovery/*.json");
  glob_t paths = {0};
  assert(!glob(pattern, 0, NULL, &paths) && paths.gl_pathc == 1);
  const char *private = paths.gl_pathv[0];
  char *before = f_read(private, 2000000);
  assert(before);
  json_object *v = f_parse(original);
  f_string_add(f_field(v, "source_snapshot"), "scope", "changed scope");
  nt_json_write(inventory, v);
  json_object_put(v);
  dc_error(dc_cli("qualify", args, false), "progress_binding_changed");
  nt_write(inventory, original);
  char *config = f_read(dc_included, 100000),
       *changed = nt_replace(config, "User builder", "User changed");
  nt_write(dc_included, changed);
  free(changed);
  dc_error(dc_cli("qualify", args, false), "progress_binding_changed");
  char *after = f_read(private, 2000000);
  assert(after && !strcmp(before, after));
  free(after);
  nt_write(dc_included, config);
  free(config);
  v = f_parse(original);
  json_object_object_add(f_field(v, "source_snapshot"), "records",
                         json_object_new_array());
  nt_json_write(inventory, v);
  json_object_put(v);
  dc_error(dc_cli("qualify", args, false), "invalid_inventory");
  after = f_read(private, 2000000);
  assert(after && !strcmp(before, after));
  free(after);
  nt_write(inventory, original);
  nt_write(private, "malformed");
  dc_error(dc_cli("qualify", args, false), "progress_invalid");
  assert(dc_call_count() == 1);
  free(original);
  free(before);
  globfree(&paths);
}
static void locking(void) {
  char progress[F_PATH];
  nt_path(progress, dc_root, "progress.json");
  const char *args[] = {"--ssh",      "slow",   "--timeout", "1",
                        "--progress", progress, NULL};
  struct nt_child child = dc_start("qualify", args);
  nt_await_file(dc_pid, 10);
  dc_error(dc_cli("qualify", args, false), "progress_busy");
  struct f_capture c = nt_wait(&child, 5);
  json_object *v = nt_output(&c, 1),
              *row = json_object_array_get_idx(dc_rows(v), 0);
  nt_equal(f_field(row, "qualification_attempts"), "1");
  json_object_put(v);
  f_capture_free(&c);
  v = dc_cli("qualify", args, false);
  row = json_object_array_get_idx(dc_rows(v), 0);
  nt_equal(f_field(row, "qualification_attempts"), "2");
  json_object *history = f_field(row, "qualification_history");
  assert(json_object_array_length(history) == 1);
  assert(!strcmp(
      f_string(f_field(f_field(json_object_array_get_idx(history, 0), "result"),
                       "error"),
               "code"),
      "timeout"));
  json_object_put(v);
}
static void cancellation(void) {
  const char *args[] = {"--ssh",     "slow", "--ssh", "zzz",
                        "--timeout", "30",   NULL};
  struct nt_child child = dc_start("qualify", args);
  nt_await_file(dc_pid, 10);
  assert(!kill(child.pid, SIGTERM));
  struct f_capture c = nt_wait(&child, 5);
  json_object *v = nt_output(&c, 1), *rows = dc_rows(v);
  assert(json_object_array_length(rows) == 2);
  const char *names[] = {"slow", "zzz"};
  for (size_t i = 0; i < 2; i++) {
    json_object *r = json_object_array_get_idx(rows, i);
    assert(
        !strcmp(f_string(r, "target"), names[i]) &&
        !strcmp(f_string(f_field(f_field(r, "qualification"), "error"), "code"),
                "cancelled"));
  }
  char *pid = f_read(dc_pid, 100);
  assert(pid);
  errno = 0;
  assert(kill((pid_t)strtol(pid, NULL, 10), 0) < 0 && errno == ESRCH);
  free(pid);
  json_object_put(v);
  f_capture_free(&c);
}
void dc_progress_cases(void) {
  void (*cases[])(void) = {batches, binding, locking, cancellation};
  for (size_t i = 0; i < 4; i++) {
    dc_setup();
    cases[i]();
    dc_cleanup();
    printf("Discovery progress case %zu passed\n", i + 1);
  }
}
