/* Remote setup foundation: private state, plan approval gate, exit statuses
 * and the interactive SSH helper. Runs every case under umask 022 and 002. */
#include "fleet/setup/setup.h"
#include "fleet/auth/agent_auth.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
const char *f_home, *f_hydra = "hydra";

static char base[F_PATH], home[F_PATH], setup_dir[F_PATH];

static void use_home(const char *label) {
    assert(!f_path(home, sizeof(home), base, label));
    assert(!f_path(setup_dir, sizeof(setup_dir), home, "fleet/setup"));
    f_home = home;
}
static void state_file(char path[F_PATH], const char *name, const char *suffix) {
    assert(snprintf(path, F_PATH, "%s/%s%s", setup_dir, name, suffix) < F_PATH);
}
static unsigned mode_of(const char *path) {
    struct stat st;
    assert(!lstat(path, &st));
    return (unsigned)(st.st_mode & 0777);
}
/* Opens and expects an error code (or success for NULL); the context is closed. */
static void expect_open(const char *name, const char *dest, const char *config, unsigned flags, const char *code) {
    struct setup_ctx ctx; json_object *error;
    ctx.command = "remote-preflight";
    error = setup_state_open(&ctx, name, dest, config, flags);
    if (!code) assert(!error);
    else {
        assert(error && !json_object_get_boolean(f_field(error, "ok")));
        assert(!strcmp(f_string(f_field(error, "error"), "code"), code));
        assert(!strcmp(f_string(error, "command"), "remote-preflight"));
        assert(f_string(f_field(error, "error"), "recovery"));
    }
    json_object_put(error); setup_state_close(&ctx);
}
static void write_text(const char *path, const char *text, unsigned mode) {
    int fd;
    unlink(path);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd); assert(!chmod(path, (mode_t)mode));
}
static char *read_text(const char *path) {
    char *text = f_read(path, 1 << 20);
    assert(text);
    return text;
}

