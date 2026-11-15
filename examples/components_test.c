/* Tests for components.c with the headless driver. The application is
 * compiled into this file so its static update/view are reachable. */
#define main components_main
#include "components.c"
#undef main
#include "testing.h"

static bool has(const char *text, const char *part) { return strstr(text, part) != NULL; }

static struct model model;
static struct kt_test *shown;   /* whose last frame a failure prints */

static void check_at(bool passed, const char *what, int line) {
    if (passed) return;
    fprintf(stderr, "FAIL examples/components_test.c:%d: %s\n", line, what);
    if (shown) fprintf(stderr, "%s\n", kt_test_screen(shown));
    exit(1);
}
#define CHECK(condition) check_at((condition), #condition, __LINE__)

static struct kt_test *start(int columns, int rows) {
    struct kt_options options = {0};
    struct kt_test *test;
    options.theme = &kt_theme_dark;
    options.escape_ms = 10;
    options.bracketed_paste = true;
    model_init(&model);
    test = kt_test_new(&components_app, &model, columns, rows, &options);
    CHECK(test != NULL);
    shown = test;
    return test;
}

static void escape(struct kt_test *test) {
    kt_test_type(test, "\033");
    kt_test_advance(test, 10);
}

/* The first screen: tabs, the list in sections with its bar, the details,
 * the status line and a footer generated from the bindings. */
static void first_screen(void) {
    struct kt_test *test = start(100, 30);
    const char *bytes;
    CHECK(has(kt_test_row(test, 0), "\xe2\x97\x86 kettle    Heads  Table  Log 3  Settings"));
    CHECK(has(kt_test_row(test, 2), " Heads \xe2\x94\x80") && has(kt_test_row(test, 2), "feature-live \xe2\x94\x80"));
    CHECK(has(kt_test_row(test, 3), "This project \xe2\x94\x80"));
    CHECK(has(kt_test_row(test, 4), " \xe2\x96\x8c \xe2\x97\x8f feature-live") &&
          has(kt_test_row(test, 3), "\xe2\x97\x8f running  codex"));
    CHECK(has(kt_test_row(test, 11), "terminal; this text wraps to the width of the pane."));
    CHECK(has(kt_test_row(test, 28), "\xe2\x97\x8f live   myproject \xc2\xb7 feature-live \xc2\xb7 dark theme"));
    CHECK(!strcmp(kt_test_row(test, 29), " \xe2\x86\x91/\xe2\x86\x93 select \xe2\x80\xa2 enter open \xe2\x80\xa2 "
                   "n new task \xe2\x80\xa2 x remove \xe2\x80\xa2 w run job \xe2\x80\xa2 : commands \xe2\x80\xa2 "
                   "q/ctrl+c quit \xe2\x80\xa2 ? more"));
    /* Idle: nothing to wake for. */
    CHECK(kt_test_sleep(test) == -1);
    /* The selection is the theme's, painted at 24 bits. */
    bytes = kt_test_output(test, NULL);
    CHECK(has(bytes, "48;2;43;53;80m"));
    kt_test_free(test);
}

static void browsing(void) {
    struct kt_test *test = start(100, 30);
    kt_test_type(test, "jj");
    CHECK(kt_list_index(&model.list) == 3 && has(kt_test_row(test, 2), "feature-unavailable \xe2\x94\x80"));
    /* A section header is skipped. */
    kt_test_type(test, "jjj");
    CHECK(kt_list_index(&model.list) == 7);
    /* Filter as you type; the footer follows what the keys now do. */
    kt_test_type(test, "/sec");
    CHECK(kt_list_rows(&model.list) >= 1 && kt_list_index(&model.list) == 8);
    CHECK(has(kt_test_row(test, 29), "enter keep filter") && has(kt_test_row(test, 29), "esc clear filter"));
    CHECK(!has(kt_test_row(test, 29), "new task") && has(kt_test_row(test, 29), "ctrl+c quit"));
    escape(test);
    CHECK(kt_list_rows(&model.list) == HEADS && kt_list_index(&model.list) == 8);
    /* Enter opens: a toast says so, and goes by itself. */
    kt_test_type(test, "\r");
    CHECK(has(kt_test_screen(test), "Opened run second-run"));
    kt_test_advance(test, 4000);
    CHECK(!has(kt_test_screen(test), "Opened") && kt_test_sleep(test) == -1);
    kt_test_free(test);
}

