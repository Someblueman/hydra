#include "support.h"
#include "fleet/placement/placement.h"
/* `workflow-task remote-tsv`: remote rows come only from the records the
 * protocol wrote. Telemetry fields are "-" unless the receipt carries them,
 * placement is auto only with a recording, and an unverifiable receipt leaves
 * everything but the binding unknown. */
static const char *fleet;
static char root[F_PATH], step[F_PATH], remote[F_PATH], home[F_PATH];
#define TASK "task_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define RECEIPT "{\"task_id\":\"" TASK "\",\"spec_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"," \
  "\"submission_key\":\"fixture\",\"assignment_generation\":2,\"runtime\":{\"schema_version\":1,\"state\":\"succeeded\"," \
  "\"launch_intent\":\"started\",\"run_id\":\"run_receiver\",\"exit_status\":0}}"
#define TELEMETRY "{\"schema_version\":1,\"measured_by\":\"task_owner\",\"scope\":\"execution_phase\",\"clock\":\"monotonic\"," \
  "\"cpu_source\":\"getrusage_children\",\"rss_source\":\"getrusage_children_maxrss\",\"invocations\":1,\"wall_seconds\":12," \
  "\"cpu_user_microseconds\":300000,\"cpu_system_microseconds\":50000,\"unmeasured\":{\"peak_rss_bytes\":\"rss_not_distinguishable\"}}"
static void write_at(const char *base, const char *name, const char *text) {
  char p[F_PATH];
  nt_path(p, base, name);
  nt_write(p, text);
}
static void observation(const char *telemetry) {
  json_object *v = f_parse(RECEIPT);
  char p[F_PATH];
  if (telemetry) json_object_object_add(f_field(v, "runtime"), "telemetry", f_parse(telemetry));
  nt_path(p, remote, "observation.json");
  nt_json_write(p, v);
  json_object_put(v);
}
static void setup(void) {
  char tmp[] = "/tmp/hydra-native-cohorts-XXXXXX", p[F_PATH];
  assert(mkdtemp(tmp));
  assert(!f_copy(root, sizeof root, tmp));
  nt_path(home, root, "home");
  assert(!f_mkdirs(home));
  f_home = home;
  assert(!setenv("HYDRA_HOME", home, 1));
  nt_path(step, root, "steps/work");
  nt_path(remote, step, "attempt-1/remote");
  assert(!f_mkdirs(remote));
  write_at(root, "run-id", "run_fixture\n");
  write_at(root, "tasks.json",
           "{\"schema_version\":1,\"steps\":{\"work\":{\"destination\":{\"kind\":\"ssh\",\"alias\":\"alpha\"}},"
           "\"local\":{\"destination\":{\"kind\":\"local\"}}}}");
  write_at(step, "attempts", "1\n");
  nt_path(p, remote, "receipt.json");
  nt_write(p, RECEIPT);
  observation(TELEMETRY);
  write_at(remote, "transport-metrics.json", "{\"schema_version\":1,\"calls\":2,\"request_bytes\":10,\"response_bytes\":20,\"complete\":true}");
}
static char *rows(void) {
  char *args[] = {(char *)fleet, "workflow-task", "remote-tsv", root, NULL};
  struct f_capture c = nt_run(args);
  char *out;
  assert(!c.status);
  out = strdup(c.out);
  assert(out);
  f_capture_free(&c);
  return out;
}
static void expect(const char *line) {
  char *out = rows();
  if (!strstr(out, line)) fprintf(stderr, "expected %s\n got %s\n", line, out);
  assert(strstr(out, line));
  free(out);
}
static void clear(void) { assert(!f_remove_tree(root)); }
int main(void) {
  char p[F_PATH], *out;
  json_object *record;
  umask(077);
  fleet = getenv("HYDRA_FLEET_BIN");
  if (!fleet) fleet = "build/hydra-fleet";
  setup();
  /* A workflow binding pins its host; the receiver's generation is recorded. */
  expect("remote\twork\talpha\tssh\tpinned\t2\tsucceeded\t12\t350000\t-\t2\t30\tpeak_rss_bytes=rss_not_distinguishable\n");
  out = rows();
  assert(!strstr(out, "\tlocal\t") && !strstr(out, "usage\t"));
  free(out);
  /* A placement record with mode auto names the cohort; its generation is used
   * only when the receipt carries none. */
  record = f_parse("{\"schema_version\":1,\"task_id\":\"" TASK "\",\"submission_key\":\"fixture\",\"mode\":\"auto\",\"generation\":3,\"assigned_host\":\"alpha\"}");
  assert(!placement_record_write(record));
  json_object_put(record);
  expect("remote\twork\talpha\tssh\tauto\t2\tsucceeded\t12\t350000\t-\t2\t30\t");
  observation("{\"schema_version\":1,\"wall_seconds\":4,\"cpu_user_microseconds\":1,\"cpu_system_microseconds\":2,\"peak_rss_bytes\":4096,\"unmeasured\":{}}");
  out = rows();
  assert(strstr(out, "\tauto\t2\tsucceeded\t4\t3\t4096\t2\t30\t-\n"));
  free(out);
  /* Without a telemetry block every measurement is unknown with the reason. */
  observation(NULL);
  expect("\tsucceeded\t-\t-\t-\t2\t30\tno_telemetry_block\n");
  /* A telemetry block of another schema is not read. */
  observation("{\"schema_version\":2,\"wall_seconds\":4}");
  expect("\tsucceeded\t-\t-\t-\t2\t30\tno_telemetry_block\n");
  /* Incomplete transport metrics never establish bytes. */
  write_at(remote, "transport-incomplete", "incomplete\n");
  observation(TELEMETRY);
  expect("\tsucceeded\t12\t350000\t-\t-\t-\tpeak_rss_bytes=rss_not_distinguishable\n");
  /* An observation that differs from the receipt leaves only the binding. */
  nt_path(p, remote, "observation.json");
  nt_write(p, "{\"task_id\":\"task_cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}");
  expect("remote\twork\talpha\tssh\tpinned\t-\t-\t-\t-\t-\t-\t-\treceipt_unverified\n");
  /* A step whose latest attempt has no remote directory is not a remote row. */
  write_at(step, "attempts", "2\n");
  out = rows();
  assert(!*out);
  free(out);
  clear();
  puts("Workflow task cohorts: binding, placement, telemetry, transport and unverified receipt cases passed");
  return 0;
}
