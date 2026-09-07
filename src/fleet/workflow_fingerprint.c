#include "workflow_data.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Bind tracked content, modes, names, untracked bytes, and HEAD without modifying
 * the index. Git quotes unusual names for its own --stdin-paths reader. */
int wd_fingerprint(const char *worktree, char digest[65]) {
    char scratch[] = "/tmp/hydra-workflow-binding.XXXXXX";
    char *head[] = {"git", "-C", (char *)worktree, "rev-parse", "HEAD", NULL};
    char *diff[] = {"git", "-C", (char *)worktree, "diff", "--no-ext-diff", "--no-textconv", "--binary", "HEAD", "--", NULL};
    char *status_argv[] = {"git", "-C", (char *)worktree, "status", "--porcelain=v1", "--untracked-files=all", NULL};
    char *untracked[] = {"git", "-C", (char *)worktree, "ls-files", "--others", "--exclude-standard", NULL};
    char *hash[] = {"git", "-C", (char *)worktree, "hash-object", "--no-filters", "--stdin-paths", NULL};
    char **commands[] = {head, diff, status_argv, untracked};
    const char *keys[] = {"head", "diff", "status", "untracked"};
    json_object *binding = json_object_new_object(); struct f_capture cap = {0}; size_t i; int status = -1;
    if (!mkdtemp(scratch)) { json_object_put(binding); return -1; }
    for (i = 0; i < 4; i++) {
        if (f_run(commands[i], NULL, 0, 30, &cap) || cap.status) goto done;
        f_string_add(binding, keys[i], cap.out); f_capture_free(&cap);
    }
    if (f_run(hash, f_string(binding, "untracked"), strlen(f_string(binding, "untracked")), 30, &cap) || cap.status) goto done;
    f_string_add(binding, "untracked_hashes", cap.out);
    status = task_json_hash(binding, scratch, digest);
done:
    f_capture_free(&cap); json_object_put(binding); f_remove_tree(scratch); return status;
}