static void switching(void) {
    struct kt_test *test = start(100, 30);
    /* Tabs by key and by click. */
    kt_test_type(test, "\t");
    CHECK(model.tabs.active == PAGE_TABLE && has(kt_test_row(test, 3), "HEAD") && has(kt_test_row(test, 3), "STATUS"));
    CHECK(has(kt_test_row(test, 4), "\xe2\x96\x8c feature-live"));
    /* Sorting is asked of the application, which sorts. */
    kt_test_type(test, "s");
    CHECK(model.table.sort_column == 0 && has(kt_test_row(test, 3), "HEAD \xe2\x96\xb2"));
    CHECK(has(kt_test_row(test, 4), "build-cache"));
    kt_test_type(test, "S");
    CHECK(has(kt_test_row(test, 4), "run visualization-proof"));
    kt_test_type(test, "3");
    CHECK(model.tabs.active == PAGE_LOG && has(kt_test_row(test, 2), " Log \xe2\x94\x80"));
    CHECK(has(kt_test_row(test, 26), "12:00:23"));
    kt_test_type(test, "\033[<0;37;1M");
    CHECK(model.tabs.active == PAGE_SETTINGS && has(kt_test_screen(test), "Project name"));
    /* The inline form, rebound to move with up and down; typed characters
     * are its text, so the footer leaves out keys that are letters. */
    CHECK(has(kt_test_row(test, 29), "\xe2\x86\x93 next field") && !has(kt_test_row(test, 29), "q quit"));
    CHECK(has(kt_test_row(test, 29), "ctrl+c quit"));
    kt_test_type(test, "\033[B\033[C\r");
    CHECK(model.settings_fields[1].choice == 1 && has(kt_test_screen(test), "Saved settings for myproject"));
    CHECK(has(kt_test_row(test, 29), "q/ctrl+c quit"));
    /* Tab is the tabs', wherever the focus is; a digit picks a page. */
    kt_test_type(test, "\033[A\t");
    CHECK(model.tabs.active == PAGE_HEADS);
    kt_test_type(test, "41");
    CHECK(model.tabs.active == PAGE_SETTINGS && !strcmp(kt_textinput_value(&model.settings_name), "myproject1"));
    kt_test_free(test);
}

/* The job: spinner and progress while it runs, the log following its end,
 * and a quiet program once it is over. */
static void job(void) {
    struct kt_test *test = start(100, 30);
    kt_test_type(test, "3w");
    CHECK(model.job && model.spinner.timer && kt_test_sleep(test) == 80);
    kt_test_advance(test, 600);
    CHECK(has(kt_test_row(test, 28), "building") && has(kt_test_row(test, 28), "25%"));
    CHECK(model.log_count == 29 && has(kt_test_row(test, 27), "12:00:28") && has(kt_test_row(test, 3), "12:00:04"));
    kt_test_advance(test, 2000);
    CHECK(!model.job && !model.spinner.timer && has(kt_test_screen(test), "Job finished"));
    CHECK(!has(kt_test_row(test, 28), "building"));
    kt_test_advance(test, 4000);
    CHECK(kt_test_sleep(test) == -1);
    kt_test_free(test);
}

static void dialogs(void) {
    struct kt_test *test = start(100, 30);
    /* A new task: the form in a dialog over a faint backdrop. */
    kt_test_type(test, "n");
    CHECK(has(kt_test_screen(test), "\xe2\x95\xad\xe2\x94\x80 New task") && has(kt_test_screen(test), "Task name"));
    CHECK(has(kt_test_screen(test), "enter create \xe2\x80\xa2 esc cancel"));
    kt_test_type(test, "\033[200~fix-login\033[201~-redirect");
    CHECK(!strcmp(kt_textinput_value(&model.task_name), "fix-login-redirect"));
    kt_test_type(test, "\r");
    CHECK(model.modals.count == 0 && has(kt_test_screen(test), "Created fix-login-redirect"));
    /* Removing: a destructive confirmation, then the row says so. */
    kt_test_type(test, "x");
    CHECK(has(kt_test_screen(test), "Remove feature-live?") && has(kt_test_screen(test), "y remove \xe2\x80\xa2 esc cancel"));
    kt_test_type(test, "y");
    CHECK(model.removed[1] && has(kt_test_row(test, 4), "removed") && has(kt_test_screen(test), "Removed feature-live"));
    /* Help: every key in a dialog, closed by ? or Esc. */
    kt_test_type(test, "?");
    CHECK(has(kt_test_screen(test), "Keyboard") && has(kt_test_screen(test), "?/esc close"));
    CHECK(has(kt_test_screen(test), "Work") && has(kt_test_screen(test), "pgup/pgdn"));
    escape(test);
    CHECK(model.modals.count == 0 && !has(kt_test_screen(test), "Keyboard"));
    /* The palette: a command by name, run as if its key were pressed. */
    kt_test_type(test, ":them");
    CHECK(has(kt_test_screen(test), "Commands") && has(kt_test_screen(test), "theme"));
    kt_test_type(test, "\r");
    CHECK(model.modals.count == 0 && model.theme == 1 && has(kt_test_row(test, 28), "light theme"));
    CHECK(has(kt_test_output(test, NULL), "48;2;251;251;252m"));
    kt_test_type(test, "q");
    CHECK(!kt_test_running(test) && kt_test_status(test) == 0);
    kt_test_free(test);
}

/* Narrow: tabs give up their padding, the footer drops what does not fit. */
static void narrow(void) {
    struct kt_test *test = start(41, 20);
    CHECK(has(kt_test_row(test, 0), "Heads  Table Log 3 Settings"));
    CHECK(has(kt_test_row(test, 19), "? more") && kt_test_sleep(test) == -1);
    kt_test_free(test);
}

int main(void) {
    first_screen();
    browsing();
    switching();
    job();
    dialogs();
    narrow();
    puts("components: list, details, table, log, forms, dialogs, palette, help, job and toasts passed under the "
         "headless driver");
    return 0;
}
