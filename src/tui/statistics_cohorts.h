#ifndef HYDRA_TUI_STATISTICS_COHORTS_H
#define HYDRA_TUI_STATISTICS_COHORTS_H
#include "internal.h"
/* The remote cohorts page of Stats (metric page HS_METRICS+1, drawn by
 * stats_cohorts_page): recorded task evidence grouped by location, host,
 * placement, assignment generation and provider. Measurements the protocol
 * did not supply are shown as unknown with the recorded reason, never as
 * zero. The page exists when a step of the sample ran as a remote task. */
#define STATISTICS_COHORT_PAGE (HS_METRICS + 1)
bool statistics_cohorts_recorded(const struct hs_model *m);
/* Cycles the location filter: all, local, remote, then each recorded host. */
void statistics_location_cycle(struct statistics_view *v, int direction);
/* Formats a remote measurement for a scope: unknown unless every eligible
 * step reported it; partial coverage shows the value with its n/N. */
void statistics_remote_text(enum hs_remote_count count, uint64_t value, size_t known, size_t eligible, char *out, size_t size);
#endif
