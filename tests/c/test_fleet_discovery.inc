/* Live-source parsers, snapshot diffs and revocation identities: pure
 * functions over fixture text, no files beyond the private test home. */
#include "fleet/discovery/discovery.h"
#include "fleet/enrollment/enrollment.h"
#include "fleet/revocation/revocation.h"
static json_object *record_named(json_object *report, const char *name) {
    json_object *records = f_field(report, "records"); size_t i;
    for (i = 0; i < json_object_array_length(records); i++)
        if (!strcmp(f_string(json_object_array_get_idx(records, i), "name"), name)) return json_object_array_get_idx(records, i);
    return NULL;
}
static void test_ansible_parser(void) {
    json_object *report = hd_parse_ansible(
        "plain.invalid\n[web]\nw1 ansible_host=10.0.0.1 ansible_user=u ansible_port=2222 # trailing\n"
        "w2 ansible_ssh_pass=hidden-secret ansible_host=10.0.0.2\nrange[1:3].invalid\nlocal ansible_connection=local\n"
        "[db]\nw1 ansible_host=10.0.0.1 ansible_user=u\nd1 ansible_host=10.0.0.1 ansible_user=u\n[db:vars]\nansible_become_pass=x\n"), *w1;
    assert(!strcmp(f_string(report, "status"), "acquired"));
    assert(json_object_get_int64(f_field(report, "credentials_omitted")) == 1);
    assert(json_object_get_int64(f_field(report, "sections_ignored")) == 1);
    assert(!strstr(json_object_to_json_string(report), "hidden-secret"));
    w1 = record_named(report, "w1");
    assert(w1 && !strcmp(f_string(w1, "target"), "u@10.0.0.1") && !strcmp(f_string(w1, "conflict"), "duplicate_target"));
    assert(json_object_array_length(f_field(w1, "labels")) == 3);
    assert(record_named(report, "plain.invalid") && !strcmp(f_string(record_named(report, "w2"), "target"), "10.0.0.2"));
    assert(!record_named(report, "local") && !record_named(report, "range[1:3].invalid"));
    assert(json_object_array_length(f_field(report, "skipped")) == 2);
    json_object_put(report);
}
static void test_avahi_parser(void) {
    json_object *report = hd_parse_avahi(
        "+;eth0;IPv4;box;_ssh._tcp;local\n=;eth0;IPv4;my\\032box;_ssh._tcp;local;box.local;192.0.2.1;22;\n"
        "=;eth0;IPv4;other;_ssh._tcp;local;other.local;192.0.2.2;2200;\n=;eth0;IPv4;bad\\999;_ssh._tcp;local;x.local;1;22;\n"), *box;
    box = record_named(report, "my box");
    assert(box && !strcmp(f_string(box, "target"), "box.local") && json_object_array_length(f_field(report, "records")) == 1);
    assert(json_object_array_length(f_field(report, "skipped")) == 2);
    json_object_put(report);
}
static void test_ssh_scanner(void) {
    json_object *report = hd_report("acquired", NULL);
    json_object_object_add(report, "files", json_object_new_int64(1));
    hd_parse_ssh_hosts("# comment\nHost build other build\n HostName b\nhost=lower\nHost pat* !neg\nMatch host x\nInclude \"/nonexistent/*.conf\"\n", "/f", report, 0);
    assert(json_object_array_length(f_field(report, "records")) == 3 && record_named(report, "lower"));
    assert(json_object_array_length(f_field(report, "skipped")) == 4);
    json_object_put(report);
}
static void test_diff(void) {
    json_object *previous = f_parse("{\"records\":[{\"name\":\"a\",\"target\":\"t1\",\"labels\":[]},{\"name\":\"b\",\"target\":\"t2\",\"labels\":[\"x\"]},{\"name\":\"gone\",\"target\":\"t3\",\"labels\":[]}]}");
    json_object *report = hd_report("acquired", NULL), *diff;
    hd_record_add(report, "a", "t2", NULL);
    hd_record_add(report, "b", "t9", NULL);
    hd_record_add(report, "new", "t4", NULL);
    hd_live_diff(report, previous);
    diff = f_field(report, "diff");
    assert(json_object_array_length(f_field(diff, "added")) == 1 && json_object_array_length(f_field(diff, "removed")) == 1);
    assert(json_object_array_length(f_field(diff, "changed")) == 2);
    assert(json_object_array_length(f_field(diff, "conflicting")) == 1 &&
           !strcmp(f_string(json_object_array_get_idx(f_field(diff, "conflicting"), 0), "reason"), "target_reassigned"));
    json_object_put(report); json_object_put(previous);
    report = hd_report("acquired", NULL);
    hd_live_diff(report, NULL);
    assert(json_object_get_boolean(f_field(f_field(report, "diff"), "first_observation")) && !f_field(report, "cache"));
    json_object_put(report);
}
static void test_revocation_identity(void) {
    char out[REVOCATION_ID]; json_object *result, *record;
    assert(revocation_normalize("candidate", "build", out) && !strcmp(out, "cand_6275696c64"));
    assert(revocation_normalize("candidate", "cand_6275696c64", out) && !strcmp(out, "cand_6275696c64"));
    assert(!revocation_normalize("candidate", "cand_zz", out) && !revocation_normalize("candidate", "-evil", out));
    assert(revocation_normalize("source", "ansible-inventory:/etc/hosts", out) && !revocation_normalize("source", "nope", out));
    assert(!revocation_normalize("transport", "bad name", out) && !revocation_normalize("package", "abc", out));
    assert(!revocation_normalize("project", "relative", out) && !revocation_normalize("other", "x", out));
    assert(!revocation_blocked("transport", "unit"));
    result = revocation_record("transport", "unit", "rotated", NULL);
    assert(json_object_get_boolean(f_field(result, "ok")) && f_string(f_field(f_field(result, "data"), "revoked_by"), "user"));
    json_object_put(result);
    assert(revocation_blocked("transport", "unit") && !revocation_blocked("candidate", "unit"));
    result = revocation_record("transport", "unit", "again", NULL);
    assert(!json_object_get_boolean(f_field(result, "ok")) && !strcmp(f_string(f_field(result, "error"), "code"), "revoked"));
    json_object_put(result);
    record = revocation_find("transport", "unit");
    assert(record && !strcmp(f_string(record, "reason"), "rotated"));
    json_object_put(record);
    result = revocation_list(NULL);
    assert(json_object_array_length(result) == 1); json_object_put(result);
    assert(!revocation_find_key("SHA256:none"));
}
/* enrollment_hash is the digest of the file enrollment_write stores. Records
 * carry it, other hosts compare it and revocation records are named by it,
 * so it may never change. The expected digests were recorded from the
 * earlier implementation, which wrote that file and hashed it, and checked
 * with sha256sum over the serialised bytes. Takes ownership of value. */
