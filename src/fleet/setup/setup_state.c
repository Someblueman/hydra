#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SETUP_STATE_LIMIT (1024U * 1024U)

static const char *const statuses[] = {
    "pending", "in_progress", "done", "skipped", "approval_required", "blocked", "outcome_unknown", "failed", NULL
};
static const char *const base_steps[] = {
    "host_key", "preflight", "provision", "agents", "verify", "alias", NULL
};

bool setup_status_valid(const char *status) {
    size_t i;
    if (!status) return false;
    for (i = 0; statuses[i]; i++) if (!strcmp(status, statuses[i])) return true;
    return false;
}
static bool in_list(const char *value, size_t length, const char *const *list) {
    size_t i;
    for (i = 0; list[i]; i++) if (strlen(list[i]) == length && !strncmp(value, list[i], length)) return true;
    return false;
}
bool setup_step_id(const char *step) {
    const char *colon;
    static const char *const agent_steps[] = {"install_agent", "sign_in", NULL};
    if (!step) return false;
    colon = strchr(step, ':');
    if (!colon) return in_list(step, strlen(step), base_steps);
    return in_list(step, (size_t)(colon - step), agent_steps) && f_name(colon + 1) && strlen(colon + 1) < 64;
}
static bool setup_name(const char *name) {
    return f_name(name) && strlen(name) < 128 && strcmp(name, "status") != 0;
}

