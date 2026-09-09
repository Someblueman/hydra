#include "fleet/workflow/workflow_contract.h"
#include "fleet/workflow/workflow_task.h"
#include "fleet/plan/plan.h"
#include <string.h>

json_object *wc_provenance(const char *run, json_object *reference) {
    json_object *binding = NULL, *owner = NULL, *out = NULL;
    const char *kind = f_string(reference, "provenance"), *hash = NULL;
    if (!strcmp(kind, "plan")) {
        owner = plan_run_definition(run);
        binding = json_object_get(f_field(owner, "source")); hash = f_string(binding, "sha256");
    } else {
        owner = wt_bindings(run);
        json_object *request = json_object_new_object();
        f_string_add(request, "source_step", f_string(reference, "step"));
        if (owner) binding = wt_source_binding(run, owner, request);
        json_object_put(request); hash = f_string(binding, "result_sha256");
    }
    const char *commit = f_string(binding, "commit");
    if (commit && hash) {
        out = json_object_new_object(); f_string_add(out, "source_commit", commit); f_string_add(out, "source_sha256", hash);
    }
    json_object_put(binding); json_object_put(owner); return out;
}
