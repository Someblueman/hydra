#define _XOPEN_SOURCE 700
#include "fleet_recovery_support.h"
#include <inttypes.h>
#include <errno.h>
#include <glob.h>
#include <json-c/json.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void wait_for_payload(void) {
    char path[4096];
    double deadline = tv_now() + 30;
    tv_format(path, sizeof(path), "%s/host-a/payload-started", base);
    while (!tv_exists(path) && tv_now() < deadline)
        tv_sleep(.05);
    CHECK(tv_exists(path), "held payload started before owner-loss injection");
}
int main(void) {
    struct tv_session s;
    char path[4096], receiver[4096], home[4096], transport[4096], oldpath[32768], newpath[32768],
        script[8192], commit[128], task[2][256],
        host[2][16] = {"host-a", "host-b"}, specfile[4096], package[4096], digest[128], key[128],
        offline[4096], wf_id[256], run_id[256], stream_id[256], cursor[64], byte_offset[64],
        text[65536];
    json_object *spec, *preview, *receipt, *data, *first, *second, *prefix, *observed_document,
        *observed, *stream, *pages, *seen, *resumed = NULL, *suffix, *full, *worklog, *document;
    size_t i, page;
    double deadline;
    pid_t owner = 0, group = 0;
    tv_init();
    tv_paths(root, sizeof(root), build, sizeof(build));
    tv_temp(base, sizeof(base), "hydra-v4-real");
    tv_format(source, sizeof(source), "%s/source", base);
    tv_mkdir(source);
    tv_format(transport, sizeof(transport), "%s/transport", base);
    tv_mkdir(transport);
    tv_format(client, sizeof(client), "%s/client", base);
    tv_format(bin, sizeof(bin), "%s/bin/hydra", root);
    tv_format(tui, sizeof(tui), "%s/hydra-tui", build);
    tv_format(path, sizeof(path), "%s/hydra-fleet", build);
    setenv("HYDRA_FLEET_BIN", path, 1);
    tv_format(path, sizeof(path), "%s/hydra-core", build);
    setenv("HYDRA_CORE", path, 1);
    setenv("HYDRA_SKIP_AI", "1", 1);
    setenv("HYDRA_NONINTERACTIVE", "1", 1);
    setenv("HYDRA_NO_SWITCH", "1", 1);
    CHECK(!atexit(cleanup), "V4 cleanup registration");
    for (i = 0; i < 2; i++) {
        tv_format(receiver, sizeof(receiver), "%s/receiver-%c", base, 'a' + (int)i);
        tv_mkdir(receiver);
        run_at(receiver, (const char *[]){"git", "init", "-q", NULL});
        run_at(receiver, (const char *[]){"git", "config", "user.name", "V4", NULL});
        run_at(receiver,
               (const char *[]){"git", "config", "user.email", "v4@example.invalid", NULL});
        run_at(receiver, (const char *[]){"git", "-c", "commit.gpgSign=false", "commit",
                                          "--allow-empty", "-qm", "initial", NULL});
        tv_format(home, sizeof(home), "%s/host-%c", base, 'a' + (int)i);
        setenv("HYDRA_HOME", home, 1);
        run_at(receiver, (const char *[]){bin, "init", "--no-agent", "--json", NULL});
    }
    RUN("git", "init", "-q");
    RUN("git", "config", "user.name", "V4");
    RUN("git", "config", "user.email", "v4@example.invalid");
    tv_format(path, sizeof(path), "%s/payload.sh", source);
    tv_write(path, ": > \"$HYDRA_HOME/payload-started\"\n"
                   "while [ ! -f \"$HYDRA_HOME/release\" ]; do sleep .1; done; printf v4-result > "
                   "result.txt\n");
    RUN("git", "add", "payload.sh");
    RUN("git", "-c", "commit.gpgSign=false", "commit", "-qm", "payload");
    tv_format(path, sizeof(path), "%s/ssh", base);
    tv_format(script, sizeof(script),
              "#!/bin/sh\nset -eu\nwhile [ \"$#\" -gt 2 ]; do shift; done\ncase \"$1\" in\n "
              "loopback-a) [ ! -f \"$FLEET_PTY_BASE/transport/offline-a\" ] || exit 255; export "
              "HYDRA_HOME=\"$FLEET_PTY_BASE/host-a\" ;;\n loopback-b) export "
              "HYDRA_HOME=\"$FLEET_PTY_BASE/host-b\" ;;\n *) exit 255 ;;\nesac\nexec /bin/sh -c "
              "\"$2\"\n");
    tv_write(path, script);
    CHECK(!chmod(path, 0755), "SSH fixture executable");
    tv_format(oldpath, sizeof(oldpath), "%s", getenv("PATH"));
    tv_format(newpath, sizeof(newpath), "%s:%s", base, oldpath);
    setenv("PATH", newpath, 1);
    setenv("FLEET_PTY_BASE", base, 1);
    setenv("HYDRA_HOME", client, 1);
    setenv("HYDRA_BIN_CMD", bin, 1);
    H("init", "--no-agent", "--json");
    for (i = 0; i < 2; i++) {
        tv_format(home, sizeof(home), "%s/host-%c", base, 'a' + (int)i);
        tv_format(key, sizeof(key), "loopback-%c", 'a' + (int)i);
        H("remote", "add", host[i], key, "--hydra", bin, "--home", home);
    }
    tv_format(commit, sizeof(commit), "%s", RUN("git", "rev-parse", "HEAD"));
    trim(commit);
    for (i = 0; i < 2; i++) {
        tv_format(specfile, sizeof(specfile), "%s/spec-%c.json", base, 'a' + (int)i);
        tv_format(package, sizeof(package), "%s/package-%c.json", base, 'a' + (int)i);
        tv_format(receiver, sizeof(receiver), "%s/receiver-%c", base, 'a' + (int)i);
        spec = parse(
            "{\"schema_version\":1,\"source\":{},\"work\":{\"kind\":\"exec\",\"argv\":[\"sh\","
            "\"payload.sh\"]},\"inputs\":[],\"outputs\":[\"result.txt\"],\"capabilities\":["
            "\"exec\"],\"completion\":\"command-exit\",\"limits\":{\"transport_seconds\":30,"
            "\"queue_seconds\":30,\"startup_seconds\":30,\"execution_seconds\":30,\"cancellation_"
            "seconds\":5,\"log_bytes\":4096,\"artifact_bytes\":4096}}");
        addstr(spec, "host", host[i]);
        addstr(spec, "project", receiver);
        addstr(field(spec, "source"), "commit", commit);
        CHECK(!json_object_to_file(specfile, spec), "task spec");
        json_object_put(spec);
        preview = parse(H("fleet", "task", "prepare", "--source", source, "--spec", specfile,
                          "--output", package));
        tv_format(digest, sizeof(digest), "%s",
                  string(field(field(preview, "data"), "spec_sha256")));
        json_object_put(preview);
        tv_format(key, sizeof(key), "v4-%c", 'a' + (int)i);
        receipt = parse(H("fleet", "task", "submit", host[i], "--input", package, "--key", key,
                          "--trust-spec", digest));
        tv_format(task[i], sizeof(task[i]), "%s",
                  string(optional(receipt, "task_id") ? optional(receipt, "task_id")
                                                      : field(field(receipt, "data"), "task_id")));
        json_object_put(receipt);
    }
    tv_format(offline, sizeof(offline), "%s/offline-a", transport);
    open_observer(&s);
    tv_until(&s, task[0], 15);
    tv_until(&s, task[1], 15);
    CHECK(tv_contains(&s, "host-a"), "first receiver visible");
    tv_send(&s, "j");
    tv_pump(&s, .3);
    CHECK(tv_contains(&s, "host-b"), "second receiver selectable");
    wait_for_payload();
    for (i = 0; i < 50 && !owner; i++) {
        data = status(host[0], task[0]);
        owner = (pid_t)json_object_get_int64(optional(optional(data, "runtime"), "owner_pid"));
        json_object_put(data);
        if (!owner)
            tv_sleep(.1);
    }
    CHECK(owner > 0, "recorded owner PID");
    tv_write(offline, "");
    deadline = tv_now() + 8;
    while (tv_now() < deadline && !tv_contains(&s, "stale") && !tv_contains(&s, "STALE"))
        tv_pump(&s, .25);
    tv_send(&s, "H");
    tv_pump(&s, .4);
    CHECK(tv_contains(&s, "stale") || tv_contains(&s, "STALE"), "lost transport visible stale");
    CHECK(!kill(owner, SIGSTOP), "stop recorded owner");
    /* Freeze the owner before selecting its current execution group: startup
     * commands can exit and be replaced after owner_pid first appears. */
    RUN("ps", "-axo", "pid=,ppid=");
    {
        const char *p = output;
        while (*p) {
            int pid, ppid;
            if (sscanf(p, "%d %d", &pid, &ppid) == 2 && ppid == owner) {
                group = (pid_t)pid;
                break;
            }
            p = strchr(p, '\n');
            if (!p)
                break;
            p++;
        }
    }
    CHECK(group > 0, "owned command group");
    CHECK(!kill(-group, SIGKILL) || errno == ESRCH, "kill only owned command group");
    CHECK(!kill(owner, SIGKILL) || errno == ESRCH, "kill recorded owner");
    CHECK(!unlink(offline), "restore A transport");
    data = wait_runtime(host[0], task[0], "state", "outcome_unknown", 5);
    json_object_put(data);
    tv_write(offline, "");
    tv_format(path, sizeof(path), "%s/host-b/release", base);
    tv_write(path, "");
    data = wait_runtime(host[1], task[1], "state", "succeeded", 10);
    json_object_put(data);
    tv_close(&s, "q", 0, 0);
    CHECK(!unlink(offline), "restore receiver transport");
    open_observer(&s);
    tv_until(&s, task[0], 15);
    tv_until(&s, task[1], 15);
    document = parse(H("fleet", "task", "observe", host[1], "--id", task[1], "--event-limit", "2"));
    first = json_object_get(field(document, "data"));
    json_object_put(document);
    field(first, "event_observation");
    CHECK(!strcmp(string(field(field(first, "task"), "task_id")), task[1]),
          "observed task identity");
    CHECK(!strcmp(string(field(
                      json_object_array_get_idx(field(field(first, "task"), "attempt_history"), 0),
                      "attempt_id")),
                  "attempt-1"),
          "original attempt recorded");
    data = wait_runtime(host[1], task[1], "result_state", "ready", 40);
    CHECK(runtime_is(data, "state", "succeeded"), "result ready after process success");
    json_object_put(data);
    tv_format(path, sizeof(path), "%s/%s.result", base, task[1]);
    H("fleet", "task", "result", host[1], "--id", task[1], "--output", path);
    {
        struct stat st;
        CHECK(!stat(path, &st) && st.st_size > 0, "result collected");
    }
    document = parse(H("fleet", "task", "inspect-result", "--input", path));
    CHECK(json_object_is_type(field(document, "ok"), json_type_boolean) &&
              json_object_get_boolean(field(document, "ok")),
          "native result verified");
    json_object_put(document);
    tv_format(cursor, sizeof(cursor), "%" PRId64,
              json_object_get_int64(field(field(first, "event_observation"), "next_cursor")));
    document = parse(H("fleet", "task", "observe", host[1], "--id", task[1], "--cursor", cursor,
                       "--event-limit", "2"));
    second = field(document, "data");
    CHECK(!strcmp(string(field(field(second, "task"), "task_id")), task[1]) &&
              !strcmp(string(field(field(second, "task"), "run_id")),
                      string(field(field(first, "task"), "run_id"))),
          "resumed observation identity");
    CHECK(json_object_get_int64(field(field(second, "event_observation"), "oldest_cursor")) <=
              json_object_get_int64(field(field(second, "event_observation"), "head_cursor")),
          "cursor interval");
    json_object_put(document);
    json_object_put(first);
    tv_close(&s, "q", 0, 0);
    tv_format(path, sizeof(path), "%s/.hydra/workflows", source);
    tv_mkdir(path);
    tv_format(path, sizeof(path), "%s/.hydra/workflows/v4.yml", source);
    tv_write(path, workflow);
    tv_format(path, sizeof(path), "%s/workflow-work.sh", source);
    tv_write(
        path,
        "set -eu\nprintf '%s\\n' workflow-prefix\nwhile [ ! -f \"$HYDRA_HOME/release-workflow\" ]; "
        "do sleep .1; done\nprintf '%s\\n' workflow-suffix\nprintf workflow-result > result.txt\n");
    RUN("git", "add", ".hydra/workflows/v4.yml", "workflow-work.sh");
    RUN("git", "-c", "commit.gpgSign=false", "commit", "-qm", "v4 workflow");
    tv_format(path, sizeof(path), "%s/spec-b.json", base);
    spec = json_object_from_file(path);
    CHECK(spec, "workflow spec baseline");
    json_object_object_add(field(spec, "limits"), "execution_seconds", json_object_new_int(120));
    tv_format(commit, sizeof(commit), "%s", RUN("git", "rev-parse", "HEAD"));
    trim(commit);
    addstr(field(spec, "source"), "commit", commit);
    json_object_object_add(spec, "work",
                           parse("{\"kind\":\"workflow\",\"path\":\".hydra/workflows/v4.yml\"}"));
    addstr(spec, "completion", "workflow-success");
    tv_format(specfile, sizeof(specfile), "%s/wf-spec.json", base);
    CHECK(!json_object_to_file(specfile, spec), "workflow spec");
    json_object_put(spec);
    tv_format(package, sizeof(package), "%s/wf-package.json", base);
    preview = parse(
        H("fleet", "task", "prepare", "--source", source, "--spec", specfile, "--output", package));
    tv_format(digest, sizeof(digest), "%s", string(field(field(preview, "data"), "spec_sha256")));
    json_object_put(preview);
    receipt = parse(H("fleet", "task", "submit", "host-b", "--input", package, "--key",
                      "v4-workflow", "--trust-spec", digest));
    tv_format(wf_id, sizeof(wf_id), "%s",
              string(optional(receipt, "task_id") ? optional(receipt, "task_id")
                                                  : field(field(receipt, "data"), "task_id")));
    json_object_put(receipt);
    data = wait_runtime("host-b", wf_id, "run_id", NULL, 40);
    json_object_put(data);
    deadline = tv_now() + 40;
    prefix = NULL;
    while (tv_now() < deadline) {
        prefix = workflow_logs(wf_id, 0, "owner", 64);
        if (strlen(string(optional(prefix, "hex"))) == 128 &&
            !json_object_get_boolean(field(prefix, "eof")))
            break;
        json_object_put(prefix);
        prefix = NULL;
        tv_sleep(.15);
    }
    CHECK(prefix, "bounded nonterminal owner-log prefix");
    deadline = tv_now() + 40;
    observed_document = NULL;
    while (tv_now() < deadline) {
        observed_document =
            parse(H("fleet", "task", "observe", "host-b", "--id", wf_id, "--event-limit", "2"));
        if (work_running(observed_document))
            break;
        json_object_put(observed_document);
        observed_document = NULL;
        tv_sleep(.15);
    }
    CHECK(observed_document, "work attempt running");
    observed = field(observed_document, "data");
    pages = json_object_new_array();
    json_object_array_add(pages, observed_document);
    stream = field(observed, "event_observation");
    CHECK(json_object_get_boolean(field(stream, "available")) &&
              json_object_array_length(field(stream, "events")) &&
              !json_object_get_boolean(field(stream, "retention_gap")),
          "initial event stream");
    tv_format(run_id, sizeof(run_id), "%s", string(field(field(observed, "task"), "run_id")));
    attempt_exact(field(field(observed, "task"), "steps"), "running");
    save_json("observe-before.json", observed_document);
    save_json("log-before.json", prefix);
    open_observer(&s);
    select_task(&s, wf_id);
    tv_until(&s, "State: running", 15);
    check_runtime("host-b", wf_id, "state", "running");
    tv_close(&s, "q", 0, 0);
    check_runtime("host-b", wf_id, "state", "running");
    tv_write(offline, "");
    open_observer(&s);
    select_task(&s, wf_id);
    tv_until(&s, "State: running", 15);
    check_runtime("host-b", wf_id, "state", "running");
    tv_format(path, sizeof(path), "%s/host-b/release-workflow", base);
    tv_write(path, "");
    data = wait_runtime("host-b", wf_id, "result_state", "ready", 60);
    CHECK(runtime_is(data, "state", "succeeded") && runtime_is(data, "run_id", run_id),
          "original result after observer restart");
    json_object_put(data);
    tv_until(&s, "State: succeeded", 15);
    tv_format(text, sizeof(text), "> %s", wf_id);
    CHECK(tv_contains(&s, text) && tv_contains(&s, "host host-b"), "selected original task/host");
    tv_format(path, sizeof(path), "%s/observer-after.txt", base);
    tv_write(path, tv_text(&s));
    tv_close(&s, "q", 0, 0);
    CHECK(!unlink(offline), "restore offline fixture");
    seen = json_object_new_array();
    sequences(field(stream, "events"), seen);
    for (page = 0; page < 32; page++) {
        tv_format(cursor, sizeof(cursor), "%" PRId64,
                  json_object_get_int64(field(stream, "next_cursor")));
        tv_format(byte_offset, sizeof(byte_offset), "%" PRId64,
                  json_object_get_int64(field(stream, "next_byte_offset")));
        tv_format(stream_id, sizeof(stream_id), "%s", string(field(stream, "stream_id")));
        document =
            parse(H("fleet", "task", "observe", "host-b", "--id", wf_id, "--cursor", cursor,
                    "--byte-offset", byte_offset, "--stream-id", stream_id, "--event-limit", "2"));
        json_object_array_add(pages, document);
        resumed = field(document, "data");
        CHECK(!strcmp(string(field(field(resumed, "task"), "run_id")), run_id) &&
                  !strcmp(string(field(field(resumed, "task"), "task_id")), wf_id),
              "resume exact task/run");
        stream = field(resumed, "event_observation");
        CHECK(!strcmp(string(field(stream, "stream_id")), stream_id) &&
                  !json_object_get_boolean(field(stream, "stream_reset")) &&
                  !json_object_get_boolean(field(stream, "retention_gap")),
              "stream continuity");
        sequences(field(stream, "events"), seen);
        if (json_object_get_int64(field(stream, "next_cursor")) ==
            json_object_get_int64(field(stream, "head_cursor")))
            break;
        CHECK(json_object_array_length(field(stream, "events")), "resume progresses");
    }
    CHECK((long long)json_object_array_length(seen) ==
                  json_object_get_int64(field(stream, "head_cursor")) &&
              json_object_get_int64(field(stream, "head_cursor")) >= 10,
          "complete retained events");
    attempt_exact(field(field(resumed, "task"), "attempt_history"), "succeeded");
    suffix =
        workflow_logs(wf_id, json_object_get_int64(field(prefix, "next_offset")), "owner", 4096);
    full = workflow_logs(wf_id, 0, "owner", 4096);
    CHECK(json_object_get_int64(field(prefix, "next_offset")) == 64 &&
              *string(field(suffix, "hex")),
          "real nonempty resumed suffix");
    tv_format(text, sizeof(text), "%s%s", string(field(prefix, "hex")),
              string(field(suffix, "hex")));
    CHECK(!strcmp(text, string(field(full, "hex"))), "prefix plus suffix equals full owner log");
    CHECK(json_object_get_int64(field(suffix, "next_offset")) ==
              json_object_get_int64(field(full, "next_offset")),
          "log offsets reconcile");
    worklog = workflow_logs(wf_id, 0, "work", 4096);
    unhex(string(field(worklog, "hex")), text, sizeof(text));
    CHECK(strstr(text, "workflow-prefix") && strstr(text, "workflow-suffix"),
          "original work log includes both phases");
    json_object_put(worklog);
    tv_format(path, sizeof(path), "%s/workflow-result.json", base);
    H("fleet", "task", "result", "host-b", "--id", wf_id, "--output", path);
    document = parse(H("fleet", "task", "inspect-result", "--input", path));
    CHECK(json_object_is_type(field(document, "ok"), json_type_boolean) &&
              json_object_get_boolean(field(document, "ok")),
          "workflow result verifies");
    json_object_put(document);
    document = json_object_from_file(path);
    CHECK(document, "result file JSON");
    {
        json_object *artifacts = field(field(document, "result"), "artifacts");
        size_t found = 0;
        for (i = 0; i < json_object_array_length(artifacts); i++) {
            json_object *row = json_object_array_get_idx(artifacts, i);
            if (!strcmp(string(field(row, "path")), "result.txt")) {
                found++;
                unhex(string(field(row, "hex")), text, sizeof(text));
                CHECK(!strcmp(text, "workflow-result"), "exact result artifact");
            }
        }
        CHECK(found == 1, "unique result artifact");
    }
    json_object_put(document);
    CHECK(acceptance_count("a") == 1 && acceptance_count("b") == 2, "no replay/new acceptance");
    check_runtime(host[0], task[0], "state", "outcome_unknown");
    for (i = 0; i < json_object_array_length(pages); i++) {
        const char *announcement;
        size_t j;
        tv_format(key, sizeof(key), "observe-page-%zu.json", i);
        save_json(key, json_object_array_get_idx(pages, i));
        tv_format(path, sizeof(path), "%s/%s", base, key);
        document = parse(H("fleet", "task", "announce", "--input", path));
        announcement = string(field(field(document, "data"), "announcement"));
        for (j = 0; announcement[j]; j++)
            CHECK((unsigned char)announcement[j] < 128, "ASCII announcement");
        CHECK(strstr(announcement, "evidence=saved_snapshot") &&
                  strstr(announcement, "resume cursor="),
              "announcement provenance/cursor");
        tv_format(path, sizeof(path), "%s/announcement-%zu.txt", base, i);
        tv_write(path, announcement);
        json_object_put(document);
    }
    document = json_object_new_object();
    addstr(document, "task", wf_id);
    addstr(document, "run", run_id);
    addstr(document, "attempt", "attempt-1");
    json_object_object_add(document, "events", json_object_get(seen));
    json_object_object_add(
        document, "log_bytes",
        json_object_new_int64(json_object_get_int64(field(suffix, "next_offset"))));
    json_object_object_add(document, "prefix_bytes", json_object_new_int(64));
    json_object_object_add(
        document, "resumed_bytes",
        json_object_new_int64((int64_t)strlen(string(field(suffix, "hex"))) / 2));
    addstr(document, "result", "workflow-result");
    json_object_object_add(document, "no_replay", json_object_new_boolean(true));
    save_json("summary.json", document);
    json_object_put(document);
    json_object_put(prefix);
    json_object_put(suffix);
    json_object_put(full);
    json_object_put(seen);
    json_object_put(pages);
    succeeded = true;
    cleanup();
    CHECK(!cleanup_failed, "fixture quiescence");
    puts("PASS V4 two receivers: observer restart preserves task/run/attempt, complete "
         "events/logs/result, isolated owner loss, no replay");
    return 0;
}