/* Opens $HYDRA_HOME/fleet/setup, creating private directories as needed. */
static int setup_directory(char path[F_PATH]) {
    char home[F_PATH], fleet[F_PATH]; struct stat st;
    int home_fd = -1, fleet_fd = -1, dir_fd = -1, status = -1;
    if (f_mkdirs(f_home) || !realpath(f_home, home)) return -1;
    home_fd = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (home_fd < 0 || fstat(home_fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) goto done;
    fleet_fd = task_owned_directory(home_fd, "fleet", true);
    if (fleet_fd < 0) goto done;
    dir_fd = task_owned_directory(fleet_fd, "setup", true);
    if (dir_fd < 0 || fstat(dir_fd, &st) || (st.st_mode & 0077)) goto done;
    if (f_path(fleet, sizeof(fleet), home, "fleet") || f_path(path, F_PATH, fleet, "setup")) goto done;
    status = 0;
done:
    if (dir_fd >= 0) close(dir_fd);
    if (fleet_fd >= 0) close(fleet_fd);
    if (home_fd >= 0) close(home_fd);
    return status;
}
static bool private_file(int fd) {
    struct stat st;
    return !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
}
/* 0 locked, -1 unavailable, -2 busy, -3 unsafe lock file. */
static int setup_lock(struct setup_ctx *ctx, const char *dir) {
    char lock[F_PATH];
    if (snprintf(lock, sizeof(lock), "%s/%s.lock", dir, ctx->name) >= (int)sizeof(lock)) return -1;
    ctx->lock_fd = open(lock, O_CREAT | O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (ctx->lock_fd < 0) return errno == ELOOP ? -3 : -1;
    if (!private_file(ctx->lock_fd)) return -3;
    if (flock(ctx->lock_fd, LOCK_EX | LOCK_NB)) return errno == EWOULDBLOCK ? -2 : -1;
    return 0;
}
/* Reads the private state file: 0 read, 1 absent, -1 unsafe or unreadable. */
static int state_read(const char *path, char **text) {
    struct stat st; int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    size_t size = 0; ssize_t n = 1;
    *text = NULL;
    if (fd < 0) return errno == ENOENT ? 1 : -1;
    if (!private_file(fd) || fstat(fd, &st) || st.st_size < 0 || (size_t)st.st_size > SETUP_STATE_LIMIT ||
        !(*text = malloc(SETUP_STATE_LIMIT + 1))) { close(fd); return -1; }
    while (n > 0 && size <= SETUP_STATE_LIMIT) {
        n = read(fd, *text + size, SETUP_STATE_LIMIT + 1 - size);
        if (n > 0) size += (size_t)n;
        else if (n < 0 && errno == EINTR) n = 1;
    }
    close(fd);
    if (n < 0 || size > SETUP_STATE_LIMIT || memchr(*text, '\0', size)) { free(*text); *text = NULL; return -1; }
    (*text)[size] = '\0';
    return 0;
}
static bool optional_absolute(const char *value) {
    return value && (!value[0] || value[0] == '/');
}
static bool remote_fields(struct f_remote *remote, json_object *obj) {
    const struct { char *dst; size_t size; const char *key; } fields[] = {
        {remote->hydra, sizeof(remote->hydra), "hydra"}, {remote->home, sizeof(remote->home), "home"},
        {remote->principal, sizeof(remote->principal), "principal"}, {remote->project, sizeof(remote->project), "project"},
        {remote->accepted_host_key, sizeof(remote->accepted_host_key), "accepted_host_key"}
    };
    size_t i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        json_object *value = f_field(obj, fields[i].key);
        if (value && (!f_text(value) || f_copy(fields[i].dst, fields[i].size, f_text(value)))) return false;
    }
    if (f_field(obj, "multiplex") && !json_object_is_type(f_field(obj, "multiplex"), json_type_boolean)) return false;
    remote->multiplex = json_object_get_boolean(f_field(obj, "multiplex"));
    return (!remote->hydra[0] || !strcmp(remote->hydra, "hydra") || remote->hydra[0] == '/') &&
        optional_absolute(remote->home) && optional_absolute(remote->project);
}
static bool steps_valid(json_object *steps) {
    if (!json_object_is_type(steps, json_type_object)) return false;
    json_object_object_foreach(steps, key, value) {
        json_object *detail = f_field(value, "detail");
        if (!setup_step_id(key) || !json_object_is_type(value, json_type_object) ||
            !setup_status_valid(f_string(value, "status")) || (detail && !json_object_is_type(detail, json_type_object)))
            return false;
    }
    return true;
}
/* Validates a parsed record and loads its binding into ctx->remote. */
static bool state_valid(struct setup_ctx *ctx, json_object *state) {
    const char *dest = f_string(state, "destination"), *config = f_string(state, "ssh_config");
    json_object *remote = f_field(state, "remote");
    if (!f_number_is(state, "schema_version", SETUP_SCHEMA) || !f_string(state, "kind") ||
        strcmp(f_string(state, "kind"), "remote-setup") || !f_string(state, "name") ||
        strcmp(f_string(state, "name"), ctx->name) || !f_target(dest) || !optional_absolute(config) ||
        !json_object_is_type(remote, json_type_object) || !steps_valid(f_field(state, "steps")))
        return false;
    if (!f_string(remote, "target") || strcmp(f_string(remote, "target"), dest) ||
        !f_string(remote, "ssh_config") || strcmp(f_string(remote, "ssh_config"), config)) return false;
    return !f_copy(ctx->remote.target, sizeof(ctx->remote.target), dest) &&
        !f_copy(ctx->remote.ssh_config, sizeof(ctx->remote.ssh_config), config) && remote_fields(&ctx->remote, remote);
}
static json_object *remote_json(const struct f_remote *remote) {
    json_object *obj = json_object_new_object();
    f_string_add(obj, "target", remote->target); f_string_add(obj, "ssh_config", remote->ssh_config);
    f_string_add(obj, "hydra", remote->hydra); f_string_add(obj, "home", remote->home);
    f_string_add(obj, "principal", remote->principal); f_string_add(obj, "project", remote->project);
    f_string_add(obj, "accepted_host_key", remote->accepted_host_key);
    json_object_object_add(obj, "multiplex", json_object_new_boolean(remote->multiplex));
    return obj;
}
static int state_write(struct setup_ctx *ctx) {
    char dir[F_PATH], *slash; const char *text; int fd, status;
    json_object_object_add(ctx->state, "remote", remote_json(&ctx->remote));
    json_object_object_add(ctx->state, "updated_at", json_object_new_int64((int64_t)time(NULL)));
    text = json_object_to_json_string_ext(ctx->state, JSON_C_TO_STRING_PLAIN);
    if (strlen(text) > SETUP_STATE_LIMIT || f_write(ctx->path, text, strlen(text), true) || f_copy(dir, sizeof(dir), ctx->path)) return -1;
    slash = strrchr(dir, '/');
    if (!slash) return -1;
    *slash = '\0';
    fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return -1;
    status = fsync(fd); close(fd);
    return status ? -1 : 0;
}
static json_object *open_error(struct setup_ctx *ctx, const char *code, const char *message, const char *recovery) {
    return setup_error(ctx, code, message, recovery, NULL);
}
static json_object *lock_error(struct setup_ctx *ctx, int locked) {
    if (locked == -2) return open_error(ctx, "setup_busy", "another setup command for this remote is running", "wait for it to finish, then rerun");
    if (locked == -3) return open_error(ctx, "state_invalid", "setup lock file is not a private regular file", "inspect and remove the unsafe lock file under $HYDRA_HOME/fleet/setup");
    return open_error(ctx, "state_unavailable", "cannot lock private setup state", "check $HYDRA_HOME/fleet/setup ownership and permissions");
}
static json_object *binding_check(struct setup_ctx *ctx, const char *dest, const char *ssh_config) {
    if ((dest && strcmp(dest, ctx->remote.target)) || (ssh_config && strcmp(ssh_config, ctx->remote.ssh_config)))
        return open_error(ctx, "setup_binding_changed", "this setup name is bound to a different destination or SSH config",
                          "rerun without DEST, choose a new NAME, or remove the setup state deliberately");
    return NULL;
}
static json_object *state_new(struct setup_ctx *ctx, const char *dest, const char *ssh_config, unsigned flags) {
    if (!(flags & SETUP_CREATE) || !dest)
        return open_error(ctx, "setup_not_started", "no setup state exists for this remote",
                          "start with hydra remote setup NAME [USER@]HOST");
    if (flags & SETUP_READONLY) return open_error(ctx, "invalid_input", "cannot create read-only setup state", NULL);
    if (f_copy(ctx->remote.target, sizeof(ctx->remote.target), dest) ||
        f_copy(ctx->remote.ssh_config, sizeof(ctx->remote.ssh_config), ssh_config ? ssh_config : "") ||
        f_copy(ctx->remote.hydra, sizeof(ctx->remote.hydra), "hydra"))
        return open_error(ctx, "invalid_input", "destination or SSH config is too long", NULL);
    ctx->state = json_object_new_object();
    json_object_object_add(ctx->state, "schema_version", json_object_new_int(SETUP_SCHEMA));
    f_string_add(ctx->state, "kind", "remote-setup"); f_string_add(ctx->state, "name", ctx->name);
    f_string_add(ctx->state, "destination", dest); f_string_add(ctx->state, "ssh_config", ctx->remote.ssh_config);
    f_string_add(ctx->state, "hydra_version", F_VERSION);
    json_object_object_add(ctx->state, "created_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(ctx->state, "steps", json_object_new_object());
    if (state_write(ctx)) return open_error(ctx, "state_unavailable", "cannot write private setup state", NULL);
    return NULL;
}
static json_object *state_load(struct setup_ctx *ctx, const char *dest, const char *ssh_config, unsigned flags) {
    char *text = NULL; int read_status = state_read(ctx->path, &text);
    if (read_status > 0) return state_new(ctx, dest, ssh_config, flags);
    ctx->state = read_status ? NULL : f_parse(text);
    free(text);
    if (!ctx->state || !state_valid(ctx, ctx->state))
        return open_error(ctx, "state_invalid", "setup state is unsafe or invalid",
                          "preserve $HYDRA_HOME/fleet/setup/NAME.json for inspection; it must be a private 0600 file owned by you");
    return binding_check(ctx, dest, ssh_config);
}
static json_object *arguments_error(struct setup_ctx *ctx, const char *name, const char *dest, const char *ssh_config) {
    if (!setup_name(name)) return open_error(ctx, "invalid_input", "NAME must be an alias name (letters, digits, _ . -) other than status", NULL);
    if (dest && !f_target(dest)) return open_error(ctx, "invalid_input", "DEST must be an SSH alias or [USER@]HOST", NULL);
    if (ssh_config && ssh_config[0] != '/') return open_error(ctx, "invalid_input", "--ssh-config requires an absolute path", NULL);
    return NULL;
}
json_object *setup_state_open(struct setup_ctx *ctx, const char *name, const char *dest,
                              const char *ssh_config, unsigned flags) {
    char dir[F_PATH], file[160]; const char *command = ctx->command; json_object *error; int locked;
    memset(ctx, 0, sizeof(*ctx));
    ctx->command = command ? command : "remote-setup"; ctx->lock_fd = -1;
    ctx->readonly = (flags & SETUP_READONLY) != 0; ctx->seconds = 10;
    if ((error = arguments_error(ctx, name, dest, ssh_config))) return error;
    f_copy(ctx->remote.name, sizeof(ctx->remote.name), name); ctx->name = ctx->remote.name;
    if (setup_directory(dir) || snprintf(file, sizeof(file), "%s.json", name) >= (int)sizeof(file) ||
        f_path(ctx->path, sizeof(ctx->path), dir, file))
        return open_error(ctx, "state_unavailable", "cannot use the private setup directory",
                          "$HYDRA_HOME and $HYDRA_HOME/fleet must be yours and not group/world writable; fleet/setup must be 0700");
    if (!ctx->readonly && (locked = setup_lock(ctx, dir))) return lock_error(ctx, locked);
    return state_load(ctx, dest, ssh_config, flags);
}
void setup_state_close(struct setup_ctx *ctx) {
    json_object_put(ctx->state); ctx->state = NULL;
    if (ctx->lock_fd >= 0) close(ctx->lock_fd);
    ctx->lock_fd = -1;
}
int setup_state_step(struct setup_ctx *ctx, const char *step, const char *status, json_object *detail) {
    json_object *steps = f_field(ctx->state, "steps"), *record, *old;
    if (ctx->readonly || !steps || !setup_step_id(step) || !setup_status_valid(status) ||
        (detail && !json_object_is_type(detail, json_type_object))) { json_object_put(detail); return -1; }
    old = f_field(steps, step);
    record = json_object_new_object();
    f_string_add(record, "status", status);
    if (detail) json_object_object_add(record, "detail", detail);
    else if (f_field(old, "detail")) json_object_object_add(record, "detail", json_object_get(f_field(old, "detail")));
    json_object_object_add(record, "updated_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(steps, step, record);
    return state_write(ctx);
}
int setup_state_set(struct setup_ctx *ctx, const char *key, json_object *value) {
    static const char *const reserved[] = {"schema_version", "kind", "name", "destination", "ssh_config", "steps", "remote", NULL};
    size_t i;
    for (i = 0; key && reserved[i]; i++) if (!strcmp(key, reserved[i])) key = NULL;
    if (ctx->readonly || !ctx->state || !key) { json_object_put(value); return -1; }
    json_object_object_add(ctx->state, key, value);
    return state_write(ctx);
}
json_object *setup_upgrade(struct setup_ctx *ctx) {
    json_object *upgrade = f_field(ctx->state, "upgrade");
    return json_object_is_type(upgrade, json_type_object) ? upgrade : NULL;
}
const char *setup_state_status(struct setup_ctx *ctx, const char *step) {
    const char *status = f_string(f_field(f_field(ctx->state, "steps"), step), "status");
    return status ? status : "pending";
}
json_object *setup_state_detail(struct setup_ctx *ctx, const char *step) {
    return f_field(f_field(f_field(ctx->state, "steps"), step), "detail");
}
