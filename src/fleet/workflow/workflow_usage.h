#ifndef HYDRA_WORKFLOW_USAGE_H
#define HYDRA_WORKFLOW_USAGE_H
#include "fleet/fleet.h"
/* Print one bounded "usage" row per step of a recorded run whose latest
 * attempt delegated to a headless agent: profile, executable version, model,
 * effort and token counts, "-" where the receipt records nothing. Read-only;
 * run is a borrowed run directory beneath projects/<id>/workflows/runs. */
int wu_usage_tsv(const char *run);
#endif
