#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_drive.h"
#include "fleet/support/files.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Each helper writes what the shell function of the same purpose writes:
 * workflow_atomic_scalar (state_v2_write_scalar), hydra_private_mkdir,
 * hydra_private_touch, `sed -n 1p` reads and workflow_event. */

int wdr_join(char *out, const char *dir, const char *name) { return f_path(out, F_PATH, dir, name); }

int wdr_step_path(char *out, const struct wdr_run *run, size_t step, const char *name) {
    char dir[F_PATH];
    if (snprintf(dir, sizeof(dir), "%s/steps/%s", run->dir, run->steps[step].id) >= (int)sizeof(dir)) return -1;
    return name ? wdr_join(out, dir, name) : f_copy(out, F_PATH, dir);
}

/* The first line without its newline, as `sed -n 1p` prints it; "" and -1
 * when the file cannot be read. */
int wdr_first_line(const char *path, char *out, size_t size) {
    size_t used = 0; int fd = open(path, O_RDONLY);
    out[0] = '\0';
    if (fd < 0) return -1;
    while (used + 1 < size) {
        ssize_t n = read(fd, out + used, size - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); out[0] = '\0'; return -1; }
        if (n == 0) break;
        out[used + (size_t)n] = '\0';
        if (memchr(out + used, '\n', (size_t)n)) { used += (size_t)n; break; }
        used += (size_t)n;
    }
    close(fd);
    out[used] = '\0';
    out[strcspn(out, "\n")] = '\0';
    return 0;
}

bool wdr_exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
bool wdr_is_dir(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }

/* Like mkdir -p, but only the components it creates are set to 0700. */
int wdr_private_mkdirs(const char *dir) {
    char parent[F_PATH]; char *slash;
    if (wdr_is_dir(dir)) return 0;
    if (f_copy(parent, sizeof(parent), dir)) return -1;
    slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (wdr_private_mkdirs(parent)) return -1;
    }
    if (mkdir(dir, 0700) == 0) return chmod(dir, 0700);
    return wdr_is_dir(dir) ? 0 : -1;
}

/* A missing file is created empty with mode 0600; an existing one is kept. */
int wdr_private_touch(const char *path) {
    struct stat st; int fd;
    if (lstat(path, &st) == 0) return 0;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return errno == EEXIST ? 0 : -1;
    if (fchmod(fd, 0600)) { close(fd); return -1; }
    return close(fd);
}

static int write_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n; size -= (size_t)n;
    }
    return 0;
}

/* One line written to an adjacent temporary file, made private and renamed
 * over the destination. */
int wdr_scalar(const char *path, const char *value) {
    char dir[F_PATH], tmp[F_PATH]; char *slash; int fd;
    if (strpbrk(value, "\n\r")) { fputs("Error: scalar state values cannot contain newlines\n", stderr); return -1; }
    if (f_copy(dir, sizeof(dir), path)) return -1;
    slash = strrchr(dir, '/');
    if (!slash) return -1;
    *slash = '\0';
    if (wdr_private_mkdirs(dir) || wdr_join(tmp, dir, ".hydra-tmp.XXXXXX")) return -1;
    fd = mkstemp(tmp);
    if (fd < 0) return -1;
    (void)(write_all(fd, value, strlen(value)) || write_all(fd, "\n", 1));
    (void)fchmod(fd, 0600);
    close(fd);
    if (rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

int wdr_scalar_long(const char *path, long value) {
    char text[32];
    snprintf(text, sizeof(text), "%ld", value);
    return wdr_scalar(path, text);
}

int wdr_run_scalar(const struct wdr_run *run, const char *name, const char *value) {
    char path[F_PATH];
    return wdr_join(path, run->dir, name) ? -1 : wdr_scalar(path, value);
}

int wdr_step_scalar(const struct wdr_run *run, size_t step, const char *name, const char *value) {
    char path[F_PATH];
    return wdr_step_path(path, run, step, name) ? -1 : wdr_scalar(path, value);
}

void wdr_step_line(const struct wdr_run *run, size_t step, const char *name, char *out, size_t size) {
    char path[F_PATH];
    out[0] = '\0';
    if (!wdr_step_path(path, run, step, name)) (void)wdr_first_line(path, out, size);
}

void wdr_run_line(const struct wdr_run *run, const char *name, char *out, size_t size) {
    char path[F_PATH];
    out[0] = '\0';
    if (!wdr_join(path, run->dir, name)) (void)wdr_first_line(path, out, size);
}

bool wdr_run_has(const struct wdr_run *run, const char *name) {
    char path[F_PATH];
    return !wdr_join(path, run->dir, name) && wdr_exists(path);
}

int wdr_remove_tree(const char *path) { return f_remove_tree(path); }

/* Digits only, as the shell's `''|*[!0-9]*` guards accept. */
bool wdr_number(const char *text, long *value) {
    if (!text[0] || strspn(text, "0123456789") != strlen(text) || strlen(text) > 18) return false;
    *value = strtol(text, NULL, 10);
    return true;
}

/* A recorded counter in shell arithmetic: digits read as a number, an empty
 * or unreadable value as 0. */
long wdr_shell_number(const char *text) {
    long value;
    return wdr_number(text, &value) ? value : 0;
}

/* json_escape: JSON syntax and C0 controls escaped, other bytes unchanged. */
static size_t escape(char *out, size_t size, const char *text) {
    static const char named[] = "\b\t\n\f\r", letters[] = "btnfr";
    size_t used = 0;
    for (; *text && used + 8 < size; text++) {
        unsigned char c = (unsigned char)*text; const char *slot = c ? strchr(named, c) : NULL;
        if (c == '"' || c == '\\') { out[used++] = '\\'; out[used++] = (char)c; }
        else if (slot) { out[used++] = '\\'; out[used++] = letters[slot - named]; }
        else if (c < 32) used += (size_t)snprintf(out + used, size - used, "\\u00%02x", c);
        else out[used++] = (char)c;
    }
    out[used] = '\0';
    return used;
}

/* awk 'END { print NR + 1 }': a last line without a newline counts too. */
static long next_sequence(const char *path) {
    char buffer[65536]; long lines = 0; char last = '\n'; int fd = open(path, O_RDONLY);
    if (fd < 0) return 1;
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer)), i;
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        for (i = 0; i < n; i++) if (buffer[i] == '\n') lines++;
        last = buffer[n - 1];
    }
    close(fd);
    return lines + (last != '\n') + 1;
}

