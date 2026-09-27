#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
#define U(marker, timeout) tv_until(&s, (marker), (timeout))
/* The fixture plan's verifier becomes a proposal asset run as @input/verifier. */
static void asset_draft(const char *draft) {
    char path[4096];
    json_object *plan, *data, *verify, *argv;
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
    plan = json_object_from_file(path);
    CHECK(plan != NULL, "fixture plan JSON");
    json_object_object_get_ex(plan, "data", &data);
    json_object_object_add(json_object_object_get(data, "inputs"), "verifier",
                           json_tokener_parse("{\"asset\":\"verifier\",\"type\":\"file\",\"max_bytes\":512}"));
    verify = json_object_object_get(json_object_object_get(data, "steps"), "verify");
    json_object_object_add(json_object_object_get(verify, "inputs"), "verifier",
                           json_tokener_parse("{\"input\":\"verifier\"}"));
    argv = json_tokener_parse("[\"sh\",\"@input/verifier\"]");
    json_object_object_add(json_object_object_get(json_object_array_get_idx(json_object_object_get(plan, "steps"), 2), "args"),
                           "argv", argv);
    CHECK(!json_object_to_file(draft, plan), "write asset draft");
    json_object_put(plan);
}
int main(void) {
    struct tv_session s;
    char directory[4096], draft[4096], policy[4096], asset[4096], path[4096], text[8192];
    tv_init();
    hf_init(&f, "hydra-plan-assets", "repo", true, false);
    tv_format(directory, sizeof(directory), "%s/plan/assets", f.base);
    tv_mkdir(directory);
    tv_format(draft, sizeof(draft), "%s/plan/draft.json", f.base);
    tv_format(policy, sizeof(policy), "%s/plan/policy.json", f.base);
    tv_format(asset, sizeof(asset), "%s/verifier", directory);
    asset_draft(draft);
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/policy.json", f.root);
    tv_copy(path, policy, false);
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/repo/check.sh", f.root);
    tv_copy(path, asset, false);
    hf_commit_init(&f);
    hf_open(&f, &s);
    U("PLAN TOGETHER", 3);
    tv_send(&s, "I");
    U("Draft JSON path:", 3);
    tv_send(&s, draft);
    tv_send(&s, "\r");
    U("Policy JSON path:", 3);
    tv_send(&s, policy);
    tv_send(&s, "\r");
    U("Revision 1 / DRAFT", 3);
    tv_send(&s, "V");
    U("READY / awaiting approval", 30);
    tv_write(asset, "#!/bin/sh\nexit 1\n");
    U("Revision 2 / DRAFT", 8);
    CHECK(!tv_contains(&s, "READY / awaiting approval"), "a changed asset invalidates the validated revision");
    tv_send(&s, "V");
    U("READY / awaiting approval", 30);
    tv_close(&s, "q", 0, 0);
    tv_format(text, sizeof(text),
              "PASS plan assets: the draft's sibling assets compile into the revision and a changed asset "
              "requires validation again (%s)",
              f.base);
    puts(text);
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    return 0;
}