static void state_create_and_lock(struct setup_ctx *a) {
    struct setup_ctx b; char path[F_PATH], lock[F_PATH];
    a->command = NULL;
    assert(!setup_state_open(a, "n1", "user@host", NULL, SETUP_CREATE));
    assert(!strcmp(a->command, "remote-setup") && !strcmp(a->name, "n1"));
    assert(!strcmp(a->remote.target, "user@host"));
    state_file(path, "n1", ".json"); state_file(lock, "n1", ".lock");
    assert(mode_of(setup_dir) == 0700);
    assert(mode_of(path) == 0600);
    assert(mode_of(lock) == 0600);
    expect_open("n1", NULL, NULL, 0, "setup_busy");
    b.command = NULL;
    assert(!setup_state_open(&b, "n1", NULL, NULL, SETUP_READONLY));
    assert(setup_state_step(&b, "host_key", "done", NULL) == -1);
    setup_state_close(&b);
}
static void state_record_steps(struct setup_ctx *a) {
    json_object *detail = json_object_new_object();
    assert(!strcmp(setup_state_status(a, "preflight"), "pending"));
    f_string_add(detail, "summary", "ED25519 SHA256:abc");
    assert(!setup_state_step(a, "host_key", "done", detail));
    assert(!f_copy(a->remote.accepted_host_key, sizeof(a->remote.accepted_host_key), "SHA256:abc"));
    assert(!setup_state_step(a, "provision", "in_progress", NULL));
    assert(!setup_state_step(a, "install_agent:claude", "skipped", NULL));
    assert(setup_state_step(a, "bogus", "done", NULL) == -1);
    assert(setup_state_step(a, "host_key", "finished", NULL) == -1);
    assert(setup_state_step(a, "install_agent:bad/name", "done", NULL) == -1);
    assert(setup_state_step(a, "host_key", "done", json_object_new_string("not an object")) == -1);
}
static void state_reopened(struct setup_ctx *a) {
    a->command = NULL;
    assert(!setup_state_open(a, "n1", "user@host", NULL, SETUP_CREATE));
    assert(!strcmp(setup_state_status(a, "host_key"), "done"));
    assert(!strcmp(f_string(setup_state_detail(a, "host_key"), "summary"), "ED25519 SHA256:abc"));
    assert(!strcmp(setup_state_status(a, "provision"), "in_progress"));
    assert(!strcmp(setup_state_status(a, "install_agent:claude"), "skipped"));
    assert(!strcmp(a->remote.accepted_host_key, "SHA256:abc"));
    assert(!strcmp(a->remote.hydra, "hydra"));
    /* A null detail keeps the earlier detail. */
    assert(!setup_state_step(a, "host_key", "done", NULL));
    assert(f_string(setup_state_detail(a, "host_key"), "summary"));
}
static void state_modes_and_lock(void) {
    struct setup_ctx a; char path[F_PATH];
    use_home("modes");
    state_create_and_lock(&a);
    state_record_steps(&a);
    state_file(path, "n1", ".json");
    assert(mode_of(path) == 0600);
    setup_state_close(&a);
    state_reopened(&a);
    setup_state_close(&a);
}
static void state_binding_and_arguments(void) {
    use_home("binding");
    expect_open("n1", "user@host", "/etc/ssh/config", SETUP_CREATE, NULL);
    expect_open("n1", "user@host", "/etc/ssh/config", SETUP_CREATE, NULL);
    expect_open("n1", NULL, NULL, 0, NULL);
    expect_open("n1", "other-host", NULL, SETUP_CREATE, "setup_binding_changed");
    expect_open("n1", "user@host", "/other/config", SETUP_CREATE, "setup_binding_changed");
    expect_open("n2", NULL, NULL, 0, "setup_not_started");
    expect_open("n2", NULL, NULL, SETUP_CREATE, "setup_not_started");
    expect_open("n2", NULL, NULL, SETUP_READONLY, "setup_not_started");
    expect_open("status", "host", NULL, SETUP_CREATE, "invalid_input");
    expect_open("../x", "host", NULL, SETUP_CREATE, "invalid_input");
    expect_open("n3", "bad host", NULL, SETUP_CREATE, "invalid_input");
    expect_open("n3", "host", "relative/config", SETUP_CREATE, "invalid_input");
}
static void state_invalid_content(void) {
    char path[F_PATH], other[F_PATH], lock[F_PATH], *text;
    use_home("invalid");
    expect_open("n1", "host", NULL, SETUP_CREATE, NULL);
    state_file(path, "n1", ".json"); state_file(other, "n3", ".json"); state_file(lock, "n1", ".lock");
    text = read_text(path);
    /* Name must match the file. */
    write_text(other, text, 0600);
    expect_open("n3", NULL, NULL, 0, "state_invalid");
    /* Mode, symlink, syntax and schema are all refused and preserved. */
    assert(!chmod(path, 0644)); expect_open("n1", NULL, NULL, 0, "state_invalid");
    assert(!chmod(path, 0600)); expect_open("n1", NULL, NULL, 0, NULL);
    unlink(path); assert(!symlink(other, path));
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    unlink(path); write_text(path, "not json", 0600);
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    write_text(path, "{\"schema_version\":2}", 0600);
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    write_text(path, "{\"schema_version\":1,\"kind\":\"remote-setup\",\"name\":\"n1\",\"destination\":\"host\",\"ssh_config\":\"\","
                     "\"remote\":{\"target\":\"host\",\"ssh_config\":\"\"},\"steps\":{\"host_key\":{\"status\":\"weird\"}}}", 0600);
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    write_text(path, "{\"schema_version\":1,\"kind\":\"remote-setup\",\"name\":\"n1\",\"destination\":\"host\",\"ssh_config\":\"\","
                     "\"remote\":{\"target\":\"elsewhere\",\"ssh_config\":\"\"},\"steps\":{}}", 0600);
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    write_text(path, "{\"schema_version\":1,\"kind\":\"remote-setup\",\"name\":\"n1\",\"destination\":\"host\",\"ssh_config\":\"\","
                     "\"remote\":{\"target\":\"host\",\"ssh_config\":\"\",\"hydra\":\"relative/hydra\"},\"steps\":{}}", 0600);
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    write_text(path, text, 0600);
    expect_open("n1", NULL, NULL, 0, NULL);
    /* An unsafe lock file is refused rather than followed. */
    unlink(lock); assert(!symlink(other, lock));
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    unlink(lock); write_text(lock, "", 0644);
    expect_open("n1", NULL, NULL, 0, "state_invalid");
    unlink(lock);
    /* A shared setup directory is not private state. */
    assert(!chmod(setup_dir, 0755)); expect_open("n1", NULL, NULL, 0, "state_unavailable");
    assert(!chmod(setup_dir, 0700)); expect_open("n1", NULL, NULL, 0, NULL);
    if (geteuid() == 0) {
        assert(!chown(path, 1, 1)); expect_open("n1", NULL, NULL, 0, "state_invalid");
        assert(!chown(path, 0, 0)); expect_open("n1", NULL, NULL, 0, NULL);
    }
    free(text);
}

