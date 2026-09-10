#include "enrollment.h"
#include <dirent.h>
#include <signal.h>
static size_t enrolled(json_object *v) {
  json_object *hosts = f_field(v, "hosts");
  size_t n = 0;
  for (size_t i = 0; i < json_object_array_length(hosts); i++)
    if (!strcmp(f_string(json_object_array_get_idx(hosts, i), "status"),
                "enrolled"))
      n++;
  return n;
}
static void interrupt_at(struct nt_child *child, const char *hold) {
  char started[F_PATH];
  assert(snprintf(started, sizeof started, "%s.started", hold) > 0);
  /* Resume revalidates all fifty SSH configurations before the held handshake.
   * Instrumented fixture startup can dominate that setup; this is a readiness
   * bound, not a latency assertion. Cancellation below retains its own limit. */
  nt_await_file(started, 120);
  assert(!kill(child->pid, SIGTERM));
  nt_write(hold, "");
  struct f_capture c = nt_wait(child, 20);
  assert(*c.out && !c.timeout);
  f_capture_free(&c);
}
void ec_batch_case(void) {
  char inventory[F_PATH], config[F_PATH], progress[F_PATH], targets[50][32],
      hold[F_PATH];
  nt_path(inventory, ec_root, "inventory.json");
  nt_path(config, ec_root, "ssh-config");
  nt_path(progress, ec_root, "qualification-progress.json");
  nt_write(config, "Host *\n HostName %h\n User tester\n");
  json_object *document =
      f_parse("{\"schema_version\":1,\"observed_at\":1,\"hosts\":[]}");
  const char *args[110];
  args[0] = "qualify";
  args[1] = "--inventory";
  args[2] = inventory;
  args[3] = "--ssh-config";
  args[4] = config;
  args[5] = "--require";
  args[6] = "list";
  args[7] = "--progress";
  args[8] = progress;
  for (size_t i = 0; i < 50; i++) {
    assert(snprintf(targets[i], sizeof targets[i], "host%zu", i) > 0);
    json_object *r = f_parse("{\"labels\":[]}");
    f_string_add(r, "name", targets[i]);
    f_string_add(r, "target", targets[i]);
    json_object_array_add(f_field(document, "hosts"), r);
    args[9 + 2 * i] = "--select";
    args[10 + 2 * i] = targets[i];
  }
  args[109] = NULL;
  nt_json_write(inventory, document);
  json_object_put(document);
  json_object *v = ec_cli(false, args, true);
  nt_equal(f_field(f_field(v, "data"), "processed_this_batch"), "16");
  nt_equal(f_field(f_field(v, "data"), "remaining_count"), "34");
  json_object_put(v);
  nt_path(hold, ec_root, "qualify-hold");
  assert(!setenv("ENROLL_HOLD_QUALIFY", hold, 1));
  struct nt_child child = ec_start(false, args);
  interrupt_at(&child, hold);
  assert(!unsetenv("ENROLL_HOLD_QUALIFY"));
  v = NULL;
  for (size_t i = 0; i < 5; i++) {
    child = ec_start(false, args);
    struct f_capture c = nt_wait(&child, 120);
    assert(c.status == 0 || c.status == 1);
    if (v)
      json_object_put(v);
    v = f_parse(c.out);
    assert(v);
    f_capture_free(&c);
    assert(json_object_get_int(
               f_field(f_field(v, "data"), "processed_this_batch")) <= 16);
    if (json_object_get_boolean(f_field(f_field(v, "data"), "complete")))
      break;
  }
  nt_equal(f_field(f_field(v, "data"), "complete"), "true");
  char q[F_PATH], intent[F_PATH], digest[65];
  nt_path(q, ec_root, "qualified.json");
  nt_json_write(q, v);
  json_object *candidates = json_object_new_array(),
              *rows = f_field(f_field(v, "data"), "candidates");
  for (size_t i = 0; i < json_object_array_length(rows); i++)
    json_object_array_add(
        candidates, json_object_new_string(f_string(
                        json_object_array_get_idx(rows, i), "candidate_id")));
  ec_review(intent, digest, q, candidates, NULL, NULL);
  json_object_put(candidates);
  json_object_put(v);
  char drop[F_PATH];
  nt_path(drop, ec_root, "dropped-init");
  assert(!setenv("ENROLL_DROP_INIT_RESPONSE", drop, 1));
  v = ec_apply(intent, digest);
  assert(enrolled(v) == 15);
  assert(!strcmp(
      f_string(json_object_array_get_idx(f_field(v, "hosts"), 0), "status"),
      "outcome_unknown"));
  json_object_put(v);
  v = ec_apply(intent, digest);
  assert(enrolled(v) == 31);
  json_object_put(v);
  nt_path(hold, ec_root, "apply-hold");
  assert(!setenv("ENROLL_HOLD_PREFLIGHT", hold, 1));
  const char *apply[] = {"apply", "--input", intent, "--confirm", digest, NULL};
  child = ec_start(true, apply);
  interrupt_at(&child, hold);
  assert(!unsetenv("ENROLL_HOLD_PREFLIGHT"));
  v = ec_apply(intent, digest);
  for (size_t i = 0; i < 3 && enrolled(v) != 50; i++) {
    json_object_put(v);
    v = ec_apply(intent, digest);
  }
  assert(enrolled(v) == 50);
  json_object *duplicate = ec_apply(intent, digest);
  assert(json_object_equal(f_field(duplicate, "hosts"), f_field(v, "hosts")));
  json_object_put(duplicate);
  json_object_put(v);
  ec_count("mutations", "init\n", 50);
  for (size_t i = 0; i < 50; i++) {
    char relative[160], dir[F_PATH];
    assert(snprintf(relative, sizeof relative,
                    "receivers/%s/fleet/enrollment-ops", targets[i]) > 0);
    nt_path(dir, ec_root, relative);
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t count = 0;
    assert(d);
    while ((e = readdir(d))) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
        continue;
      char p[F_PATH];
      nt_path(p, dir, e->d_name);
      v = f_read_json(p, 1000000);
      assert(v && !strcmp(f_string(v, "state"), "completed"));
      json_object_put(v);
      count++;
    }
    assert(!closedir(d) && count == 1);
  }
  puts("Enrollment fifty-host interrupted batches passed");
}
