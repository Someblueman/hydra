#ifndef HYDRA_TUI_PLAN_DIAGNOSTICS_H
#define HYDRA_TUI_PLAN_DIAGNOSTICS_H
#include <stddef.h>

/* Summarizes a failed `workflow plan compile` response for people: each
 * data.diagnostics message and its optional recovery, followed by the raw
 * response for technical detail. Returns the summary length, or 0 when the
 * response has no readable diagnostics and should be shown unchanged. */
size_t plan_failure_summary(const char *raw, char *out, size_t size);
#endif
