#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Create only missing files through descriptor-relative, link-free directories.
 * Existing bytes are verified by the caller and are never overwritten. A
 * file appears under its name only whole (f_write_at): a write that fails
 * leaves no partial file for a later call to take as existing. */
int task_collection_file(const char *root, const char *relative, const char *hex) {
    char path[F_PATH], *part, *next; int dir = -1, fd = -1, status = -1; unsigned char *bytes = NULL; size_t i, size;
    struct stat st;
    if (!task_path(relative) || !hex || strlen(hex) % 2 || !task_hex(hex, strlen(hex)) || f_copy(path, sizeof(path), relative)) return -1;
    size = strlen(hex) / 2; if (size > TASK_PACKAGE_LIMIT) return -1;
    dir = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (dir < 0) return -1;
    for (part = path; (next = strchr(part, '/')); part = next + 1) {
        *next = '\0'; fd = task_owned_directory(dir, part, true); close(dir); dir = fd; fd = -1; if (dir < 0) goto done;
    }
    if (!fstatat(dir, part, &st, AT_SYMLINK_NOFOLLOW)) { status = 0; goto done; }
    if (errno != ENOENT) goto done;
    bytes = malloc(size ? size : 1); if (!bytes) goto done;
    for (i = 0; i < size; i++) {
        unsigned high = hex[2*i] <= '9' ? (unsigned)(hex[2*i] - '0') : (unsigned)(hex[2*i] - 'a' + 10);
        unsigned low = hex[2*i+1] <= '9' ? (unsigned)(hex[2*i+1] - '0') : (unsigned)(hex[2*i+1] - 'a' + 10);
        bytes[i] = (unsigned char)(high * 16 + low);
    }
    if (!f_write_at(dir, part, (const char *)bytes, size, false) || errno == EEXIST) status = 0;
done:
    if (dir >= 0) close(dir);
    free(bytes); return status;
}
