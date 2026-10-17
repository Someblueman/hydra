/* Live-source parsers, snapshot diffs and revocation identities: pure
 * functions over fixture text, no files beyond the private test home. */
#include "fleet/discovery/discovery.h"
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
static void test_discovery_units(void) {
    test_ansible_parser(); test_avahi_parser(); test_ssh_scanner(); test_diff(); test_revocation_identity();
}
