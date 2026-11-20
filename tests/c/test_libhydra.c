#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 700
#include "libhydra.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROJECT_A "project_aaaaaaaaaaaaaaaa"
#define PROJECT_B "project_bbbbbbbbbbbbbbbb"
#define HEAD_C "head_cccccccccccccccc"
#define HEAD_D "head_dddddddddddddddd"
#define HEAD_E "head_eeeeeeeeeeeeeeee"
#define INSTANCE_C "instance_cccccccccccccccc"
#define INSTANCE_D "instance_dddddddddddddddd"
#define INSTANCE_E "instance_eeeeeeeeeeeeeeee"
#define HEAD_C_DIR "projects/" PROJECT_A "/heads/" HEAD_C
#define INSTANCE_C_DIR HEAD_C_DIR "/instances/" INSTANCE_C

static int failures = 0;
static char state_root[512];

static void check(int condition, const char *message) {
    if (condition) {
        printf("[PASS] %s\n", message);
    } else {
        printf("[FAIL] %s\n", message);
        failures++;
    }
}

static void snapshot_empty_state(void) {
    char root[] = "/tmp/hydra-snapshot-output.XXXXXX", schema[256], projects[256];
    FILE *record = NULL, *readonly = NULL;
    if (!mkdtemp(root)) { check(0, "snapshot failure fixture"); return; }
    snprintf(schema, sizeof(schema), "%s/schema-version", root);
    snprintf(projects, sizeof(projects), "%s/projects", root);
    record = fopen(schema, "w");
    if (record) { fputs("2\n", record); fclose(record); }
    if (!record || mkdir(projects, 0700)) { check(0, "snapshot failure fixture"); goto done; }
    FILE *snapshot = tmpfile();
    check(snapshot && hydra_write_snapshot(root, snapshot, stderr) == 0,
          "empty snapshot succeeds");
    if (snapshot) {
        char output[256];
        rewind(snapshot);
        size_t bytes = fread(output, 1U, sizeof(output) - 1U, snapshot);
        output[bytes] = '\0';
        check(strstr(output, "\"projects\":0,\"heads\":[]") != NULL,
              "empty snapshot contains no projects or heads");
        fclose(snapshot);
    }
    readonly = fopen(schema, "r");
    check(readonly && hydra_write_snapshot(root, readonly, stderr) != 0,
          "snapshot reports output errors");
    if (readonly) fclose(readonly);
done:
    unlink(schema); rmdir(projects); rmdir(root);
}

/* Writes <state_root>/<relative>, creating its directories; NULL content
 * removes the file instead. */
