#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
#define U(marker, timeout) kt_until(&s, (marker), (timeout))
int main(void) {
    struct kt_session s;
    char draft[4096], policy[4096], path[4096], text[8192];
    int sizes[][2] = {{40, 10}, {80, 24}, {140, 40}};
    size_t i;
    json_object *changed;
    kt_init();
    hf_init(&f, "hydra-plan-workspace", "repo", true, false);
    kt_format(draft, sizeof(draft), "%s/draft.json", f.base);
    kt_format(policy, sizeof(policy), "%s/policy.json", f.base);
    kt_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
    kt_copy(path, draft, false);
    kt_format(path, sizeof(path), "%s/tests/fixtures/plan/policy.json", f.root);
    kt_copy(path, policy, false);
    hf_commit_init(&f);
    hf_open(&f, &s);
    U("PLAN TOGETHER", 3);
    kt_send(&s, "I");
    U("Draft JSON path:", 3);
    kt_send(&s, draft);
    kt_send(&s, "\r");
    U("Policy JSON path:", 3);
    kt_send(&s, policy);
    kt_send(&s, "\r");
    U("Revision 1 / DRAFT", 3);
    kt_send(&s, "VB");
    U("READY / awaiting approval", 30);
    kt_send(&s, "z");
    U("READY / awaiting approval", 3);
    CHECK(kt_contains(&s, "Revision 1"), "first revision");
    kt_send(&s, "jjjjB");
    U("PLAN TOGETHER", 3);
    kt_send(&s, "B");
    U("PLAN OVERVIEW", 3);
    CHECK(kt_contains(&s, "Revision 1"), "layout retains revision");
    changed = json_object_from_file(draft);
    CHECK(changed, "draft JSON");
    json_object_object_add(changed, "objective",
                           json_object_new_string("Revised workspace acceptance objective"));
    CHECK(!json_object_to_file(draft, changed), "write revised draft");
    json_object_put(changed);
    U("Revision 2 / DRAFT", 8);
    CHECK(!kt_contains(&s, "READY / awaiting approval"), "revision invalidates approval");
    kt_send(&s, "V");
    U("READY / awaiting approval", 30);
    kt_send(&s, "kkkk");
    U("Revised workspace acceptance objective", 3);
    for (i = 0; i < 3; i++) {
        if (sizes[i][0] == 40) {
            kt_send(&s, "j");
            kt_sleep(.1);
        }
        kt_resize(&s, sizes[i][0], sizes[i][1]);
        kt_pump(&s, .3);
        if (s.screen.overflow) {
            kt_format(path, sizeof(path), "%s/build/plan-resize-failure.ansi", f.root);
            kt_write(path, s.raw ? s.raw : "");
            fprintf(stderr, "%s\n", kt_text(&s));
        }
        CHECK(!s.screen.overflow, "plan resize overflow");
    }
    kt_write(draft, "{\"broken\":");
    U("Revision 3 / DRAFT", 8);
    kt_send(&s, "V");
    U("INVALID", 30);
    kt_send(&s, "CA");
    U("PLAN TOGETHER", 3);
    kt_close(&s, "q", 0, 0);
    kt_format(text, sizeof(text),
              "PASS plan workspace: compilation, revised scope, invalid JSON, reversible layouts "
              "and termios (%s)",
              f.base);
    hf_finish(&f);
    puts(text);
    return 0;
}
