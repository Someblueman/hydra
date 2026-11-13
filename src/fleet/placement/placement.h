#ifndef HYDRA_FLEET_PLACEMENT_H
#define HYDRA_FLEET_PLACEMENT_H
#include "fleet/fleet.h"
#include "fleet/support/json.h"
#include <stdint.h>

/* Automatic placement of new, unassigned remote tasks (fleet-placement
 * schema 1, placement policy version 1). The submitter observes candidate
 * hosts, filters and ranks them with a pure function, records the decision
 * durably under $HYDRA_HOME/fleet/placements/TASK_ID/placement.json, and
 * dispatches to the chosen receiver. Receiver admission remains final; the
 * record is an inspectable, replayable account of why a host was chosen. */
#define PLACEMENT_SCHEMA 1
#define PLACEMENT_POLICY_VERSION 1
#define PLACEMENT_ESTIMATES_SCHEMA 1
#define PLACEMENT_HOSTS 16
#define PLACEMENT_SEPARATIONS 8
#define PLACEMENT_RECORD_LIMIT (1024U * 1024U)
#define PLACEMENT_GENERATION_MAX 1000000

/* Policy knobs. The file is optional; an invalid file fails closed (NULL). */
json_object *placement_policy_defaults(void);
json_object *placement_policy_load(void);
/* Static placement inputs derived from a validated task specification. */
json_object *placement_inputs(json_object *spec);
/* Bounded observation of one alias: handshake plus admission status. Errors are
 * recorded in the returned object; the call never returns NULL. */
json_object *placement_observe(const char *alias, const char *required_labels, unsigned seconds, int64_t now, bool drained);
/* Pure, deterministic filter and ranking. excluded/separation are arrays of
 * alias strings. *decision receives {host|null, reason}; caller owns both. */
json_object *placement_decide(json_object *inputs, json_object *observations, json_object *policy,
                              json_object *excluded, json_object *separation, int64_t now, json_object **decision);
/* Typed, versioned estimate hooks. They are recorded as unavailable and never
 * used in ranking until measured evidence exists. */
json_object *placement_estimates(void);
/* Record store. */
int placement_task_id(const char *key, char id[70]);
int placement_root(char root[F_PATH], bool create);
int placement_directory(const char *task_id, char directory[F_PATH], bool create);
json_object *placement_record_read(const char *task_id);
int placement_record_write(json_object *record);
/* Canonical digest of a record without its sha256 member; 0 on success. */
int placement_record_digest(json_object *record, char digest[65]);
bool placement_drained(const char *alias);
json_object *placement_drained_list(void);
/* Ownership fence for results: NULL admits, otherwise an error envelope. host
 * may be NULL for a file-sourced result. */
json_object *placement_result_fence(json_object *receipt, const char *host);
/* Current generation for a task with a record, 0 when none. */
int placement_generation(const char *task_id, char host[128]);
/* Submission driver. Takes a validated package (borrowed), the submission key,
 * an optional trust digest of the prepared package, the placement mode
 * ("auto" or an alias), optional separation task IDs and the transport deadline. */
json_object *placement_submit(json_object *package, const char *key, const char *trust, const char *mode,
                              json_object *separate_from, unsigned seconds);
json_object *placement_cli(int argc, char **argv);
#endif