static const char *argv_at(json_object *argv, size_t index) {
    return f_text(json_object_array_get_idx(argv, index));
}
static void plan_hash_stability(struct setup_ctx *ctx) {
    json_object *one = f_parse("{\"b\":1,\"a\":{\"y\":\"2\",\"x\":[{\"k\":1,\"j\":2}]}}");
    json_object *two = f_parse("{\"a\":{\"x\":[{\"j\":2,\"k\":1}],\"y\":\"2\"},\"b\":1}");
    json_object *three = f_parse("{\"a\":{\"x\":[{\"j\":2,\"k\":1}],\"y\":\"3\"},\"b\":1}");
    json_object *canonical = NULL; char d1[65], d2[65], d3[65], d4[65], expected[65];
    const char *text = "{\"a\":{\"x\":[{\"j\":2,\"k\":1}],\"y\":\"2\"},\"b\":1,\"destination\":\"user@host\",\"kind\":\"provision\","
                       "\"name\":\"p1\",\"schema\":\"remote-setup-plan\",\"schema_version\":1}";
    assert(!setup_plan_hash(ctx, "provision", one, &canonical, d1));
    assert(!setup_plan_hash(ctx, "provision", two, NULL, d2));
    assert(!setup_plan_hash(ctx, "provision", three, NULL, d3));
    assert(!setup_plan_hash(ctx, "install_agent", one, NULL, d4));
    assert(!strcmp(json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN), text));
    assert(!auth_hash(text, expected) && !strcmp(d1, expected));
    assert(!strcmp(d1, d2) && strcmp(d1, d3) && strcmp(d1, d4));
    json_object_put(canonical); canonical = json_object_new_array();
    assert(setup_plan_hash(ctx, "provision", canonical, NULL, d4) == -1);
    json_object_put(canonical); json_object_put(one); json_object_put(two); json_object_put(three);
}
static void check_approval_required(json_object *result, const char *digest) {
    json_object *data = f_field(result, "data"), *next = f_field(data, "next"), *args = f_field(next, "argv");
    const char *const expected[] = {"hydra", "remote", "provision", "p1", "--approve", digest, "--json"};
    size_t i;
    assert(result && !strcmp(f_string(f_field(result, "error"), "code"), "approval_required"));
    assert(setup_exit_status(result, 1) == 3);
    assert(!strcmp(f_string(data, "plan_sha256"), digest));
    assert(!strcmp(f_string(f_field(data, "plan"), "schema"), "remote-setup-plan"));
    assert(!strcmp(f_string(f_field(data, "plan"), "prefix"), "/home/u/.local/share/hydra/fleet/abc"));
    assert(!strcmp(f_string(next, "step"), "provision"));
    assert(!strcmp(f_string(next, "approval_sha256"), digest));
    assert(json_object_array_length(args) == 7);
    for (i = 0; i < 7; i++) assert(!strcmp(argv_at(args, i), expected[i]));
}
static void expect_code(json_object *result, const char *code) {
    assert(result && !strcmp(f_string(f_field(result, "error"), "code"), code));
    json_object_put(result);
}
static void plan_gate_noninteractive(struct setup_ctx *ctx) {
    const char *const argv[] = {"provision", "p1", NULL};
    json_object *plan = f_parse("{\"binary_sha256\":\"abc\",\"prefix\":\"/home/u/.local/share/hydra/fleet/abc\"}");
    json_object *result; char digest[65], wrong[65];
    assert(!setup_plan_hash(ctx, "provision", plan, NULL, digest));
    result = setup_plan_gate(ctx, "provision", plan, NULL, argv);
    check_approval_required(result, digest);
    assert(!strcmp(setup_state_status(ctx, "provision"), "approval_required"));
    json_object_put(result);
    memset(wrong, '0', 64); wrong[64] = '\0';
    result = setup_plan_gate(ctx, "provision", plan, wrong, argv);
    assert(setup_exit_status(result, 1) == 1);
    assert(!strcmp(f_string(f_field(result, "data"), "plan_sha256"), digest));
    expect_code(result, "approval_mismatch");
    assert(!setup_plan_gate(ctx, "provision", plan, digest, argv));
    /* An interrupted step is never relabelled as merely waiting. */
    assert(!setup_state_step(ctx, "provision", "in_progress", NULL));
    json_object_put(setup_plan_gate(ctx, "provision", plan, NULL, argv));
    assert(!strcmp(setup_state_status(ctx, "provision"), "in_progress"));
    expect_code(setup_plan_gate(ctx, "bogus", plan, NULL, argv), "invalid_plan");
    expect_code(setup_plan_gate(ctx, "provision", plan, NULL, NULL), "invalid_plan");
    json_object_put(plan);
}
static void plan_gate_host_key(struct setup_ctx *ctx) {
    const char *const argv[] = {"trust-key", "p1", NULL};
    json_object *plan = f_parse("{\"fingerprint\":\"SHA256:abc\",\"key_type\":\"ED25519\"}"), *result, *args;
    ctx->json = false;
    result = setup_plan_gate(ctx, "host_key", plan, NULL, argv);
    args = f_field(f_field(f_field(result, "data"), "next"), "argv");
    assert(!strcmp(f_string(f_field(result, "error"), "code"), "approval_required"));
    assert(json_object_array_length(args) == 6 && !strcmp(argv_at(args, 4), "--fingerprint") && !strcmp(argv_at(args, 5), "SHA256:abc"));
    json_object_put(result);
    result = setup_plan_gate(ctx, "host_key", plan, "SHA256:zzz", argv);
    assert(!strcmp(f_string(f_field(result, "error"), "code"), "approval_mismatch")); json_object_put(result);
    assert(!setup_plan_gate(ctx, "host_key", plan, "SHA256:abc", argv));
    json_object_put(plan);
}
/* Feeds one answer through stdin to the interactive prompt. */
static json_object *answer(struct setup_ctx *ctx, const char *step, json_object *plan, const char *reply) {
    const char *const argv[] = {"provision", "p1", NULL}; int fds[2], saved = dup(STDIN_FILENO); json_object *result;
    assert(saved >= 0 && !pipe(fds));
    assert(write(fds[1], reply, strlen(reply)) == (ssize_t)strlen(reply));
    close(fds[1]); assert(dup2(fds[0], STDIN_FILENO) == STDIN_FILENO); close(fds[0]); clearerr(stdin);
    result = setup_plan_gate(ctx, step, plan, NULL, argv);
    assert(dup2(saved, STDIN_FILENO) == STDIN_FILENO); close(saved); clearerr(stdin);
    return result;
}
static void plan_gate_interactive(struct setup_ctx *ctx) {
    json_object *plan = f_parse("{\"fingerprint\":\"SHA256:abc\"}"), *result;
    ctx->interactive = true;
    assert(!answer(ctx, "provision", plan, "y\n"));
    result = answer(ctx, "provision", plan, "n\n");
    assert(!strcmp(f_string(f_field(result, "error"), "code"), "approval_declined")); json_object_put(result);
    result = answer(ctx, "provision", plan, "");
    assert(!strcmp(f_string(f_field(result, "error"), "code"), "approval_declined")); json_object_put(result);
    result = answer(ctx, "host_key", plan, "y\n");
    assert(!strcmp(f_string(f_field(result, "error"), "code"), "approval_declined")); json_object_put(result);
    assert(!answer(ctx, "host_key", plan, "yes\n"));
    ctx->interactive = false;
    json_object_put(plan);
}
static void plan_gate(void) {
    struct setup_ctx ctx;
    use_home("plan");
    ctx.command = "remote-provision";
    assert(!setup_state_open(&ctx, "p1", "user@host", NULL, SETUP_CREATE));
    ctx.json = true; ctx.interactive = false;
    plan_hash_stability(&ctx);
    plan_gate_noninteractive(&ctx);
    plan_gate_host_key(&ctx);
    plan_gate_interactive(&ctx);
    setup_state_close(&ctx);
}
static void exit_statuses(void) {
    json_object *ok = f_success("remote-setup", json_object_new_object());
    json_object *unknown = f_error("remote-provision", "outcome_unknown", "lost");
    json_object *cancelled = f_error("remote-sign-in", "cancelled", "interrupted"), *data = json_object_new_object();
    json_object *other = f_error("remote-setup", "not_implemented", "later");
    json_object_object_add(data, "exit_status", json_object_new_int(130));
    json_object_object_add(cancelled, "data", data);
    assert(setup_exit_status(ok, 0) == 0 && setup_exit_status(unknown, 1) == 4);
    assert(setup_exit_status(cancelled, 1) == 130 && setup_exit_status(other, 1) == 1);
    assert(setup_command("setup") && setup_command("sign-in") && !setup_command("add") && !setup_command(NULL));
    setenv("CI", "1", 1); assert(!setup_interactive(false)); unsetenv("CI");
    assert(!setup_interactive(true));
    json_object_put(ok); json_object_put(unknown); json_object_put(cancelled); json_object_put(other);
}
/* Installs a fake ssh that logs its argv and runs the remote command locally. */
static void fake_ssh(char log[F_PATH]) {
    char bin[F_PATH], ssh[F_PATH], path[F_PATH * 2];
    const char *script = "#!/bin/sh\nfor arg do printf '%s\\n' \"$arg\"; done > \"$SETUP_TEST_LOG\"\n"
                         "while [ $# -gt 1 ]; do shift; done\nexec /bin/sh -c \"$1\"\n";
    assert(!f_path(bin, sizeof(bin), base, "bin"));
    assert(!mkdir(bin, 0700));
    assert(!f_path(ssh, sizeof(ssh), bin, "ssh"));
    assert(!f_path(log, F_PATH, base, "ssh.argv"));
    write_text(ssh, script, 0700);
    assert(snprintf(path, sizeof(path), "%s:%s", bin, getenv("PATH")) < (int)sizeof(path));
    setenv("PATH", path, 1); setenv("SETUP_TEST_LOG", log, 1);
}
static int interactive_status(const struct f_remote *remote, const char *command) {
    int status = -2;
    assert(!f_ssh_interactive(remote, command, 5, &status));
    return status;
}
static void interactive_ssh(void) {
    char log[F_PATH], *argv; struct f_remote remote; int status;
    fake_ssh(log);
    memset(&remote, 0, sizeof(remote));
    assert(!f_copy(remote.target, sizeof(remote.target), "fake-host"));
    assert(interactive_status(&remote, "exit 7") == 7);
    argv = read_text(log);
    assert(!strncmp(argv, "-t\n", 3));
    assert(!strstr(argv, "-vv"));
    assert(strstr(argv, "StrictHostKeyChecking=yes"));
    assert(strstr(argv, "fake-host\nexit 7\n"));
    free(argv);
    assert(interactive_status(&remote, "exit 0") == 0);
    assert(interactive_status(&remote, "kill -TERM $$") == 143);
    assert(interactive_status(&remote, "kill -INT $$") == 130);
    remote.require_existing_master = true;
    assert(f_ssh_interactive(&remote, "exit 0", 5, &status) == -1 && status == -1);
}
int main(void) {
    char temp[] = "/tmp/hydra-setup-test.XXXXXX";
    const mode_t masks[] = {022, 002};
    size_t i;
    assert(mkdtemp(temp) && realpath(temp, base));
    interactive_ssh();
    for (i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
        char dir[F_PATH];
        umask(masks[i]);
        assert(snprintf(dir, sizeof(dir), "%s/umask%o", base, (unsigned)masks[i]) < (int)sizeof(dir) && !mkdir(dir, 0700));
        assert(!f_copy(base, sizeof(base), dir));
        state_modes_and_lock();
        state_binding_and_arguments();
        state_invalid_content();
        plan_gate();
        assert(realpath(temp, base));
    }
    exit_statuses();
    assert(!f_remove_tree(base));
    puts("Remote setup foundation tests passed");
    return 0;
}
