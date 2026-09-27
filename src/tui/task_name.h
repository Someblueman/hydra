#ifndef HYDRA_TUI_TASK_NAME_H
#define HYDRA_TUI_TASK_NAME_H
#include <stdbool.h>
#include <stddef.h>

/* Derived branches stay short enough for plan head IDs and tmux sessions. */
#define TASK_BRANCH_SLUG_MAX 64U

/* Mirrors validate_branch_name in lib/git.sh (without HYDRA_ALLOW_ADVANCED_REFS)
 * plus Git's rule that no path component starts with '.'. */
bool task_branch_valid(const char *name);
/* Writes the spawn branch for a natural-language task name. A valid name is
 * kept unchanged. Otherwise ASCII letters are lowercased, digits kept, '/'
 * separates components and each other run of bytes becomes one '-', without
 * leading or trailing separators, bounded to TASK_BRANCH_SLUG_MAX bytes.
 * Returns false, with out empty, when no letter or digit remains. */
bool task_branch_name(const char *name, char *out, size_t size);
/* True when the guided local policy can name this recorded head profile as a
 * profile:<name> tool (a plan ID other than none); mirrors
 * workflow_plan_local_profile in lib/workflow_plan_proposal.sh. */
bool task_profile_tool(const char *profile);
#endif
