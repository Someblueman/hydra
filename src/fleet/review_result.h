#ifndef HYDRA_REVIEW_RESULT_H
#define HYDRA_REVIEW_RESULT_H

#include "fleet/fleet.h"
#include "fleet/support/json.h"

/* Read-only result review of one compiled plan run. run and project are
 * borrowed directories; delivery is the borrowed output of plan_delivery or
 * NULL when the run is not (yet) verified. Returns a caller-owned object, or
 * NULL for a run without an accepted compiled plan. Git is only queried, never
 * changed; the worker branch is reported as currently observed. */
json_object *review_result(const char *run, const char *project, json_object *delivery);

/* Borrowed exec receipt of one workflow attempt: the agent profile, executable
 * version, model/effort when recorded and token usage. NULL members stay
 * unknown; a missing receipt returns NULL. Caller owns the result. */
json_object *review_attempt_agent(const char *project, const char *attempt, char exec_log[F_PATH]);

/* Emit the human result review as lines through emit(context, line). */
void review_result_text(json_object *result, void (*emit)(void *context, const char *line), void *context);

#endif
