#define _XOPEN_SOURCE 700
#include "fleet/workflow/workflow_task.h"
#include "fleet/support/files.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* POSIX record locks survive exec but are not inherited by forked workers.
 * Keep ownership in the coordinator shell, without a stale-lock deletion race. */
int wt_drive(const char *run) {
    char path[F_PATH], *id = NULL; struct stat st; int fd = -1, owner = -1;
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    if (f_path(path, sizeof(path), run, "run-id") || !(id = f_read(path, 128))) goto done;
    id[strcspn(id, "\r\n")] = '\0';
    if (!f_name(id) || f_path(path, sizeof(path), run, "coordinator.lock")) goto done;
    fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) goto done;
    owner = fcntl(fd, F_DUPFD, 64); close(fd); fd = -1;
    if (owner < 0 || fcntl(owner, F_SETLK, &lock) || setenv("HYDRA_WORKFLOW_LOCKED_RUN", run, 1)) goto done;
    execl(f_hydra, f_hydra, "workflow", "resume", id, (char *)NULL);
done:
    if (fd >= 0) close(fd);
    if (owner >= 0) close(owner);
    free(id); return -1;
}
