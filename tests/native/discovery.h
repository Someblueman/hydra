#ifndef HYDRA_NATIVE_DISCOVERY_H
#define HYDRA_NATIVE_DISCOVERY_H
#include "support.h"
extern char dc_root[F_PATH], dc_home[F_PATH], dc_config[F_PATH],
    dc_included[F_PATH], dc_calls[F_PATH], dc_pid[F_PATH], dc_errors[F_PATH],
    dc_hydra[F_PATH];
void dc_setup(void);
void dc_cleanup(void);
json_object *dc_cli(const char *action, const char *const extra[], bool ok);
struct nt_child dc_start(const char *action, const char *const extra[]);
json_object *dc_rows(json_object *response);
size_t dc_call_count(void);
void dc_snapshot(char out[F_PATH], const char *kind, json_object *records,
                 int64_t observed);
void dc_error(json_object *response, const char *code);
void dc_progress_cases(void);
#define DC(op, ok, ...) dc_cli(op, (const char *[]){__VA_ARGS__, NULL}, ok)
#endif
