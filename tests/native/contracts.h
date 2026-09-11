#ifndef HYDRA_NATIVE_CONTRACT_CASES_H
#define HYDRA_NATIVE_CONTRACT_CASES_H
#include "support.h"
struct hc_fixture {
  char root[F_PATH], repo[F_PATH], home[F_PATH], manifest[F_PATH],
      graph[F_PATH], run[F_PATH], origin[F_PATH], hydra[F_PATH], fleet[F_PATH];
};
extern struct hc_fixture hc;
void hc_setup(void);
void hc_cleanup(void);
void hc_write(const char *dir, const char *name, const char *text);
void hc_json(const char *dir, const char *name, json_object *value);
json_object *hc_read(const char *dir, const char *name);
struct f_capture hc_command(char *const args[], bool ok);
void hc_native(const char *op, const char *a, const char *b, const char *c,
               const char *d, bool ok);
json_object *hc_data(void);
json_object *hc_contract(const char *unit, const char *kind, const char *schema,
                         json_object *fields);
json_object *hc_declaration(json_object *contract, const char *path);
json_object *hc_value(int duration, const char *candidate, const char *unit);
json_object *hc_step(json_object *manifest, const char *name);
void hc_validate(json_object *manifest, bool ok);
void hc_initialize(json_object *manifest);
void hc_prepare(const char *step, char attempt[F_PATH], bool ok);
void hc_seal(const char *step, const char *attempt, bool ok);
void hc_bind(json_object *plan);
void hc_plan(json_object **plan, json_object **policy);
json_object *hc_compile(json_object *plan, json_object *policy, bool ok);
void hc_data_cases(void);
void hc_plan_cases(void);
void hc_runtime_cases(void);
#endif
