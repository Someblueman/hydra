#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

/* What a child of the control centre starts with. The loop ignores SIGPIPE
 * and handles the ending, stop and window signals; a handler ends at exec,
 * but an ignored signal and the signal mask are inherited, and a POSIX shell
 * cannot stop ignoring a signal it was started ignoring. A `hydra` command
 * started from here would then see EPIPE in `yes | head -1` instead of ending
 * quietly. Every child therefore gets the default disposition for each signal
 * the runtime changes, and an empty mask.
 *
 * The stop key's signal is the exception: it stays ignored when the control
 * centre itself was started that way (no job control), which is the only time
 * it is ignored here. A child stopped alone would never be continued. */
static void child_defaults(sigset_t *defaults) {
    struct sigaction stop;
    size_t index;
    int number;
    (void)sigemptyset(defaults);
    for (index = 0; (number = kt_child_signal(index)) != 0; index++) (void)sigaddset(defaults, number);
    if (sigaction(SIGTSTP, NULL, &stop) == 0 && stop.sa_handler == SIG_IGN) (void)sigdelset(defaults, SIGTSTP);
}

/* In a forked child, before exec. */
void child_signals(void) {
    struct sigaction action;
    sigset_t defaults, none;
    size_t index;
    int number;
    child_defaults(&defaults);
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    for (index = 0; (number = kt_child_signal(index)) != 0; index++)
        if (sigismember(&defaults, number) == 1) (void)sigaction(number, &action, NULL);
    (void)sigemptyset(&none);
    (void)sigprocmask(SIG_SETMASK, &none, NULL);
}

/* The same for posix_spawn: initialises the attributes, with the caller's
 * flags besides. The caller destroys them. */
void child_spawn_attributes(posix_spawnattr_t *attributes, short flags) {
    sigset_t defaults, none;
    child_defaults(&defaults);
    (void)sigemptyset(&none);
    (void)posix_spawnattr_init(attributes);
    (void)posix_spawnattr_setsigdefault(attributes, &defaults);
    (void)posix_spawnattr_setsigmask(attributes, &none);
    (void)posix_spawnattr_setflags(attributes, (short)(flags | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK));
}
