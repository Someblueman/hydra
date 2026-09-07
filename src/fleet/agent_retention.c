#include "agent.h"
#include "task.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Only completed, explicitly retained provider payloads live in this directory.
 * Per-head shell execution locking serializes publication and pruning. */
int agent_retain(const char *head, const char *run, const struct f_capture *capture) {
    char root[F_PATH], stage[F_PATH], destination[F_PATH], path[F_PATH];
    struct stat st; int result = -1; bool staged = false;
    if (!f_name(run) || strncmp(run, "run_", 4) || f_path(root, sizeof(root), head, "agent-payloads") ||
        f_mkdirs(root) || lstat(root, &st) || !S_ISDIR(st.st_mode) ||
        snprintf(stage, sizeof(stage), "%s/.retain.XXXXXX", root) >= (int)sizeof(stage) || !mkdtemp(stage)) return -1;
    staged = true;
    if (f_path(path, sizeof(path), stage, "stdout") || f_write(path, capture->out, capture->out_bytes > AGENT_OUTPUT_LIMIT ? AGENT_OUTPUT_LIMIT : capture->out_bytes, false) ||
        f_path(path, sizeof(path), stage, "stderr") || f_write(path, capture->err, capture->err_bytes > AGENT_OUTPUT_LIMIT ? AGENT_OUTPUT_LIMIT : capture->err_bytes, false) ||
        f_path(destination, sizeof(destination), root, run) || rename(stage, destination)) goto done;
    staged = false;
    for (;;) {
        DIR *dir = opendir(root); struct dirent *entry; size_t count = 0; char oldest[F_PATH] = ""; time_t oldest_time = 0;
        if (!dir) goto done;
        while ((entry = readdir(dir))) {
            if (strncmp(entry->d_name, "run_", 4) || !f_name(entry->d_name)) continue;
            if (f_path(path, sizeof(path), root, entry->d_name) || lstat(path, &st) || !S_ISDIR(st.st_mode)) { closedir(dir); goto done; }
            count++;
            if (strcmp(entry->d_name, run) && (!*oldest || st.st_mtime < oldest_time)) {
                if (f_copy(oldest, sizeof(oldest), path)) { closedir(dir); goto done; }
                oldest_time = st.st_mtime;
            }
        }
        closedir(dir);
        if (count <= 10) break;
        if (!*oldest || f_remove_tree(oldest)) goto done;
    }
    result = task_sync_dir(root);
done:
    if (staged) f_remove_tree(stage);
    return result;
}
