/* U14 through the real product path: recorded Claude Code and Codex output
 * (tests/fixtures/providers) is printed in a Hydra head's tmux session, then
 * read back by the native TUI in the Details transcript and in the attached
 * pane at 140x40 and 80x24, including resize and scrollback. An independent
 * cell observer checks glyphs, wrapping markers, colors and that no raw
 * control sequence or replacement run reaches the screen. */
#define _XOPEN_SOURCE 700
#include "hydra_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hf_fixture f;
static struct tv_session s;
static char target[300], evidence[4096];
#define RUN(...) hf_run(&f, NULL, 0, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(f.hydra, __VA_ARGS__)
#define UNICODE_LINE "Unicode check: caf\xc3\xa9, na\xc3\xafve, \xe7\x95\x8c\xe9\x9d\xa2 (CJK), emoji " \
    "\xf0\x9f\x9a\x80\xe2\x9c\x85, arrows \xe2\x86\x92 \xe2\x86\x90, box \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90" \
    "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x98, combining \xc3\xa9."
#define WARNING_FG 0xe5e510U
#define SUCCESS_FG 0x0dbc79U
#define RED_FG 0xcd3131U

static void save(const char *name) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s-%dx%d.html", evidence, name, s.screen.cols, s.screen.rows);
    tv_save(&s, path);
}

static void fail_with(const char *message) {
    fprintf(stderr, "%s\n%s\n", message, tv_text(&s));
    CHECK(false, message);
}

/* Prints a fixture in the head's own shell, as the provider once did. */
static void show_in_head(const char *fixture) {
    char command[8192];
    tv_format(command, sizeof(command), "clear; /bin/cat '%s/tests/fixtures/providers/%s'", f.root, fixture);
    RUN("tmux", "send-keys", "-t", target, "-l", command);
    RUN("tmux", "send-keys", "-t", target, "Enter");
}

/* Observer column of a byte offset in row y (one cell per column). */
static int column_of(int y, size_t prefix) {
    int x = 0;
    size_t bytes = 0;
    while (bytes < prefix && x < s.screen.cols) {
        const struct tv_cell *cell = &s.screen.cells[y * s.screen.cols + x];
        if (cell->width) bytes += strlen(cell->text);
        x++;
    }
    while (x < s.screen.cols && !s.screen.cells[y * s.screen.cols + x].width) x++;
    return x;
}

static int find_row(const char *needle, int *column) {
    char row[8192];
    int y;
    for (y = 0; y < s.screen.rows; y++) {
        char *hit;
        tv_row(&s, y, row, sizeof(row));
        if (!(hit = strstr(row, needle))) continue;
        if (column) *column = column_of(y, (size_t)(hit - row));
        return y;
    }
    return -1;
}

static unsigned fg_of(const char *needle) {
    int x = 0, y = find_row(needle, &x);
    if (y < 0) fail_with(needle);
    return s.screen.cells[y * s.screen.cols + x].fg;
}

/* No escape residue, no C0 bytes and no '?' runs on any row. */
static void clean_screen(const char *when) {
    const char *text = tv_text(&s);
    if (strstr(text, "[38;5") || strstr(text, "[1m") || strstr(text, "[0m") || strstr(text, "\033") ||
        strstr(text, "??") || strstr(text, "8;id="))
        fail_with(when);
    CHECK(!s.screen.overflow, "no overflow");
}

/* Wide cells: the observer marks the second column of 界 as a continuation. */
static void wide_intact(void) {
    int x = 0, y = find_row("\xe7\x95\x8c\xe9\x9d\xa2", &x);
    if (y < 0) fail_with("CJK text missing");
    CHECK(s.screen.cells[y * s.screen.cols + x].width == 2, "wide CJK cell");
    CHECK(!strcmp(s.screen.cells[y * s.screen.cols + x + 2].text, "\xe9\x9d\xa2"), "next wide cell aligned");
}

/* Rows only a 140x40 Details view has room for. */
static void codex_full(void) {
    wide_intact();
    CHECK(fg_of("-w\xc3\xb6rld") == WARNING_FG, "removed diff line toned");
    CHECK(fg_of("+world") == SUCCESS_FG, "added diff line toned");
    if (find_row(UNICODE_LINE, NULL) < 0) fail_with("Unicode line not verbatim");
}

/* The Details transcript of Codex's plain diff: themed diff, wrapped long
 * line. 80x24 leaves room for the newest rows only. */
static void details_codex(bool full) {
    tv_until(&s, "TERMINAL OUTPUT", 5);
    tv_until(&s, "LONGLINE:", 8);
    tv_until(&s, "Read-only transcript", 3);
    clean_screen("codex transcript");
    CHECK(tv_contains(&s, "\xe2\x86\xaa"), "long line continues with a marker");
    CHECK(tv_contains(&s, "-END"), "end of the long line is not clipped");
    if (full) codex_full();
    save("details-codex");
}

