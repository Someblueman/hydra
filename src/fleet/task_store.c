#define _XOPEN_SOURCE 700
#include "task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The explicitly configured home may resolve through a user-owned symlink.
 * Within it, task storage never follows links or uses writable shared directories. */
int task_owned_directory(int parent, const char *name, bool create) {
    struct stat st; int fd;
    if (create && mkdirat(parent, name, 0700) && errno != EEXIST) return -1;
    fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) { close(fd); return -1; }
    if (fsync(parent)) { close(fd); return -1; }
    return fd;
}
int task_store_root(char root[F_PATH]) {
    char home[F_PATH], fleet_path[F_PATH]; int home_fd, fleet_fd = -1, tasks_fd = -1, status = -1;
    struct stat st;
    if (f_mkdirs(f_home) || !realpath(f_home, home)) return -1;
    home_fd = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (home_fd < 0) return -1;
    if (fstat(home_fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) goto done;
    fleet_fd = task_owned_directory(home_fd, "fleet", true); if (fleet_fd < 0) goto done;
    tasks_fd = task_owned_directory(fleet_fd, "tasks", true); if (tasks_fd < 0) goto done;
    if (f_path(fleet_path, sizeof(fleet_path), home, "fleet") || f_path(root, F_PATH, fleet_path, "tasks")) goto done;
    status = 0;
done:
    if (tasks_fd >= 0) close(tasks_fd);
    if (fleet_fd >= 0) close(fleet_fd);
    close(home_fd); return status;
}
int task_sync_dir(const char *path) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW), status;
    if (fd < 0) return -1;
    status = fsync(fd); close(fd); return status;
}
int task_write_json(const char *directory, const char *name, json_object *value, bool replace) {
    char path[F_PATH]; const char *text = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    if (f_path(path, sizeof(path), directory, name) || f_write(path, text, strlen(text), replace)) return -1;
    return task_sync_dir(directory);
}
json_object *task_read_record(const char *directory, const char *name) {
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW), fd = -1;
    struct stat st; char bytes[16385]; size_t used = 0; json_object *result = NULL;
    if (dir < 0) return NULL;
    if (fstat(dir, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) goto done;
    fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 16384) goto done;
    while (used < sizeof(bytes)) {
        ssize_t n = read(fd, bytes + used, sizeof(bytes) - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) goto done;
        if (!n) break;
        used += (size_t)n;
    }
    if (used > 16384 || memchr(bytes, '\0', used)) goto done;
    bytes[used] = '\0'; result = f_parse(bytes);
done:
    if (fd >= 0) close(fd);
    close(dir); return result;
}
