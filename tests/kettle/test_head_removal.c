#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Removing heads from Work, through the real CLI: a cancelled removal changes
 * nothing; removing an active head stops its terminal's process; with a clean
 * and a dirty head both marked, one bulk removal removes the clean head and
 * refuses the dirty one, which keeps its worktree and its draft, and the
 * whole exchange stays inside the control centre (no shell detour, no
 * "Press Enter to return"). Private tmux socket; the fixture guardian removes
 * everything. */
static struct hf_fixture f;
static const char *const WORK = "1 Work";
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define REFUSED(...) hf_run(&f, NULL, -2, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) kt_send(&s, (keys))
#define U(marker, seconds) kt_until(&s, (marker), (seconds))

static void worktree_of(const char *head, char *out, size_t size) {
    kt_format(out, size, "%s", H("path", head));
    hf_trim(out);
}
static bool listed(const char *head) {
    char needle[256];
    kt_format(needle, sizeof(needle), "%s ", head);
    H("list");
    return strstr(f.output, needle) != NULL;
}
/* The Work list's header names the selected head: "Heads --- 4  name ---". */
static bool selected(struct kt_session *s, const char *head) {
    char row[512], needle[256];
    kt_format(needle, sizeof(needle), "  %s ", head);
    return strstr(kt_row(s, 2, row, sizeof(row)), needle) != NULL;
}
static void select_head(struct kt_session *s, const char *head) {
    int tries;
    kt_repeat(s, "k", 8);
    kt_pump(s, .3);
    for (tries = 0; tries < 8 && !selected(s, head); tries++) {
        kt_send(s, "j");
        kt_pump(s, .2);
    }
    CHECK(selected(s, head), "the Work list selects the head");
}
/* Pumps for a while, checking every snapshot: the control centre's tab bar
 * stays up and nothing asks the user to press a key to return to it. */
static void in_app_for(struct kt_session *s, double seconds) {
    double end = kt_now() + seconds;
    while (kt_now() < end) {
        kt_pump(s, .05);
        CHECK(kt_contains(s, WORK), "the control centre stays up during the removal");
        CHECK(!kt_contains(s, "Press Enter to return"), "the removal never asks for a key to return");
    }
}

int main(void) {
    struct kt_session s;
    char path[4096], dirty[4096], clean[4096], survivor[4096], active[4096], draft[4096], text[4096];
    kt_init();
    hf_init(&f, "hydra-head-removal", "repo", false, true);
    hf_file(&f, "README", path, sizeof(path));
    kt_write(path, "removal fixture\n");
    hf_commit_init(&f);
    H("spawn", "active", "--no-agent");
    H("spawn", "survivor", "--no-agent");
    H("spawn", "dirty", "--no-agent");
    H("spawn", "clean", "--no-agent");
    worktree_of("active", active, sizeof(active));
    worktree_of("survivor", survivor, sizeof(survivor));
    worktree_of("dirty", dirty, sizeof(dirty));
    worktree_of("clean", clean, sizeof(clean));
    kt_format(draft, sizeof(draft), "%s/draft.txt", dirty);
    kt_write(draft, "uncommitted draft\n");
    /* A process runs in the active head's terminal. */
    RUN("tmux", "send-keys", "-t", "active", "sleep 7777", "Enter");
    {
        double deadline = kt_now() + 10;
        while (hf_run(&f, NULL, -1, (const char *[]){"pgrep", "-f", "sleep 7777", NULL}), !f.output[0] && kt_now() < deadline)
            kt_sleep(.2);
        CHECK(f.output[0], "the active head's terminal is running a process");
    }

    hf_open(&f, &s);
    U("survivor", 10);
    S("1");
    U("dirty", 5);

    /* Cancelling keeps the head, its worktree and the selection. */
    select_head(&s, "survivor");
    S("x");
    U("Remove survivor?", 5);
    CHECK(kt_contains(&s, "The branch survivor is kept") || kt_contains(&s, "survivor is kept"),
          "the confirmation says the branch is kept");
    CHECK(kt_contains(&s, "refused, never forced"), "the confirmation says dirty work is refused");
    S("n");
    U("Removal cancelled; nothing changed", 5);
    CHECK(selected(&s, "survivor"), "cancelling keeps the selection");
    CHECK(listed("survivor") && !access(survivor, F_OK), "cancelling keeps the head and its worktree");

    /* An active head: its terminal's process is stopped, its worktree removed. */
    select_head(&s, "active");
    S("x");
    U("Remove active?", 5);
    S("y");
    in_app_for(&s, 3);
    U("Removed active", 15);
    CHECK(access(active, F_OK) && !listed("active"), "the active head and its worktree are gone");
    REFUSED("pgrep", "-f", "sleep 7777");
    RUN("git", "rev-parse", "--verify", "active");

    /* One bulk removal with a clean and a dirty head marked. */
    select_head(&s, "dirty");
    S(" ");
    select_head(&s, "clean");
    S(" ");
    U("2 marked", 5);
    S("x");
    U("Remove 2 heads", 5);
    CHECK(kt_contains(&s, "clean") && kt_contains(&s, "dirty"), "the bulk confirmation names both heads");
    S("y");
    U("REMOVAL OUTPUT", 20);
    in_app_for(&s, 1);
    CHECK(kt_contains(&s, "== dirty (exit 1)") && kt_contains(&s, "untracked files"),
          "the result reports the dirty head refused for its untracked files");
    CHECK(kt_contains(&s, "== clean (exit 0)") && kt_contains(&s, "Removed clean."),
          "the result reports the clean head removed");
    CHECK(access(clean, F_OK) && !listed("clean"), "the clean head was removed");
    CHECK(listed("dirty") && !access(dirty, F_OK), "the dirty head and its worktree remain");
    kt_read(draft, text, sizeof(text));
    CHECK(!strcmp(text, "uncommitted draft\n"), "the dirty head keeps its draft");
    RUN("git", "rev-parse", "--verify", "clean");
    /* The messages close with Esc and surviving work is selected. */
    S("\033");
    U("x remove", 5);
    CHECK(!kt_contains(&s, "REMOVAL OUTPUT"), "the removal output closes");
    CHECK(selected(&s, "dirty") || selected(&s, "survivor"), "surviving work stays selected");
    kt_close(&s, "q", 0, 0);
    hf_finish(&f);
    return 0;
}
