#define _XOPEN_SOURCE 700
#include "task.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int task_collection_root(const char *project, char root[F_PATH], bool create) {
    char canonical[F_PATH], path[F_PATH], common[F_PATH]; struct f_capture cap = {0}; int dir = -1, child = -1, status = -1;
    char *args[] = {"rev-parse", "--git-common-dir", NULL};
    if (!realpath(project, canonical) || task_git(canonical, args, &cap)) goto done;
    cap.out[strcspn(cap.out, "\r\n")] = '\0';
    if (*cap.out == '/') { if (f_copy(path, sizeof(path), cap.out)) goto done; }
    else if (f_path(path, sizeof(path), canonical, cap.out)) goto done;
    if (!realpath(path, common)) goto done;
    dir = open(common, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (dir < 0) goto done;
    child = task_owned_directory(dir, "hydra", create); close(dir); dir = child; child = -1; if (dir < 0) goto done;
    child = task_owned_directory(dir, "task-results", create); if (child < 0) goto done;
    if (f_path(path, sizeof(path), common, "hydra") || f_path(root, F_PATH, path, "task-results")) goto done;
    status = 0;
done:
    if (dir >= 0) close(dir);
    if (child >= 0) close(child);
    f_capture_free(&cap); return status;
}
