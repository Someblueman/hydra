#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/auth/agent_auth.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int auth_hash(const char *text, char digest[65]) {
    f_sha256_text(text, strlen(text), digest); return 0;
}
int auth_path(const char *agent, char path[F_PATH]) {
    const char *root = NULL, *suffix = NULL; char base[F_PATH];
    if (!strcmp(agent, "codex")) { root = getenv("CODEX_HOME"); suffix = "auth.json"; if (!root) suffix = ".codex/auth.json"; }
    else if (!strcmp(agent, "pi")) { root = getenv("PI_CODING_AGENT_DIR"); suffix = "auth.json"; if (!root) suffix = ".pi/agent/auth.json"; }
    else if (!strcmp(agent, "opencode")) { root = getenv("XDG_DATA_HOME"); suffix = "opencode/auth.json"; if (!root) suffix = ".local/share/opencode/auth.json"; }
    else if (!strcmp(agent, "claude")) { root = getenv("CLAUDE_CONFIG_DIR"); suffix = ".credentials.json"; if (!root) suffix = ".claude/.credentials.json"; }
    else return -1;
    if (!root) root = getenv("HOME");
    if (!root || root[0] != '/' || !realpath(root, base)) return -1;
    return f_path(path, F_PATH, base, suffix);
}
/* Walk absolute paths without following symlinks. System ancestors may be root
 * owned; the credential's immediate parent must be owned and not shared writable.
 * Sticky temporary roots are permitted for isolated test and configured homes. */
static int parent_open(const char *path, bool create, char leaf[F_PATH]) {
    char copy[F_PATH], *part, *next, *save = NULL; int fd = -1, child = -1; struct stat st;
    errno = EINVAL;
    if (!path || path[0] != '/' || f_copy(copy, sizeof(copy), path + 1)) return -1;
    fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    part = strtok_r(copy, "/", &save);
    while (fd >= 0 && part) {
        errno = EPERM;
        if (!strcmp(part, ".") || !strcmp(part, "..")) break;
        next = strtok_r(NULL, "/", &save);
        if (!next) {
            if (fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022) || f_copy(leaf, F_PATH, part)) break;
            return fd;
        }
        if (fstat(fd, &st) || (st.st_uid != 0 && st.st_uid != geteuid()) || ((st.st_mode & 0022) && !(st.st_mode & 01000))) break;
        child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (child < 0 && errno == ENOENT && create && st.st_uid == geteuid() && !(st.st_mode & 0022)) {
            if (mkdirat(fd, part, 0700) && errno != EEXIST) break;
            child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        close(fd); fd = child; part = next;
    }
    if (fd >= 0) close(fd);
    return -1;
}
static int read_at(int dir, const char *leaf, json_object **value, char digest[65]) {
    int fd = -1, rc = -1; struct stat st; char *text = NULL; size_t used = 0; ssize_t n;
    *value = NULL;
    fd = openat(dir, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) { f_copy(digest, 65, "absent"); return 0; }
        return -1;
    }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 0077) || st.st_size > AUTH_LIMIT || st.st_size <= 0) goto done;
    text = malloc(AUTH_LIMIT + 1); if (!text) goto done;
    while (used <= AUTH_LIMIT) {
        n = read(fd, text + used, AUTH_LIMIT + 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) goto done;
        if (!n) break;
        used += (size_t)n;
    }
    if (used > AUTH_LIMIT || memchr(text, 0, used)) goto done;
    text[used] = 0;
    if (!(*value = f_parse(text)) || auth_hash(text, digest)) goto done;
    rc = 0;
done:
    if (rc) { json_object_put(*value); *value = NULL; }
    free(text); close(fd); return rc;
}
int auth_read(const char *path, json_object **value, char digest[65]) {
    char leaf[F_PATH]; int fd, rc; struct stat st;
    *value = NULL;
    fd = parent_open(path, false, leaf);
    if (fd < 0) {
        /* ENOENT is safe only if the walk itself failed on a missing component. */
        if (errno == ENOENT && lstat(path, &st) && errno == ENOENT) { f_copy(digest, 65, "absent"); return 0; }
        return -1;
    }
    rc = read_at(fd, leaf, value, digest); close(fd); return rc;
}
/* Staged, compared and published with f_stage_at and f_publish_at under the
 * store's lock. The destination is read again once the new bytes are staged:
 * the agent's own CLI writes this file without taking the lock, so the
 * comparison sits as close to the publication as it can. A credential that
 * was absent is linked into place, which never replaces a file that appeared
 * meanwhile. */
int auth_store(const char *path, const char *before, json_object *value) {
    char leaf[F_PATH], digest[65], temp[F_TEMP_NAME] = ""; int dir = -1, lock = -1, rc = -1;
    json_object *old = NULL; struct stat st; const char *text = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    size_t size = strlen(text);
    if (size > AUTH_LIMIT || (dir = parent_open(path, true, leaf)) < 0) goto done;
    lock = openat(dir, ".hydra-auth.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (lock < 0 || fstat(lock, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 0077) || flock(lock, LOCK_EX | LOCK_NB)) goto done;
    if (read_at(dir, leaf, &old, digest) || strcmp(before, digest)) { rc = -2; goto done; }
    json_object_put(old); old = NULL;
    if (f_stage_at(dir, leaf, text, size, temp)) goto done;
    if (read_at(dir, leaf, &old, digest) || strcmp(before, digest)) { rc = -2; goto done; }
    rc = f_publish_at(dir, temp, leaf, strcmp(before, "absent") != 0) ? -1 : 0;
    temp[0] = '\0';
done:
    json_object_put(old);
    if (dir >= 0 && temp[0]) unlinkat(dir, temp, 0);
    if (lock >= 0) close(lock);
    if (dir >= 0) close(dir);
    return rc;
}
