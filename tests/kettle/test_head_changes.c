#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A head whose work is two committed files and a clean worktree: Work does not
 * summarise it as zero files, Details label the uncommitted changes as such,
 * and the full diff against the branch base (":diff") lists both files inside
 * the control centre. Tab and Shift-Tab walk the panes and return. Private
 * tmux socket. */
static struct hf_fixture f;
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define S(keys) kt_send(&s, (keys))
#define U(marker, seconds) kt_until(&s, (marker), (seconds))
/* The pane the footer is for: the list offers removal, the conversation offers
 * "enter talk", and the details page only scrolls. */
static const char *focus(struct kt_session *s) {
    char row[512];
    kt_pump(s, .4);
    kt_row(s, s->screen.rows - 1, row, sizeof(row));
    if (strstr(row, "x remove")) return "list";
    if (strstr(row, "enter talk")) return "conversation";
    if (strstr(row, "scroll")) return "details";
    return "";
}
/* Tab walks list, conversation and details and returns to the list; Shift-Tab
 * walks the same cycle backwards. */
static void cycle(struct kt_session *s) {
    static const char *const forward[] = {"conversation", "details", "list"};
    static const char *const backward[] = {"details", "conversation", "list"};
    int i;
    CHECK(!strcmp(focus(s), "list"), "Work starts with the list focused");
    for (i = 0; i < 3; i++) {
        kt_send(s, "\t");
        CHECK(!strcmp(focus(s), forward[i]), "Tab moves the focus to the next pane");
    }
    for (i = 0; i < 3; i++) {
        kt_send(s, "\033[Z");
        CHECK(!strcmp(focus(s), backward[i]), "Shift-Tab moves the focus to the previous pane");
    }
}
int main(void) {
    struct kt_session s;
    char path[4096], worktree[4096], text[512];
    const char *row;
    kt_init();
    hf_init(&f, "hydra-head-changes", "repo", false, true);
    hf_file(&f, "README", path, sizeof(path));
    kt_write(path, "changes fixture\n");
    hf_commit_init(&f);
    H("spawn", "worker", "--no-agent");
    kt_format(worktree, sizeof(worktree), "%s", H("path", "worker"));
    hf_trim(worktree);
    kt_format(path, sizeof(path), "%s/greeting.txt", worktree);
    kt_write(path, "hello\n");
    kt_format(path, sizeof(path), "%s/greet.sh", worktree);
    kt_write(path, "echo hello\n");
    RUN("git", "-C", worktree, "add", "greeting.txt", "greet.sh");
    RUN("git", "-C", worktree, "-c", "commit.gpgSign=false", "commit", "-qm", "work");
    hf_open(&f, &s);
    U("worker", 10);
    S("1");
    U("Changed", 5);
    row = kt_row(&s, 3, text, sizeof(text));
    CHECK(strstr(row, "worker") && !strstr(row, "0 files") && !strstr(row, "0 changed"),
          "Work does not summarise committed work as zero files");
    CHECK(kt_contains(&s, "not yet committed") && !kt_contains(&s, "0 changed files"),
          "Details label uncommitted changes separately from the task's diff against its base");
    cycle(&s);
    S(":");
    U("type to filter", 5);
    S("diff");
    U("Show diff of the selected head", 5);
    S("\r");
    U("+++ w/greeting.txt", 10);
    CHECK(kt_contains(&s, "+++ w/greet.sh") && kt_contains(&s, "+hello") && kt_contains(&s, "+echo hello"),
          "the full diff lists both committed files inside the control centre");
    CHECK(!kt_contains(&s, "Press Enter to return"), "the diff needs no return to the control centre");
    S("\033");
    U("x remove", 5);
    kt_close(&s, "q", 0, 0);
    hf_finish(&f);
    return 0;
}
