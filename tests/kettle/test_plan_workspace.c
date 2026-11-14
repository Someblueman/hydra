#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
#define U(marker, timeout) kt_until(&s, (marker), (timeout))
/* The Plan page's state line: "READY · revision 1 · awaiting you". */
#define STATE(word, n) word " \xc2\xb7 revision " #n
/* z zooms the Plan page and restores the panes; ] and [ go round the side
 * pane's pages and back to the plan. */
static void zoom_and_pages(struct kt_session *s) {
    kt_send(s, "z");
    kt_pump(s, .3);
    CHECK(!kt_contains(s, "Heads \xe2\x94\x80") && kt_contains(s, STATE("READY", 1)), "zoomed plan page");
    kt_send(s, "z");
    kt_until(s, "Heads \xe2\x94\x80", 3);
    kt_send(s, "jjjj]");
    kt_until(s, "No head selected", 3);
    kt_send(s, "[");
    kt_until(s, STATE("READY", 1), 3);
    CHECK(kt_contains(s, STATE("READY", 1)), "pages retain revision");
}
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
    /* Work is up with nothing in it yet: an imported plan belongs to no head. */
    U("No agent work in this project yet", 5);
    kt_send(&s, ":import\r");
    U("Draft JSON path", 3);
    kt_send(&s, draft);
    kt_send(&s, "\r");
    U("Policy JSON path", 3);
    kt_send(&s, policy);
    kt_send(&s, "\r");
    /* The import opens the Plan page with focus, so V applies at once. A
     * draft not validated yet reads as a plan: what each step does, not the
     * JSON it was published as. */
    U(STATE("DRAFT", 1), 3);
    U("runs sh compose.sh in plan-smoke", 3);
    CHECK(kt_contains(&s, "starts a head on branch plan-smoke") && kt_contains(&s, "at most 3") &&
          !kt_contains(&s, "schema_version"), "a draft reads as its steps, not its JSON");
    kt_send(&s, "V");
    U(STATE("READY", 1) " \xc2\xb7 awaiting you", 30);
    zoom_and_pages(&s);
    changed = json_object_from_file(draft);
    CHECK(changed, "draft JSON");
    json_object_object_add(changed, "objective",
                           json_object_new_string("Revised workspace acceptance objective"));
    CHECK(!json_object_to_file(draft, changed), "write revised draft");
    json_object_put(changed);
    U(STATE("DRAFT", 2), 8);
    CHECK(!kt_contains(&s, STATE("READY", 1)) && !kt_contains(&s, STATE("READY", 2)), "revision invalidates approval");
    kt_send(&s, "V");
    U(STATE("READY", 2), 30);
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
    U(STATE("DRAFT", 3), 8);
    kt_send(&s, "V");
    U("INVALID", 30);
    /* Esc gives focus back to the list. */
    kt_send(&s, "\033");
    kt_pump(&s, .3);
    CHECK(kt_contains(&s, "Heads \xe2\x94\x80") && kt_contains(&s, "INVALID"), "the list beside the invalid plan");
    kt_close(&s, "q", 0, 0);
    kt_format(text, sizeof(text),
              "PASS plan workspace: compilation, revised scope, invalid JSON, reversible zoom and pages "
              "and termios (%s)",
              f.base);
    hf_finish(&f);
    puts(text);
    return 0;
}