static void pause_second(void) {
    struct timespec left = {1, 0};
    while (nanosleep(&left, &left) && errno == EINTR) {}
}

static int take_event_lock(struct wdr_run *run, const char *lock) {
    char owner_file[F_PATH], owner[64], pid[32]; int tries = 0, fd;
    if (wdr_join(owner_file, lock, "owner-pid")) return -1;
    while (mkdir(lock, 0700)) {
        (void)wdr_first_line(owner_file, owner, sizeof(owner));
        if (owner[0] && !wdr_pid_alive(run, owner)) { (void)wdr_remove_tree(lock); continue; }
        if (++tries >= 30) return -1;
        pause_second();
    }
    (void)chmod(lock, 0700);
    snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
    fd = open(owner_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) { (void)fchmod(fd, 0600); (void)write_all(fd, pid, strlen(pid)); close(fd); }
    return 0;
}

static bool notice_type(const char *type) {
    static const char *const types[] = {"run.created", "approval.requested", "step.failed", "step.recovery-required",
        "step.recovery_required", "run.succeeded", "run.failed", "run.cancelled", "run.recovery-required",
        "run.recovery_required", NULL};
    size_t i;
    for (i = 0; types[i]; i++) if (!strcmp(types[i], type)) return true;
    return false;
}

static size_t format_event(char *line, size_t size, const struct wdr_run *run, long sequence, const char *step,
                           const char *type, const char *detail) {
    char id[256], run_id[512], step_json[600], type_json[512], detail_json[3 * WDR_VALUE], now[32];
    time_t clock = time(NULL); struct tm utc;
    wdr_run_line(run, "run-id", id, sizeof(id));
    escape(run_id, sizeof(run_id), id);
    if (step && step[0]) {
        char text[512];
        escape(text, sizeof(text), step);
        snprintf(step_json, sizeof(step_json), "\"%s\"", text);
    } else f_copy(step_json, sizeof(step_json), "null");
    escape(type_json, sizeof(type_json), type);
    escape(detail_json, sizeof(detail_json), detail ? detail : "");
    gmtime_r(&clock, &utc);
    strftime(now, sizeof(now), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return (size_t)snprintf(line, size,
        "{\"schema_version\":1,\"sequence\":%ld,\"occurred_at\":\"%s\",\"run_id\":\"%s\",\"step_id\":%s,\"type\":\"%s\",\"detail\":\"%s\"}\n",
        sequence, now, run_id, step_json, type_json, detail_json);
}

/* One event appended under .events.lock, as workflow_event appends it, and
 * the planning conversation's notice for it. */
int wdr_event(struct wdr_run *run, const char *step, const char *type, const char *detail) {
    char file[F_PATH], lock[F_PATH], line[4 * WDR_VALUE]; size_t size; int fd, status = 1;
    if (wdr_join(file, run->dir, "events.jsonl") || wdr_join(lock, run->dir, ".events.lock") ||
        wdr_private_touch(file) || take_event_lock(run, lock)) return 1;
    size = format_event(line, sizeof(line), run, next_sequence(file), step, type, detail);
    fd = open(file, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) { status = size < sizeof(line) && !write_all(fd, line, size) ? 0 : 1; close(fd); }
    (void)wdr_remove_tree(lock);
    if (status == 0 && notice_type(type) && wdr_run_has(run, "planning-head"))
        (void)wdr_hook(run, NULL, 0, false, "notify", step ? step : "", type);
    return status;
}
