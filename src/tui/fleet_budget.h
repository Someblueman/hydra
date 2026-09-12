#ifndef HYDRA_FLEET_BUDGET_H
#define HYDRA_FLEET_BUDGET_H

/* Fleet visual data performs list and overview observations serially. Each
 * observation has a handshake and an action request, so four bounded remote
 * phases can occur before the adapter receives a complete snapshot. */
#define HYDRA_FLEET_TUI_REQUEST_SECONDS 3U
#define HYDRA_FLEET_TUI_PHASES 4U
#define HYDRA_FLEET_TUI_CAPTURE_OVERHEAD_MS 1000L
#define HYDRA_FLEET_TUI_CAPTURE_BUDGET_MS \
    ((long)HYDRA_FLEET_TUI_PHASES * (long)HYDRA_FLEET_TUI_REQUEST_SECONDS * 1000L + \
     HYDRA_FLEET_TUI_CAPTURE_OVERHEAD_MS)

/* Local state observation is a single shell transaction. Keep startup and
 * refresh bounded while allowing the measured ten-head snapshot to complete. */
#define HYDRA_TUI_LOCAL_CAPTURE_BUDGET_MS 10000L

#endif
