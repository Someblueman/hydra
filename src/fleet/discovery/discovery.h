#ifndef HYDRA_HOST_DISCOVERY_H
#define HYDRA_HOST_DISCOVERY_H
#include "fleet/fleet.h"
#include <json-c/json.h>

#define HD_HOSTS 100
#define HD_QUALIFY_BATCH 16
/* Opt-in live sources: at most four per invocation, one client process each,
 * 1 MiB of client output or file bytes and 100 records per source. */
#define HD_SOURCES 4
#define HD_SOURCE_BYTES (1024U * 1024U)
#define HD_SOURCE_RECORDS 100
#define HD_SOURCE_INCLUDES 16
struct hd_options {
    const char *ssh[HD_HOSTS], *select[HD_HOSTS];
    size_t ssh_count, select_count;
    const char *inventory, *config, *capability;
    const char *progress;
    const char *sources[HD_SOURCES];
    size_t source_count;
    unsigned seconds;
    bool probe, diff;
    /* Live acquisition reports (hd_live_acquire); owned by the CLI entry. */
    json_object *live;
};
struct hd_progress {
    int lock;
    char path[F_PATH];
    json_object *record;
};
/* Inputs are borrowed. Returned JSON belongs to the caller. Config output is
 * caller-owned and must be unlinked after all SSH children have exited. */
json_object *hd_cli(int argc, char **argv);
json_object *hd_sources(const struct hd_options *options);
/* Borrow rows/options; progress owns its record/lock until hd_progress_close. */
json_object *hd_progress_open(struct hd_progress *progress, const struct hd_options *options, json_object *rows);
int hd_progress_save(struct hd_progress *progress, json_object *rows);
void hd_progress_close(struct hd_progress *progress);
/* Private $HYDRA_HOME/fleet/discovery[/child] directory, owner-only. */
int hd_private_directory(char path[F_PATH], const char *child);
json_object *hd_batch(const struct hd_options *options, json_object *rows);
json_object *hd_resolve(const char *target, const char *config, unsigned seconds);
json_object *hd_probe(json_object *candidate, const char *config, const struct hd_options *options);
int hd_config(char path[F_PATH], const char *source);
bool hd_text(const char *text, size_t limit);
bool hd_keys(json_object *object, const char *const *keys);
void hd_id(char id[520], const char *target);
/* Live sources. Selectors are KIND[:LOCATOR]: ssh-config[:FILE],
 * ansible-inventory:FILE, dns-sd[:DOMAIN]. Acquire returns one report per
 * selector (NULL for an invalid selector); import adds the importable records
 * as candidates and marks matched --select names; public strips records. */
json_object *hd_candidate_add(json_object *rows, const char *target);
void hd_source_add(json_object *row, const char *kind, const char *locator, const char *name, int64_t observed, json_object *labels);
bool hd_source_valid(const char *selector);
json_object *hd_live_acquire(const struct hd_options *options);
int hd_live_import(json_object *rows, const struct hd_options *options, bool matched[HD_HOSTS]);
json_object *hd_live_public(json_object *live, bool diff);
bool hd_live_failed(json_object *live);
json_object *hd_cache_summary(const char *kind, const char *locator);
json_object *hd_cache_read(const char *kind, const char *locator);
int hd_cache_write(json_object *report);
/* Adapters and their parsers. Each report has status, records, skipped and
 * credentials_omitted; parsers take borrowed text and return a caller-owned
 * report. */
json_object *hd_scan_ssh_config(const char *path);
json_object *hd_parse_ssh_hosts(const char *text, const char *path, json_object *report, unsigned depth);
json_object *hd_scan_ansible(const char *path);
json_object *hd_parse_ansible(const char *text);
json_object *hd_scan_dnssd(const char *domain, unsigned seconds);
json_object *hd_parse_avahi(const char *text);
json_object *hd_report(const char *status, const char *reason);
void hd_record_add(json_object *report, const char *name, const char *target, json_object *labels);
void hd_skip(json_object *report, const char *name, const char *reason);
/* Snapshot diff against the previous private cache; marks conflicts. */
void hd_live_diff(json_object *report, json_object *previous);
/* Adds error.host_key: presented/recorded fingerprints with provenance. */
void hd_host_key_detail(json_object *error, json_object *candidate, const char *stderr_text);
#endif
