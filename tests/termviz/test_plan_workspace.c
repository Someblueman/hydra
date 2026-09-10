#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
#define U(marker, timeout) tv_until(&s, (marker), (timeout))
int main(void) {
    struct tv_session s;
    char draft[4096], policy[4096], path[4096], text[8192];
    int sizes[][2] = {{40, 10}, {80, 24}, {140, 40}};
    size_t i;
    json_object *changed;
    tv_init();
    hf_init(&f, "hydra-plan-workspace", "repo", true, false);
    tv_format(draft, sizeof(draft), "%s/draft.json", f.base);
    tv_format(policy, sizeof(policy), "%s/policy.json", f.base);
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/plan.json", f.root);
    tv_copy(path, draft, false);
    tv_format(path, sizeof(path), "%s/tests/fixtures/plan/policy.json", f.root);
    tv_copy(path, policy, false);
    hf_commit_init(&f);
    hf_open(&f, &s);
    U("A CONVERSATION", 3);
    tv_send(&s, "P");
    U("Draft JSON path:", 3);
    tv_send(&s, draft);
    tv_send(&s, "\r");
    U("Policy JSON path:", 3);
    tv_send(&s, policy);
    tv_send(&s, "\r");
    U("Revision 1 / DRAFT", 3);
    tv_send(&s, "VB\t\t");
    U("READY / awaiting approval", 30);
    tv_send(&s, "z");
    U("READY / awaiting approval", 3);
    CHECK(tv_contains(&s, "Revision 1"), "first revision");
    tv_send(&s, "jjjjB");
    U("A CONVERSATION", 3);
    tv_send(&s, "B");
    U("B PLAN OVERVIEW", 3);
    CHECK(tv_contains(&s, "Revision 1"), "layout retains revision");
    changed = json_object_from_file(draft);
    CHECK(changed, "draft JSON");
    json_object_object_add(changed, "objective",
                           json_object_new_string("Revised workspace acceptance objective"));
    CHECK(!json_object_to_file(draft, changed), "write revised draft");
    json_object_put(changed);
    U("Revision 2 / DRAFT", 8);
    CHECK(!tv_contains(&s, "READY / awaiting approval"), "revision invalidates approval");
    tv_send(&s, "V");
    U("READY / awaiting approval", 30);
    tv_send(&s, "kkkk");
    U("Revised workspace acceptance objective", 3);
    for (i = 0; i < 3; i++) {
        if (sizes[i][0] == 40) {
            tv_send(&s, "j");
            tv_sleep(.1);
        }
        tv_resize(&s, sizes[i][0], sizes[i][1]);
        tv_pump(&s, .3);
        if (s.screen.overflow) {
            tv_format(path, sizeof(path), "%s/build/plan-resize-failure.ansi", f.root);
            tv_write(path, s.raw ? s.raw : "");
            fprintf(stderr, "%s\n", tv_text(&s));
        }
        CHECK(!s.screen.overflow, "plan resize overflow");
    }
    tv_write(draft, "{\"broken\":");
    U("Revision 3 / DRAFT", 8);
    tv_send(&s, "V");
    U("INVALID", 30);
    tv_send(&s, "CA");
    U("A CONVERSATION", 3);
    tv_close(&s, "q", 0, 0);
    tv_format(text, sizeof(text),
              "PASS plan workspace: compilation, revised scope, invalid JSON, reversible layouts "
              "and termios (%s)",
              f.base);
    puts(text);
    {
        const char *remove[] = {"rm", "-rf", f.base, NULL};
        tv_command_ok(NULL, remove);
    }
    return 0;
}
