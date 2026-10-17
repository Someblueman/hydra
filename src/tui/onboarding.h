#ifndef HYDRA_TUI_ONBOARDING_H
#define HYDRA_TUI_ONBOARDING_H
#include "setup_flow.h"
#include "../termviz/input.h"
#include <stdbool.h>
#include <stddef.h>
/* Onboarding operator view (H5): the H1-H3 records (candidates and their
 * qualification, live-source reports with snapshot diffs, enrollment intents
 * and per-host apply progress) as the control centre shows them, and the
 * `hydra fleet discover|qualify|enroll` children it delegates every action
 * to. The model is read-only data parsed from CLI envelopes: the TUI never
 * decides identity, typed state or progress itself and never writes a
 * record. Bounds follow the CLI's own (100 candidates, four sources). */
#define ONB_CANDIDATES 100
#define ONB_SOURCES 4
#define ONB_RECORDS 100
#define ONB_HOSTS 100
#define ONB_DIFF_NAMES 16
#define ONB_RECORDED 4
#define ONB_ARGV_MAX 232
#define ONB_ARG_TEXT 320
#define ONB_PATH 320

struct onb_source_ref { char kind[32], locator[ONB_PATH], name[128], freshness[48], scope[128], client[48], labels[200]; long long observed_at, age_seconds; };
struct onb_recorded { char fingerprint[160], source[120]; };
struct onb_revocation { char scope[16], id[200], reason[200], user[64], host[64]; long long revoked_at; bool present; };
/* One candidate of candidate schema 1 plus its enrollment row, if any. */
struct onb_candidate {
    char candidate_id[80], target[256], status[32];
    struct onb_source_ref sources[ONB_SOURCES]; size_t source_count;
    bool resolution_ok; char policy_sha256[72], user[128], hostname[256], port[16], ssh_config[ONB_PATH];
    bool qualified, qualification_ok; char code[48], message[256], recovery[256], fingerprint[160];
    char platform[48], hydra_version[48], freshness[96]; long long qualified_at, attempts; int fleet_protocol;
    /* A host_key_changed qualification: the presented and recorded keys. */
    bool key_changed; char presented[160], key_type[32], known_hosts[ONB_PATH]; long long known_hosts_line;
    struct onb_recorded recorded[ONB_RECORDED]; size_t recorded_count;
    struct onb_revocation revocation;
};
/* A live-source record that is reported but never imported. */
struct onb_record { char source[48], name[128], target[256], reason[32]; struct onb_revocation revocation; };
struct onb_change { char name[128], previous_target[256], target[256]; };
struct onb_source_report {
    char kind[32], locator[ONB_PATH], scope[128], client[48], status[16], reason[48];
    long long observed_at, bytes, record_count, credentials_omitted, skipped;
    bool cached, first_observation; long long cache_age, cache_count; char cache_status[16];
    long long added_count, removed_count, changed_count, conflicting_count, revoked_count;
    char added[ONB_DIFF_NAMES][128], removed[ONB_DIFF_NAMES][128]; size_t added_n, removed_n;
    struct onb_change changed[ONB_DIFF_NAMES]; size_t changed_n;
};
/* One reviewed host of an intent, with the apply progress recorded for it. */
struct onb_host {
    char alias[128], candidate_id[80], target[256], principal[128], project[ONB_PATH], fingerprint[160];
    char package_sha256[72], prefix[ONB_PATH], ssh_config[ONB_PATH], operation_id[200], status[32], phase[16];
    bool attempted, key_changed; char presented[160], reviewed[160];
    struct onb_revocation revocation;
};
struct onb_intent {
    bool loaded, progress_recorded, partial;
    char path[ONB_PATH], digest[72], progress_path[ONB_PATH]; long long reviewed_at;
    struct onb_host hosts[ONB_HOSTS]; size_t host_count;
};
struct onb_envelope { bool parsed, ok; int exit_status; char command[48], code[48], message[320], recovery[480]; };
struct onb_model {
    struct onb_envelope last;
    char export_path[ONB_PATH];
    long long observed_at, pending, remaining, processed, revoked_count; bool complete, partial, diff_requested;
    char capability[64];
    struct onb_candidate candidates[ONB_CANDIDATES]; size_t candidate_count;
    struct onb_source_report sources[ONB_SOURCES]; size_t source_count;
    struct onb_record records[ONB_RECORDS]; size_t record_count;
    struct onb_intent intent;
};

