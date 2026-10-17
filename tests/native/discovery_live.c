#include "discovery.h"
#include <sys/stat.h>
/* Opt-in live sources (H4) and revoked identities in discovery (H6):
 * ssh-config scans, Ansible INI files, the dns-sd adapter over a fixture
 * avahi-browse, snapshot diffs, stale caches, partial output, source
 * disappearance, identity conflicts and revocation provenance. Nothing here
 * contacts a real host or network. */
static char live_bin[F_PATH], hosts[F_PATH], selector[F_PATH];
static json_object *source0(json_object *v) {
  json_object *s = f_field(f_field(v, "data"), "sources");
  assert(json_object_array_length(s) == 1);
  return json_object_array_get_idx(s, 0);
}
static void status_is(json_object *s, const char *status, const char *reason) {
  assert(!strcmp(f_string(s, "status"), status));
  if (reason)
    assert(!strcmp(f_string(s, "reason"), reason));
}
static size_t cache_files(void) {
  char dir[F_PATH];
  nt_path(dir, dc_home, "fleet/discovery/sources");
  char *list[] = {"sh", "-c", "ls \"$0\" | wc -l", dir, NULL};
  struct f_capture c = nt_run(list);
  size_t n = (size_t)atoi(c.out);
  f_capture_free(&c);
  return n;
}
static json_object *revoke_identity(const char *scope, const char *id) {
  char *argv[] = {dc_hydra, "fleet", "revoke", (char *)scope, (char *)id,
                  "--reason", "fixture decision", "--json", NULL};
  struct f_capture c = nt_run(argv);
  json_object *v = nt_output(&c, 0);
  f_capture_free(&c);
  return v;
}
static void ssh_scan(void) {
  assert(snprintf(selector, sizeof selector, "ssh-config:%s", dc_config) > 0);
  json_object *v = DC("discover", true, "--source", selector),
              *rows = dc_rows(v), *s = source0(v);
  assert(json_object_array_length(rows) == 3);
  const char *names[] = {"good", "jump", "other"};
  for (size_t i = 0; i < 3; i++) {
    json_object *r = json_object_array_get_idx(rows, i),
                *src = json_object_array_get_idx(f_field(r, "sources"), 0);
    assert(!strcmp(f_string(r, "target"), names[i]) &&
           !strcmp(f_string(r, "status"), "discovered"));
    assert(!strcmp(f_string(src, "kind"), "ssh-config") &&
           !strcmp(f_string(src, "locator"), dc_config) &&
           !strcmp(f_string(src, "freshness"), "acquired_now") &&
           !strncmp(f_string(src, "scope"), "account:", 8));
  }
  status_is(s, "acquired", NULL);
  nt_equal(f_field(s, "record_count"), "3");
  nt_equal(f_field(s, "cache"), "null");
  nt_equal(f_field(f_field(s, "diff"), "first_observation"), "true");
  assert(!f_field(s, "records"));
  json_object_put(v);
  assert(cache_files() == 1);
  v = DC("discover", true, "--source", selector, "--diff");
  s = source0(v);
  assert(json_object_get_int64(f_field(f_field(s, "cache"), "age_seconds")) >=
         0);
  nt_equal(f_field(f_field(s, "cache"), "record_count"), "3");
  nt_equal(f_field(f_field(s, "diff"), "added"), "[]");
  nt_equal(f_field(f_field(s, "diff"), "removed_count"), "0");
  json_object_put(v);
  FILE *f = fopen(dc_included, "a");
  assert(f && fputs("Host added pat*\n HostName added.invalid\n", f) >= 0 &&
         !fclose(f));
  v = DC("discover", true, "--source", selector, "--diff", "--select",
         "added");
  s = source0(v);
  nt_equal(f_field(f_field(s, "diff"), "added"), "[\"added\"]");
  assert(json_object_array_length(dc_rows(v)) == 1 &&
         !strcmp(f_string(json_object_array_get_idx(dc_rows(v), 0), "target"),
                 "added"));
  bool pattern = false;
  json_object *skipped = f_field(s, "skipped");
  for (size_t i = 0; i < json_object_array_length(skipped); i++)
    if (!strcmp(f_string(json_object_array_get_idx(skipped, i), "reason"),
                "pattern"))
      pattern = true;
  assert(pattern);
  json_object_put(v);
  dc_error(DC("qualify", false, "--source", selector), "invalid_input");
  dc_error(DC("discover", false, "--source", selector, "--select", "nothere"),
           "invalid_inventory");
  dc_error(DC("discover", false, "--source", "ssh-config:relative"),
           "invalid_input");
  dc_error(DC("discover", false, "--source", selector, "--source", selector),
           "invalid_input");
  v = DC("qualify", true, "--source", selector, "--select", "good");
  assert(json_object_array_length(dc_rows(v)) == 1 &&
         !strcmp(f_string(json_object_array_get_idx(dc_rows(v), 0), "status"),
                 "compatible"));
  json_object_put(v);
  assert(dc_call_count() == 1);
}
static void ansible(void) {
  nt_path(hosts, dc_root, "hosts.ini");
  assert(snprintf(selector, sizeof selector, "ansible-inventory:%s", hosts) >
         0);
  nt_write(hosts, "# inventory\nmail.invalid\n[web]\nweb1 ansible_host=good "
                  "ansible_user=builder ansible_port=2222\nweb2 "
                  "ansible_host=other ansible_ssh_pass=secret-test-token\n"
                  "www[01:09].invalid\nlocalbox ansible_connection=local\n"
                  "[db]\ndb1 ansible_host=good ansible_user=builder\nweb2 "
                  "ansible_host=other\n[db:vars]\nansible_password=secret-"
                  "test-token\n[all:children]\nweb\ndb\n");
  json_object *v = DC("discover", true, "--source", selector, "--diff"),
              *s = source0(v), *rows = dc_rows(v);
  status_is(s, "acquired", NULL);
  nt_equal(f_field(s, "credentials_omitted"), "1");
  nt_equal(f_field(s, "sections_ignored"), "2");
  nt_equal(f_field(f_field(s, "diff"), "conflicting_count"), "2");
  assert(json_object_array_length(rows) == 2);
  assert(!strcmp(f_string(json_object_array_get_idx(rows, 0), "target"),
                 "mail.invalid") &&
         !strcmp(f_string(json_object_array_get_idx(rows, 1), "target"),
                 "other"));
  json_object *labels = f_field(
      json_object_array_get_idx(
          f_field(json_object_array_get_idx(rows, 1), "sources"), 0),
      "labels");
  nt_equal(labels, "[\"web\",\"db\"]");
  json_object_put(v);
  v = DC("discover", false, "--source", selector, "--select", "web1");
  dc_error(v, "invalid_inventory");
  nt_write(hosts, "[web]\nweb1 ansible_host=other ansible_user=builder\n"
                  "web3 ansible_host=third\n");
  v = DC("discover", true, "--source", selector, "--diff");
  s = source0(v);
  json_object *d = f_field(s, "diff");
  nt_equal(f_field(d, "added"), "[\"web3\"]");
  nt_equal(f_field(d, "removed"), "[\"mail.invalid\",\"web2\",\"db1\"]");
  assert(json_object_array_length(f_field(d, "changed")) == 1);
  nt_equal(f_field(json_object_array_get_idx(f_field(d, "changed"), 0),
                   "previous_target"),
           "\"builder@good\"");
  nt_equal(f_field(d, "conflicting"), "[]");
  assert(json_object_array_length(dc_rows(v)) == 2);
  json_object_put(v);
  nt_write(hosts, "[web]\nweb1 ansible_host=third\nweb3 ansible_host=fourth\n");
  v = DC("discover", true, "--source", selector, "--diff");
  d = f_field(source0(v), "diff");
  nt_equal(f_field(json_object_array_get_idx(f_field(d, "conflicting"), 0),
                   "reason"),
           "\"target_reassigned\"");
  assert(json_object_array_length(dc_rows(v)) == 1);
  json_object_put(v);
  assert(!unlink(hosts));
  v = DC("discover", false, "--source", selector);
  s = source0(v);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "source_unavailable"));
  status_is(s, "unavailable", "file_missing");
  nt_equal(f_field(f_field(s, "cache"), "status"), "\"stale\"");
  nt_equal(f_field(f_field(s, "cache"), "record_count"), "2");
  assert(!json_object_array_length(dc_rows(v)));
  json_object_put(v);
  dc_error(DC("discover", false, "--source", selector, "--select", "web3"),
           "invalid_inventory");
  assert(cache_files() == 1);
}
static void client(const char *body) {
  if (!body) {
    unlink(live_bin);
    return;
  }
  char text[4096];
  assert(snprintf(text, sizeof text, "#!/bin/sh\n%s\n", body) > 0);
  nt_write(live_bin, text);
  assert(!chmod(live_bin, 0700));
}
static void dnssd(void) {
  nt_path(live_bin, dc_root, "bin/avahi-browse");
  json_object *v = DC("discover", false, "--source", "dns-sd"), *s = source0(v);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "source_unavailable"));
  status_is(s, "unavailable", "client_missing");
  assert(!strncmp(f_string(s, "scope"), "network:local", 13) &&
         !json_object_array_length(dc_rows(v)));
  nt_equal(f_field(s, "cache"), "null");
  json_object_put(v);
  assert(!cache_files());
  client("[ \"$1\" = -rtpk ] && [ \"$4\" = _ssh._tcp ] || exit 9\n"
         "printf '=;eth0;IPv4;build\\\\032box;_ssh._tcp;local;good;192.0.2.1;"
         "22;\\n=;eth0;IPv6;build\\\\032box;_ssh._tcp;local;good;fe80::1;22;\\n"
         "=;eth0;IPv4;printer;_ssh._tcp;local;other;192.0.2.2;2222;\\n"
         "=;eth0;IPv4;web;_http._tcp;local;web;192.0.2.3;80;\\n'");
  v = DC("discover", true, "--source", "dns-sd", "--diff");
  s = source0(v);
  status_is(s, "acquired", NULL);
  nt_equal(f_field(s, "record_count"), "1");
  nt_equal(f_field(f_field(s, "diff"), "added"), "[\"build box\"]");
  assert(json_object_array_length(dc_rows(v)) == 1);
  json_object *src = json_object_array_get_idx(
      f_field(json_object_array_get_idx(dc_rows(v), 0), "sources"), 0);
  assert(!strcmp(f_string(src, "name"), "build box") &&
         !strcmp(f_string(src, "kind"), "dns-sd"));
  nt_equal(f_field(src, "labels"),
           "[\"mdns\",\"IPv4\",\"interface:eth0\",\"domain:local\",\"IPv6\"]");
  assert(json_object_array_length(f_field(s, "skipped")) == 2);
  json_object_put(v);
  client("printf '=;eth0;IPv4;late;_ssh._tcp;local;other;192.0.2.2;22;\\n'; "
         "exit 3");
  v = DC("discover", true, "--source", "dns-sd", "--diff");
  s = source0(v);
  status_is(s, "partial", "client_failed");
  assert(!json_object_array_length(dc_rows(v)));
  nt_equal(f_field(f_field(s, "diff"), "removed"), "[\"build box\"]");
  json_object_put(v);
  v = DC("discover", true, "--source", "dns-sd", "--select", "late");
  assert(json_object_array_length(dc_rows(v)) == 1 &&
         !strcmp(f_string(json_object_array_get_idx(
                              f_field(json_object_array_get_idx(dc_rows(v), 0),
                                      "sources"),
                              0),
                          "freshness"),
                 "partial_acquisition"));
  json_object_put(v);
  client("sleep 5");
  v = DC("discover", false, "--source", "dns-sd", "--timeout", "1");
  s = source0(v);
  status_is(s, "unavailable", "timeout");
  nt_equal(f_field(f_field(s, "cache"), "status"), "\"stale\"");
  nt_equal(f_field(f_field(s, "cache"), "record_count"), "1");
  json_object_put(v);
  client(NULL);
  v = DC("discover", false, "--source", "dns-sd:local");
  status_is(source0(v), "unavailable", "client_missing");
  nt_equal(f_field(f_field(source0(v), "cache"), "record_count"), "1");
  json_object_put(v);
  dc_error(DC("discover", false, "--source", "dns-sd:Bad Domain"),
           "invalid_input");
}
static void revoked(void) {
  json_object *v = revoke_identity("candidate", "good");
  assert(!strcmp(f_string(f_field(v, "data"), "id"), "cand_676f6f64"));
  json_object_put(v);
  v = DC("qualify", true, "--ssh", "good", "--ssh", "other");
  json_object *r = json_object_array_get_idx(dc_rows(v), 0);
  assert(!strcmp(f_string(r, "target"), "good") &&
         !strcmp(f_string(r, "status"), "revoked") && !f_field(r, "resolution") &&
         !strcmp(f_string(f_field(r, "revocation"), "reason"),
                 "fixture decision"));
  assert(!strcmp(f_string(json_object_array_get_idx(dc_rows(v), 1), "status"),
                 "compatible"));
  nt_equal(f_field(f_field(v, "data"), "revoked_count"), "1");
  nt_equal(f_field(f_field(v, "data"), "complete"), "true");
  json_object_put(v);
  assert(dc_call_count() == 1);
  char file[F_PATH];
  nt_path(file, dc_root, "inventory.json");
  nt_write(file, "{\"schema_version\":1,\"observed_at\":1,\"hosts\":[{\"name\":"
                 "\"again\",\"target\":\"good\",\"labels\":[]}]}");
  v = DC("discover", true, "--inventory", file, "--select", "again");
  assert(!strcmp(f_string(json_object_array_get_idx(dc_rows(v), 0), "status"),
                 "revoked"));
  json_object_put(v);
  nt_path(hosts, dc_root, "hosts.ini");
  nt_write(hosts, "[web]\nw ansible_host=good\nx ansible_host=other\n");
  assert(snprintf(selector, sizeof selector, "ansible-inventory:%s", hosts) >
         0);
  v = DC("discover", true, "--source", selector);
  json_object *s = source0(v);
  assert(json_object_array_length(f_field(s, "revoked")) == 1 &&
         !strcmp(f_string(json_object_array_get_idx(f_field(s, "revoked"), 0),
                          "name"),
                 "w"));
  assert(json_object_array_length(dc_rows(v)) == 1);
  json_object_put(v);
  dc_error(DC("discover", false, "--source", selector, "--select", "w"),
           "invalid_inventory");
  json_object_put(revoke_identity("source", selector));
  v = DC("discover", false, "--source", selector);
  assert(!strcmp(f_string(f_field(v, "error"), "code"), "revoked"));
  s = source0(v);
  status_is(s, "revoked", "source_revoked");
  assert(f_field(s, "revocation") && !json_object_array_length(dc_rows(v)));
  json_object_put(v);
}
static void progress_revoked(void) {
  char file[F_PATH], progress[F_PATH];
  nt_path(progress, dc_root, "progress.json");
  json_object *records = f_parse_value(
      "[{\"host\":\"a\",\"address\":\"good\",\"labels\":[]},{\"host\":\"b\","
      "\"address\":\"other\",\"labels\":[]}]");
  dc_snapshot(file, "config-management", records, 1);
  json_object *v = DC("qualify", true, "--inventory", file, "--select", "a",
                      "--select", "b", "--progress", progress);
  nt_equal(f_field(f_field(v, "data"), "complete"), "true");
  json_object_put(v);
  assert(dc_call_count() == 2);
  json_object_put(revoke_identity("candidate", "cand_676f6f64"));
  v = DC("qualify", true, "--inventory", file, "--select", "a", "--select",
         "b", "--progress", progress);
  json_object *r = json_object_array_get_idx(dc_rows(v), 0);
  assert(!strcmp(f_string(r, "status"), "revoked") &&
         json_object_get_boolean(f_field(f_field(r, "qualification"), "ok")) &&
         !strcmp(f_string(r, "qualification_freshness"),
                 "retained; identity revoked"));
  nt_equal(f_field(f_field(v, "data"), "processed_this_batch"), "0");
  json_object_put(v);
  assert(dc_call_count() == 2);
}
static void key_changed(void) {
  char p[F_PATH];
  nt_path(p, dc_home, "fleet/remotes/old.json");
  nt_write(p, "{\"schema_version\":1,\"target\":\"changed\",\"hydra\":"
              "\"hydra\",\"home\":\"\",\"accepted_host_key\":\"SHA256:"
              "oldfixturekey\",\"multiplex\":false}");
  nt_path(p, dc_home, "fleet/remotes/rotated.json");
  nt_write(p, "{\"schema_version\":1,\"target\":\"changed\",\"hydra\":"
              "\"hydra\",\"home\":\"\",\"accepted_host_key\":\"SHA256:"
              "revokedfixturekey\",\"multiplex\":false}");
  json_object_put(revoke_identity("transport", "rotated"));
  json_object *v = DC("qualify", false, "--ssh", "changed"),
              *e = f_field(f_field(json_object_array_get_idx(dc_rows(v), 0),
                                   "qualification"),
                           "error"),
              *k = f_field(e, "host_key");
  assert(!strcmp(f_string(e, "code"), "host_key_changed"));
  nt_equal(f_field(k, "presented_fingerprint"),
           "\"SHA256:newfixturekey0123456789abcdefghijklmnopqrstuv\"");
  nt_equal(f_field(k, "key_type"), "\"ED25519\"");
  nt_equal(f_field(k, "known_hosts"), "\"/fixture/known_hosts\"");
  nt_equal(f_field(k, "known_hosts_line"), "3");
  json_object *recorded = f_field(k, "recorded");
  assert(json_object_array_length(recorded) == 2);
  nt_equal(f_field(json_object_array_get_idx(recorded, 0), "fingerprint"),
           "\"SHA256:oldfixturekey\"");
  nt_equal(f_field(json_object_array_get_idx(recorded, 0), "source"),
           "\"alias:old accepted_host_key\"");
  nt_equal(f_field(json_object_array_get_idx(recorded, 1), "fingerprint"),
           "\"SHA256:revokedfixturekey\"");
  assert(strstr(f_string(k, "acceptance"), "ssh-keygen -R") &&
         !strstr(json_object_to_json_string(v), "secret-test-token"));
  json_object_put(v);
  char *after = f_read(p, 100000);
  assert(after && strstr(after, "revokedfixturekey"));
  free(after);
}
void dc_live_cases(void) {
  void (*cases[])(void) = {ssh_scan, ansible, dnssd,
                           revoked,  progress_revoked, key_changed};
  const char *names[] = {"ssh-config scan", "ansible inventory", "dns-sd",
                         "revoked identities", "progress under revocation",
                         "changed host key"};
  for (size_t i = 0; i < 6; i++) {
    dc_setup();
    cases[i]();
    dc_cleanup();
    printf("Discovery live case %zu (%s) passed\n", i + 1, names[i]);
  }
}
