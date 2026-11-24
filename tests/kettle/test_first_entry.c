#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* First entry into a repository Hydra has never seen. Opening the control
 * centre, visiting its pages and the new-task form, and leaving again only
 * observe: no project is registered, no repository command is trusted, no
 * tmux session starts, and the checkout, its git status and .git/info/exclude
 * stay as they were. A committed workflow stays refused until
 * `hydra init --trust` says otherwise. Real bin/hydra and tmux on a private
 * socket; nothing runs `hydra init` before the control centre opens. */
static struct hf_fixture f;
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define REFUSED(...) hf_run(&f, NULL, -2, (const char *[]){__VA_ARGS__, NULL})
#define S(keys) kt_send(&s, (keys))
#define U(marker, seconds) kt_until(&s, (marker), (seconds))

static size_t registrations(void) {
    char pattern[4096];
    kt_format(pattern, sizeof(pattern), "%s/state/v2/projects/*", f.home);
    return hf_glob_count(pattern);
}
/* A committed workflow that nobody trusted is refused, naming trust. */
static void workflow_refused(const char *when) {
    char text[256];
    kt_format(text, sizeof(text), "%s: the committed workflow is refused as untrusted", when);
    REFUSED(f.hydra, "workflow", "validate", "local");
    CHECK(strstr(f.output, "not trusted"), text);
}
static void write_repository(void) {
    char path[4096], source[4096], text[4096];
    hf_file(&f, "README.txt", path, sizeof(path));
    kt_write(path, "first entry fixture\n");
    hf_file(&f, ".hydra", path, sizeof(path));
    kt_mkdir(path);
    hf_file(&f, ".hydra/workflows", path, sizeof(path));
    kt_mkdir(path);
    hf_file(&f, ".hydra/config.yml", path, sizeof(path));
    kt_write(path, "version: 1\n");
    kt_format(source, sizeof(source), "%s/examples/workflows/local-review.yml", f.root);
    kt_read(source, text, sizeof(text));
    hf_file(&f, ".hydra/workflows/local.yml", path, sizeof(path));
    kt_write(path, text);
}

int main(void) {
    struct kt_session s;
    char git_hydra[4096], exclude_path[4096], exclude_before[8192], exclude_after[8192];
    char status_before[4096], tree_before[8192];
    kt_init();
    hf_init(&f, "hydra-first-entry", "repo", false, true);
    write_repository();
    hf_git_init(&f);
    hf_file(&f, ".git/info/exclude", exclude_path, sizeof(exclude_path));
    kt_read(exclude_path, exclude_before, sizeof(exclude_before));
    kt_format(status_before, sizeof(status_before), "%s", RUN("git", "status", "--porcelain"));
    kt_format(tree_before, sizeof(tree_before), "%s", RUN("find", ".", "-path", "./.git", "-prune", "-o", "-type", "f", "-print"));
    CHECK(!*status_before, "the fixture repository starts clean and committed");
    CHECK(registrations() == 0, "the repository starts unregistered");
    workflow_refused("before entry");

    hf_open(&f, &s);
    /* Work is up with nothing in it. */
    U("No agent work in this project yet", 10);
    S("n");
    U("New task", 5);
    S("\033");
    U("No agent work in this project yet", 5);
    S("3");
    U("This machine", 10);
    S("4");
    U("Sessions", 10);
    S("1");
    U("No agent work in this project yet", 5);
    kt_close(&s, "q", 0, 0);

    CHECK(registrations() == 0, "entering and leaving registers no project and records no trust");
    hf_file(&f, ".git/hydra", git_hydra, sizeof(git_hydra));
    CHECK(!kt_exists(git_hydra), "entering leaves nothing in the repository's git directory");
    REFUSED("tmux", "list-sessions");
    CHECK(!strstr(RUN(f.hydra, "list", "--json"), "\"branch\""), "entering creates no head");
    CHECK(!strcmp(RUN("git", "status", "--porcelain"), status_before), "entering leaves git status unchanged");
    CHECK(!strcmp(RUN("find", ".", "-path", "./.git", "-prune", "-o", "-type", "f", "-print"), tree_before),
          "entering leaves the working tree unchanged");
    kt_read(exclude_path, exclude_after, sizeof(exclude_after));
    CHECK(!strcmp(exclude_before, exclude_after), "entering leaves .git/info/exclude unchanged");
    workflow_refused("after entry");

    /* Trust stays explicit: only `init --trust` lets the workflow validate. */
    RUN(f.hydra, "init", "--no-agent", "--trust");
    RUN(f.hydra, "workflow", "validate", "local");
    /* Reopening after configuration still changes nothing in the checkout. */
    hf_open(&f, &s);
    U("No agent work in this project yet", 10);
    kt_close(&s, "q", 0, 0);
    kt_read(exclude_path, exclude_after, sizeof(exclude_after));
    CHECK(!strcmp(RUN("git", "status", "--porcelain"), status_before) && !strcmp(exclude_before, exclude_after),
          "reopening after init leaves git status and .git/info/exclude unchanged");
    hf_finish(&f);
    return 0;
}