/* Parses one CLI envelope. Returns 0 and fills the model part the envelope's
 * command describes (fleet-discovery: candidates, sources and records;
 * fleet-enrollment-review / -status: the intent; fleet-enrollment: apply
 * progress merged into the intent), -1 when the text is not an envelope.
 * m->last always reports the envelope header. */
int onb_parse(struct onb_model *m, const char *text, size_t length, int exit_status);
int onb_parse_file(struct onb_model *m, const char *path);
void onb_envelope_only(struct onb_envelope *e, const char *text, size_t length, int exit_status);

/* ---- Typed state, rows and filters ---- */
enum onb_typed { ONB_NEW, ONB_QUALIFIED, ONB_FAILED, ONB_CONFLICTING, ONB_REVOKED, ONB_REVIEWED, ONB_UNKNOWN, ONB_ENROLLED, ONB_TYPED_COUNT };
const char *onb_typed_name(enum onb_typed typed);
const struct onb_host *onb_host_for(const struct onb_model *m, const char *candidate_id);
enum onb_typed onb_candidate_typed(const struct onb_model *m, const struct onb_candidate *c);
/* Short state text ("qualified", "key changed", "apply: outcome unknown"). */
void onb_candidate_state(const struct onb_model *m, const struct onb_candidate *c, char *out, size_t size);
const char *onb_candidate_source(const struct onb_candidate *c);
const char *onb_candidate_platform(const struct onb_candidate *c);
void onb_candidate_age(const struct onb_candidate *c, long long now, char *out, size_t size);
enum onb_row_kind { ONB_ROW_CANDIDATE, ONB_ROW_RECORD };
struct onb_row { enum onb_row_kind kind; size_t index; };
/* Filters: typed state (-1 for all), source kind, platform, label text and
 * search text (empty for none). */
struct onb_filter { int typed; char source[32], platform[48], label[64], search[128]; };
bool onb_row_visible(const struct onb_model *m, const struct onb_filter *f, const struct onb_row *row);
size_t onb_rows(const struct onb_model *m, const struct onb_filter *f, struct onb_row *rows, size_t capacity);
/* Distinct source kinds / platforms for the filter cycles; next value after
 * current (empty for all). */
void onb_next_source(const struct onb_model *m, char *current, size_t size);
void onb_next_platform(const struct onb_model *m, char *current, size_t size);
void onb_filter_text(const struct onb_filter *f, char *out, size_t size);
void onb_time_text(long long at, char *out, size_t size);

/* ---- Commands ---- */
struct onb_argv { char words[ONB_ARGV_MAX][ONB_ARG_TEXT]; size_t count; };
void onb_argv_reset(struct onb_argv *a);
bool onb_argv_add(struct onb_argv *a, const char *word);
/* The selection a discover or qualify run is made of. */
#define ONB_SELECTION_TEXT 2048
struct onb_selection { char aliases[ONB_SELECTION_TEXT], source[ONB_PATH], names[ONB_SELECTION_TEXT], inventory[ONB_PATH], config[ONB_PATH]; };
/* argv[0] is `hydra`. False when the selection is empty or exceeds limits. */
bool onb_discover_argv(struct onb_argv *a, const struct onb_selection *s, bool qualify, const char *export_path);
/* The selection a loaded export was made of (ssh aliases, a plain
 * inventory and live sources); false when it cannot be reconstructed. */
bool onb_export_selection(const struct onb_model *m, struct onb_selection *s);
struct onb_review { char project[ONB_PATH], alias[128], package[ONB_PATH], sha256[72], prefix[ONB_PATH]; };
bool onb_review_argv(struct onb_argv *a, const char *export_path, const char *candidate_id, const struct onb_review *r, const char *intent_path);
bool onb_status_argv(struct onb_argv *a, const char *intent_path);
bool onb_apply_argv(struct onb_argv *a, const char *intent_path, const char *digest);
/* True when argv is exactly `hydra fleet enroll apply --input INTENT
 * --confirm DIGEST --json` for the intent the operator was shown and the
 * digest they confirmed; anything else never runs. */
bool onb_apply_argv_valid(const struct onb_argv *a, const char *intent_path, const char *digest);
/* True when argv is a `hydra fleet discover|qualify ...` of known options
 * only, whose --progress (qualify) lies under the onboarding directory. */
bool onb_discover_argv_valid(const struct onb_argv *a, const char *directory);
bool onb_hex_digest(const char *text);
bool onb_digest_confirms(const char *typed, const char *digest);
/* Simple validation shared with the forms; NULL or a message. */
const char *onb_path_problem(const char *path, bool required);
#endif
