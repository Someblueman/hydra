/* Tests for counter.c with the headless driver: no terminal, no real clock,
 * no real child process. The application is compiled into this file so its
 * static init/update/view are reachable. */
#define main counter_main
#include "counter.c"
#undef main
#include "testing.h"
#include <assert.h>

int main(void) {
    struct model model = {0};
    struct kt_test *test = kt_test_new(&counter, &model, 60, 8, NULL);
    const struct kt_test_process *date;
    assert(test);

    /* init asked for the date; nothing ran, the request is on record. */
    date = kt_test_process(test, 0);
    assert(date && !strcmp(date->spawn.argv[0], "date") && date->running);
    assert(kt_test_contains(test, "Date     asking..."));
    assert(kt_test_exit(test, date->id, 0, "Tue Nov 10 12:00:00 UTC 2026\n"));
    assert(strstr(kt_test_row(test, 4), "Date     Tue Nov 10 12:00:00 UTC 2026"));

    /* Keys go through the real input decoder: an arrow is its escape sequence. */
    kt_test_type(test, "++\033[A-");
    assert(model.count == 2);
    assert(kt_test_contains(test, "Count    2"));

    /* The clock moves only when the test moves it. */
    kt_test_advance(test, 3500);
    assert(model.seconds == 3);
    assert(kt_test_contains(test, "Running  3 s"));

    /* A child that fails, or runs out of its budget, is reported like any other. */
    kt_test_type(test, "d");
    kt_test_advance(test, 2000);
    assert(kt_test_process(test, 1)->signal != 0);
    assert(kt_test_contains(test, "Date     unavailable"));

    kt_test_type(test, "q");
    assert(!kt_test_running(test) && kt_test_status(test) == 0);
    kt_test_free(test);
    puts("counter: keys, timer, process result and quit passed under the headless driver");
    return 0;
}