static void put(const char *relative, const char *content) {
    char path[1024];
    char *slash;
    FILE *file;
    snprintf(path, sizeof(path), "%s/%s", state_root, relative);
    if (content == NULL) {
        unlink(path);
        return;
    }
    for (slash = strchr(path + strlen(state_root) + 1U, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        mkdir(path, 0700);
        *slash = '/';
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        check(0, "state fixture file");
        return;
    }
    fputs(content, file);
    fclose(file);
}

static void put_in(const char *directory, const char *name, const char *content) {
    char relative[512];
    snprintf(relative, sizeof(relative), "%s/%s", directory, name);
    put(relative, content);
}

static void put_line(const char *directory, const char *name, const char *value) {
    char line[256];
    snprintf(line, sizeof(line), "%s\n", value);
    put_in(directory, name, line);
}

/* One interactive head as state_v2_create_head in lib/state_v2.sh records it. */
static void make_head(const char *project, const char *head, const char *instance, const char *branch) {
    static const char *const dashes[] = {"profile", "group", "dependencies", "pr"};
    char directory[256], instance_directory[384];
    size_t index;
    snprintf(directory, sizeof(directory), "projects/%s/heads/%s", project, head);
    snprintf(instance_directory, sizeof(instance_directory), "%s/instances/%s", directory, instance);
    put_line(directory, "head-id", head);
    put_line(directory, "branch", branch);
    put_line(directory, "session", "session-1");
    for (index = 0U; index < sizeof(dashes) / sizeof(dashes[0]); index++) put_line(directory, dashes[index], "-");
    put_line(directory, "created-at", "100");
    put_line(directory, "current-instance", instance);
    put_line(directory, "desired-state", "running");
    put_line(directory, "completion-policy", "declared-done");
    put_line(directory, "worktree", "/tmp/worktree");
    put_line(directory, "task", "");
    put_line(directory, "scopes", "");
    put_line(directory, "base-ref", "main");
    put_in(directory, "terminal-mode", NULL);
    put_in(directory, "events/events.jsonl", "");
    put_line(instance_directory, "instance-id", instance);
    put_line(instance_directory, "session", "session-1");
    put_line(instance_directory, "started-at", "100");
    put_line(instance_directory, "observed-status", "starting");
    put_line(instance_directory, "observed-source", "hydra");
    put_line(instance_directory, "observed-confidence", "exact");
    put_line(instance_directory, "observed-at", "100");
    put_line(instance_directory, "observed-exit-code", "");
    put_line(instance_directory, "provider-session-id", "");
    put_in(instance_directory, "terminal-mode", NULL);
}

static void make_state(void) {
    put("schema-version", "2\n");
    put_line("projects/" PROJECT_B, "project-id", PROJECT_B);
    put_line("projects/" PROJECT_B, "repo-root", "/tmp/b");
    put_line("projects/" PROJECT_A, "project-id", PROJECT_A);
    put_line("projects/" PROJECT_A, "repo-root", "/tmp/a");
    make_head(PROJECT_B, HEAD_E, INSTANCE_E, "later");
    make_head(PROJECT_A, HEAD_D, INSTANCE_D, "feature/\"quoted\"\\path");
    make_head(PROJECT_A, HEAD_C, INSTANCE_C, "first");
}

/* Runs the snapshot into output; returns its status. */
static int snapshot(char *output, size_t size) {
    FILE *stream = tmpfile();
    FILE *diagnostics = tmpfile();
    size_t bytes;
    int status;
    output[0] = '\0';
    if (stream == NULL || diagnostics == NULL) {
        if (stream) fclose(stream);
        if (diagnostics) fclose(diagnostics);
        return -2;
    }
    status = hydra_write_snapshot(state_root, stream, diagnostics);
    rewind(stream);
    bytes = fread(output, 1U, size - 1U, stream);
    output[bytes] = '\0';
    fclose(stream);
    fclose(diagnostics);
    return status;
}

static const char expected_head_c[] =
    "{\"project_id\":\"" PROJECT_A "\",\"head_id\":\"" HEAD_C "\",\"branch\":\"first\","
    "\"desired_state\":\"running\",\"current_instance\":\"" INSTANCE_C "\"}";
static const char expected_rest[] =
    ",{\"project_id\":\"" PROJECT_A "\",\"head_id\":\"" HEAD_D "\",\"branch\":\"feature/\\\"quoted\\\"\\\\path\","
    "\"desired_state\":\"running\",\"current_instance\":\"" INSTANCE_D "\"}"
    ",{\"project_id\":\"" PROJECT_B "\",\"head_id\":\"" HEAD_E "\",\"branch\":\"later\","
    "\"desired_state\":\"running\",\"current_instance\":\"" INSTANCE_E "\"}]}}\n";

static void expect_snapshot(const char *head_c, const char *message) {
    char output[2048], expected[2048];
    snprintf(expected, sizeof(expected),
             "{\"schema_version\":1,\"ok\":true,\"command\":\"snapshot\",\"data\":{\"state_schema\":2,\"projects\":2,\"heads\":[%s%s",
             head_c, expected_rest);
    check(snapshot(output, sizeof(output)) == 0 && strcmp(output, expected) == 0, message);
}

static void (*restore_state)(void) = make_state;

/* A store the shell verifier refuses is refused here too, with no output. The
 * store is valid before the one change, so the change is what is refused. */
static void expect_refused(const char *relative, const char *content, const char *message) {
    char output[2048];
    int valid = snapshot(output, sizeof(output)) == 0;
    put(relative, content);
    check(valid && snapshot(output, sizeof(output)) == -1 && output[0] == '\0', message);
    restore_state();
}

static void make_headless_state(void) {
    make_state();
    put(HEAD_C_DIR "/terminal-mode", "headless\n");
    put(INSTANCE_C_DIR "/terminal-mode", "headless\n");
    put(HEAD_C_DIR "/session", "-\n");
    put(INSTANCE_C_DIR "/session", "-\n");
    put(HEAD_C_DIR "/desired-state", "headless\n");
}

static int remove_entry(const char *path, const struct stat *status, int type, struct FTW *walk) {
    (void)status; (void)type; (void)walk;
    return remove(path);
}

static void remove_tree(const char *relative) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", state_root, relative);
    nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static void snapshot_states(void) {
    const char *base = getenv("TMPDIR");
    char scalar[16];
    snprintf(state_root, sizeof(state_root), "%s/hydra-libhydra.XXXXXX", base && base[0] ? base : "/tmp");
    if (!mkdtemp(state_root)) { check(0, "state fixture directory"); return; }
    make_state();
    expect_snapshot(expected_head_c, "snapshot lists heads by project, then head");

    put(HEAD_C_DIR "/branch", "first");
    expect_snapshot(expected_head_c, "a record without its final newline is read");
    put("projects/" PROJECT_A "/heads/head_ffffffffffffffff", "not a directory\n");
    put("projects/" PROJECT_A "/heads/notes/readme", "ignored\n");
    put("projects/README", "ignored\n");
    put(HEAD_C_DIR "/events/events.jsonl", "not an event\n");
    expect_snapshot(expected_head_c, "entries that are not heads or projects, and event logs, are not read");
    put("projects/" PROJECT_A "/heads/head_ffffffffffffffff", NULL);
    make_state();

    make_headless_state();
    restore_state = make_headless_state;
    expect_snapshot("{\"project_id\":\"" PROJECT_A "\",\"head_id\":\"" HEAD_C "\",\"branch\":\"first\","
                    "\"desired_state\":\"headless\",\"current_instance\":\"" INSTANCE_C "\"}",
                    "a headless head is listed");
    expect_refused(INSTANCE_C_DIR "/session", "session-1\n", "a headless instance with a session is refused");
    expect_refused(HEAD_C_DIR "/session", "session-1\n", "a headless head with a session is refused");
    expect_refused(INSTANCE_C_DIR "/terminal-mode", NULL, "a head and instance of different terminal modes are refused");
    restore_state = make_state;
    make_state();

    expect_refused("schema-version", "3\n", "another state schema is refused");
    expect_refused("schema-version", "2\r\n", "a schema record with a carriage return is refused");
    expect_refused("projects/" PROJECT_A "/project-id", PROJECT_B "\n", "a project-id record naming another project is refused");
    expect_refused("projects/" PROJECT_A "/repo-root", "\n", "an empty repo-root is refused");
    expect_refused("projects/project_nothex/project-id", "project_nothex\n", "a project directory that is not an opaque ID is refused");
    remove_tree("projects/project_nothex");
    expect_refused(HEAD_C_DIR "/head-id", HEAD_D "\n", "a head-id record naming another head is refused");
    expect_refused(HEAD_C_DIR "/branch", NULL, "a missing branch is refused");
    expect_refused(HEAD_C_DIR "/branch", "", "an empty branch file is refused");
    expect_refused(HEAD_C_DIR "/branch", "\n", "an empty branch is refused");
    expect_refused(HEAD_C_DIR "/branch", "first\nsecond\n", "a branch of two lines is refused");
    expect_refused(HEAD_C_DIR "/branch", "first\r\n", "a branch with a carriage return is refused");
    expect_refused(HEAD_C_DIR "/branch", "feature/\"quoted\"\\path\n", "two heads of a project on one branch are refused");
    expect_refused(HEAD_C_DIR "/session", "\n", "an empty session is refused");
    expect_refused(HEAD_C_DIR "/session", "-\n", "an interactive head without a session is refused");
    expect_refused(HEAD_C_DIR "/profile", NULL, "a missing required record is refused");
    expect_refused(HEAD_C_DIR "/base-ref", "main\r\n", "a required record with a carriage return is refused");
    expect_refused(HEAD_C_DIR "/task", NULL, "a missing task is refused");
    expect_refused(HEAD_C_DIR "/scopes", NULL, "missing scopes are refused");
    expect_refused(HEAD_C_DIR "/desired-state", "paused\n", "an unknown desired state is refused");
    expect_refused(HEAD_C_DIR "/completion-policy", "never\n", "an unknown completion policy is refused");
    expect_refused(HEAD_C_DIR "/terminal-mode", "windowed\n", "an unknown terminal mode is refused");
    expect_refused(HEAD_C_DIR "/terminal-mode", "", "an empty terminal mode is refused");
    expect_refused(HEAD_C_DIR "/current-instance", "instance_nothex\n", "a current instance that is not an opaque ID is refused");
    expect_refused(HEAD_C_DIR "/current-instance", "instance_0000000000000000\n", "a current instance without its directory is refused");
    expect_refused(INSTANCE_C_DIR "/instance-id", INSTANCE_D "\n", "an instance-id record naming another instance is refused");
    expect_refused(INSTANCE_C_DIR "/observed-exit-code", "", "an empty instance record file is refused");
    expect_refused(INSTANCE_C_DIR "/provider-session-id", NULL, "a missing instance record is refused");
    expect_snapshot(expected_head_c, "the restored store is listed again");

    put("scalar", "value\n");
    snprintf(scalar, sizeof(scalar), "unset");
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/scalar", state_root);
        check(hydra_read_scalar(path, scalar, sizeof(scalar)) == 0 && strcmp(scalar, "value") == 0, "scalar record is read");
        put("scalar", "0123456789abcdef\n");
        check(hydra_read_scalar(path, scalar, sizeof(scalar)) != 0, "scalar record longer than the buffer is refused");
        put("scalar", "0123456789abcd\n");
        check(hydra_read_scalar(path, scalar, sizeof(scalar)) == 0 && strlen(scalar) == 14U, "scalar record that fills the buffer is read");
        check(hydra_read_scalar(state_root, scalar, sizeof(scalar)) != 0, "a directory is not a scalar record");
    }
    remove_tree(".");
}

int main(void) {
    FILE *json = tmpfile();
    snapshot_empty_state();
    snapshot_states();
    char buffer[128];
    size_t bytes;
    check(hydra_valid_id("project_0123456789abcdef") == 1, "valid opaque ID");
    check(hydra_valid_id("project_not-hex") == 0, "invalid opaque ID");
    check(json != NULL, "temporary JSON stream");
    if (json != NULL) {
        check(hydra_json_write_string(json, "quote\" slash\\ tab\t") == 0, "JSON encoder succeeds");
        rewind(json);
        bytes = fread(buffer, 1U, sizeof(buffer) - 1U, json);
        buffer[bytes] = '\0';
        check(strcmp(buffer, "\"quote\\\" slash\\\\ tab\\t\"") == 0, "canonical JSON escaping");
        fclose(json);
    }
    return failures == 0 ? 0 : 1;
}