static void enrollment_digest_is(json_object *value, const char *expected) {
    char digest[65], stored[65], path[F_PATH];
    assert(!enrollment_hash(value, digest));
    if (strcmp(digest, expected)) fprintf(stderr, "enrollment digest %s (want %s) of %s\n", digest, expected, json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN));
    assert(!strcmp(digest, expected));
    assert(!f_path(path, sizeof(path), f_home, "enrollment-digest.json") && !enrollment_write(path, value));
    assert(!f_hash(path, stored) && !strcmp(stored, digest) && enrollment_file_matches(path, digest));
    assert(!unlink(path));
    json_object_put(value);
}
static void test_enrollment_digests(void) {
    json_object *intent = f_parse("{\"schema_version\":1,\"kind\":\"enrollment_intent\",\"project\":\"/srv/app\",\"remote_hydra\":\"hydra\","
        "\"hosts\":[{\"name\":\"build-01\",\"target\":\"ubuntu@build-01.example.test\",\"port\":22,"
        "\"host_key\":\"ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGp\",\"jump\":[],\"labels\":{\"role\":\"builder\"}}],\"created_at\":1795000000}");
    assert(intent);
    enrollment_digest_is(json_object_new_string("transport:build-01.example.test"), "dd9e547abe257836818480f8bfefc7b5a233793a7db7d7e88ae590a5bc73b4a4");
    enrollment_digest_is(json_object_new_string("caf\xc3\xa9 \xe2\x9a\xa0 \xf0\x9f\x98\x80 /path\\with \"quotes\"\n\t\x01"),
                         "aeed2abaea654f8e56e4eff7d0b689728d93164155760045f538c031cde87cd4");
    enrollment_digest_is(f_parse("{\"path\":\"/home/user/project\",\"url\":\"ssh://u@h:22/x\"}"), "ba3b71b364e3fc1287ade321d4cbf06e872e70a4a630e2ae76843c687f817e50");
    enrollment_digest_is(intent, "68357b8095fb1b38a49ff4b23fd8f5d80390c5530cda53cd4d87f7e064dcef48");
    enrollment_digest_is(f_parse("{}"), "44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a");
    enrollment_digest_is(json_object_new_array(), "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945");
    enrollment_digest_is(NULL, "74234e98afe7498fb5daf1f36ac2d78acc339464f950703b8c019892f982b90b");
}
static void test_discovery_units(void) {
    test_ansible_parser(); test_avahi_parser(); test_ssh_scanner(); test_diff(); test_revocation_identity();
    test_enrollment_digests();
}
