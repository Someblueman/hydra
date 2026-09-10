#ifndef HYDRA_RETENTION_H
#define HYDRA_RETENTION_H
#include "fleet/support/json.h"
#include <sys/types.h>

#define RT_RECORDS 1024
#define RT_FILES 16384
#define RT_BYTES (16U * 1024U * 1024U)
struct rt_record {
    char path[F_PATH], id[128], project[128], submission_key[160];
    bool task, protected, expired, selected;
    const char *reason;
    int owner;
    int64_t newest, bytes, payload_bytes;
    json_object *files, *refs;
};
struct rt_inventory {
    struct rt_record *records;
    size_t count, files;
    int64_t now, audit_seconds, max_bytes, max_evidence_records;
};
/* All input objects/paths are borrowed. Returned JSON belongs to the caller. */
json_object *retention_cli(int argc, char **argv);
json_object *retention_state(const char *directory);
bool retention_expired(const char *directory);
int rt_scan(struct rt_inventory *inventory);
void rt_release(struct rt_inventory *inventory);
int rt_record_files(struct rt_inventory *inventory, struct rt_record *record);
bool rt_payload(bool task, const char *relative);
int rt_expire(struct rt_inventory *inventory, struct rt_record *record);
int64_t retention_audit_until(const char *directory);
int rt_audit_commit(struct rt_inventory *inventory);
int rt_hash_at(int directory, const char *name, char digest[65]);
int rt_parent(int root, const char *relative, char name[256]);
#endif