/* Claude's own colors survive capture; its key hints are labelled live-only. */
static void details_claude(bool full) {
    tv_until(&s, "for agents", 8);
    clean_screen("claude transcript");
    tv_until(&s, "Key hints above belong to the agent", 3);
    if (full) {
        wide_intact();
        CHECK(fg_of("-w\xc3\xb6rld") == RED_FG, "provider red kept");
        CHECK(fg_of("+world") == SUCCESS_FG, "provider green kept");
    }
    save("details-claude");
}

static void resize(int cols, int rows) {
    tv_resize(&s, cols, rows);
    tv_pump(&s, 2.5); /* the preview refreshes every two seconds */
}

/* The live attached pane shows the same bytes through tmux and termviz. */
static void attached(void) {
    int i;
    bool wide = false;
    tv_send(&s, "\033");
    tv_pump(&s, .3);
    tv_send(&s, "a");
    tv_until(&s, "INPUT TO AGENT", 5);
    tv_send(&s, "/bin/cat '");
    tv_send(&s, f.root);
    tv_send(&s, "/tests/fixtures/providers/codex-exec.raw'; /bin/cat '");
    tv_send(&s, f.root);
    tv_send(&s, "/tests/fixtures/providers/claude-capture.ansi'\r");
    tv_until(&s, "for agents", 8);
    tv_pump(&s, .5);
    clean_screen("attached pane");
    CHECK(fg_of("-w\xc3\xb6rld") == RED_FG, "attached pane keeps provider red");
    save("attached");
    /* Ctrl-B [ opens the agent's own tmux history: output that scrolled past
     * is reachable with PgUp, wide characters intact. */
    tv_send(&s, "\002[");
    tv_until(&s, "Agent history", 3);
    for (i = 0; i < 40 && find_row("OpenAI Codex", NULL) < 0; i++) {
        tv_send(&s, "\033[5~");
        tv_pump(&s, .2);
        if (find_row("\xe7\x95\x8c\xe9\x9d\xa2", NULL) >= 0) { wide_intact(); wide = true; }
    }
    if (find_row("OpenAI Codex", NULL) < 0 || !wide) fail_with("scrollback lost streamed output");
    clean_screen("scrollback");
    save("attached-scrollback");
    tv_send(&s, "\002]");
    tv_until(&s, "Back to live agent output", 3);
    tv_until(&s, "for agents", 3);
}

static void fixture(void) {
    char path[4096], rows[65536], session[256];
    const char *p;
    tv_init();
    hf_init(&f, "hydra-output-fidelity", "repo", false, true);
    unsetenv("NO_COLOR");
    hf_file(&f, "input", path, sizeof(path));
    tv_write(path, "fidelity\n");
    hf_commit_init(&f);
    H("spawn", "one", "--no-agent");
    tv_format(rows, sizeof(rows), "%s", H("tui", "--data"));
    for (p = rows; p && strncmp(p, "H\t", 2); p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {}
    CHECK(p != NULL, "head row");
    p = strchr(p + 2, '\t') + 1;
    tv_format(session, sizeof(session), "%.*s", (int)(strchr(p, '\t') - p), p);
    tv_format(target, sizeof(target), "=%s:", session);
    tv_format(evidence, sizeof(evidence), "%s/output-fidelity-evidence", f.build);
    tv_mkdir(evidence);
}

int main(void) {
    fixture();
    show_in_head("codex-exec.raw");
    {
        const char *argv[] = {f.tui, "--hydra", f.hydra, NULL};
        tv_open(&s, argv, 140, 40, f.repo);
    }
    tv_until(&s, "Session   running", 8);
    tv_send(&s, "p");
    details_codex(true);
    resize(80, 24);
    details_codex(false);
    resize(140, 40);
    details_codex(true);
    show_in_head("claude-capture.ansi");
    tv_pump(&s, 2.5);
    details_claude(true);
    resize(80, 24);
    details_claude(false);
    /* PgUp scrolls the read-only transcript back; PgDn returns to the newest. */
    tv_send(&s, "\033[5~");
    tv_until(&s, "rows back", 3);
    clean_screen("scrolled transcript");
    save("details-scrolled");
    tv_send(&s, "\033[6~\033[6~\033[6~");
    tv_until(&s, "PgUp scrolls", 3);
    tv_until(&s, "for agents", 3);
    resize(140, 40);
    attached();
    resize(80, 24);
    tv_until(&s, "INPUT TO AGENT", 3);
    tv_until(&s, "for agents", 5);
    clean_screen("attached after resize");
    save("attached");
    {
        /* The whole PTY byte stream, replayable with cat, for human review. */
        char path[4096];
        FILE *out;
        tv_format(path, sizeof(path), "%s/session.raw", evidence);
        out = fopen(path, "wb");
        CHECK(out && fwrite(s.raw, 1, s.raw_size, out) == s.raw_size && !fclose(out), "raw PTY log");
    }
    tv_close(&s, "\002q", 0, 0);
    hf_finish(&f);
    printf("PASS output fidelity: real Claude/Codex output in Details and the attached pane at 140x40 and 80x24, "
           "resize, scrollback; evidence in %s\n", evidence);
    return 0;
}
