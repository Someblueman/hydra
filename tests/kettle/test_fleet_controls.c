#define _XOPEN_SOURCE 700
#include "pty_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static char root[4096], build[4096], base[4096], tui[4096], logpath[4096], mode[4096], count[4096];
static const char *digest = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
static const char wrapper_script[] =
    "#!/bin/sh\n"
    "printf 'ARGS:%s\\n' \"$*\" >> \"$FC_BASE/dispatch.log\"\n"
    "if [ \"$1\" = fleet ] && [ \"$2\" = tui-visual-data ]; then\n"
    "  state=$(cat \"$FC_BASE/mode\")\n"
    "  cancel=-; scope=-; requested=-; taskstate=waiting_approval; freshness=fresh; request=req-2\n"
    "  case \"$state\" in\n"
    "    requested) cancel=requested; scope=managed_commands; requested=2026-09-10T00:00:01Z ;;\n"
    "    delivered) cancel=delivered; scope=managed_commands; requested=2026-09-10T00:00:02Z ;;\n"
    "    confirmed) cancel=confirmed_stopped; scope=managed_commands; "
    "requested=2026-09-10T00:00:03Z ;;\n"
    "    stale) freshness=stale ;;\n"
    "    changed) request=req-3 ;;\n"
    "    unknown) cancel=unknown; scope=managed_commands; requested=2026-09-10T00:00:04Z; "
    "taskstate=outcome_unknown ;;\n"
    "  esac\n"
    "  printf 'HYDRA_FLEET_TUI\\t3\\n'\n"
    "  printf 'F\\tbuild\\t/project\\tbranch\\thead\\tinstance\\tLIVE\\n'\n"
    "  printf 'T\\tbuild\\tresponded\\t0\\t-\\treachable\\tfresh\\t1\\t1\\n'\n"
    "  printf "
    "'O\\tbuild\\ttask_1\\t-\\t-\\t-\\t-\\tnone\\trecorded\\trunning\\tnone\\t-"
    "\\tinspect\\t1\\t1\\t%s\\t0\\tunavailable\\tunavailable\\t%s\\t-\\t-\\t-\\t-\\n' "
    "\"$freshness\" \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"\n"
    "  printf "
    "'O\\tbuild\\ttask_2\\t-\\t-\\t-\\t-\\tnone\\twaiting\\t%"
    "s\\tapproval\\tReview\\tdecide\\t1\\t1\\t%s\\t1\\tunavailable\\tunavailable\\t%s\\t%s\\t%s\\t%"
    "s\\t%s\\n' \"$taskstate\" \"$freshness\" "
    "\"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789\" \"$cancel\" \"$scope\" "
    "\"$requested\" \"$request\"\n"
    "  exit 0\n"
    "fi\n"
    "if [ \"$1\" = fleet ] && [ \"$2\" = task ]; then\n"
    "  if [ \"$3\" = cancel ]; then\n"
    "    n=$(cat \"$FC_BASE/count\"); n=$((n + 1)); printf '%s\\n' \"$n\" > \"$FC_BASE/count\"\n"
    "    printf requested > \"$FC_BASE/mode\"; exit 9\n"
    "  fi\n"
    "fi\n"
    "printf '%s\\n' \"$@\" >> \"$FC_BASE/dispatch.log\"\n"
    "exit 0\n";
static void launch(struct kt_session *s) {
    const char *argv[] = {tui, "--fleet", "--view", "overview", NULL};
    kt_open_terminal(s, argv, 120, 40, base);
    kt_until(s, "task_2", 8);
    /* The one Work list: the remote head, then task_1, then task_2. */
    kt_send(s, "jj");
    kt_until(s, "req-2", 3);
}
/* The control's confirmation names the task and the control ("Approve
 * remote task"; Work's conversation says "remote task" of the row itself);
 * y accepts it. */
static void confirm(struct kt_session *s, const char *key, const char *word) {
    char title[64];
    kt_format(title, sizeof(title), "%s remote task", word[0] == 'a' ? "Approve" : word[0] == 'r' && word[1] == 'e' && word[2] == 'j' ?
              "Reject" : word[0] == 'r' ? "Resume" : "Cancel");
    kt_send(s, key);
    kt_until(s, title, 3);
    CHECK(kt_contains(s, title), "the confirmation names the control");
    kt_send(s, "y");
    kt_pump(s, .5);
}
static void mutations(char *out, size_t capacity) {
    char data[65536];
    char *p;
    size_t used = 0;
    kt_read(logpath, data, sizeof(data));
    out[0] = 0;
    for (p = data; *p;) {
        char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!strncmp(p, "ARGS:fleet task ", 16)) {
            CHECK(used + n + 2 < capacity, "mutation log capacity");
            memcpy(out + used, p, n);
            used += n;
            out[used++] = '\n';
            out[used] = 0;
        }
        if (!end)
            break;
        p = end + 1;
    }
}
static void unchanged(const char *before) {
    char after[32768];
    mutations(after, sizeof(after));
    CHECK(!strcmp(before, after), "stale confirmation mutated receiver");
}
static void preserved(void) {
    char path[4096];
    kt_format(path, sizeof(path), "%s/active.lock", base);
    CHECK(kt_file_equals(path, "active"), "lock preserved");
    kt_format(path, sizeof(path), "%s/dirty.txt", base);
    CHECK(kt_file_equals(path, "dirty\n"), "dirty bytes preserved");
}
int main(void) {
    struct kt_session s;
    char wrapper[4096], home[4096], path[4096], before[32768], lines[65536], expected[1024];
    const char *stages[] = {"requested", "delivered", "confirmed"};
    const char *markers[] = {"requested", "delivered", "confirmed_stopped"};
    size_t i;
    kt_init();
    kt_paths(root, sizeof(root), build, sizeof(build));
    kt_temp(base, sizeof(base), "hydra-fleet-controls");
    kt_format(tui, sizeof(tui), "%s/hydra-tui", build);
    kt_format(logpath, sizeof(logpath), "%s/dispatch.log", base);
    kt_format(mode, sizeof(mode), "%s/mode", base);
    kt_format(count, sizeof(count), "%s/count", base);
    kt_write(mode, "fresh");
    kt_write(count, "0");
    kt_format(path, sizeof(path), "%s/active.lock", base);
    kt_write(path, "active");
    kt_format(path, sizeof(path), "%s/dirty.txt", base);
    kt_write(path, "dirty\n");
    kt_format(wrapper, sizeof(wrapper), "%s/hydra-wrapper", base);
    kt_write(wrapper, wrapper_script);
    CHECK(!chmod(wrapper, 0755), "wrapper executable");
    kt_format(home, sizeof(home), "%s/home", base);
    setenv("FC_BASE", base, 1);
    setenv("HYDRA_BIN_CMD", wrapper, 1);
    setenv("HYDRA_HOME", home, 1);
    launch(&s);
    confirm(&s, "Y", "approve");
    confirm(&s, "N", "reject");
    confirm(&s, "R", "resume");
    kt_read(logpath, lines, sizeof(lines));
    kt_format(expected, sizeof(expected),
              "task decide build --id task_2 --request req-2 --decision approve --trust-spec %s",
              digest);
    CHECK(strstr(lines, expected), "approve exact task/request/spec");
    kt_format(expected, sizeof(expected),
              "task decide build --id task_2 --request req-2 --decision reject --trust-spec %s",
              digest);
    CHECK(strstr(lines, expected), "reject exact task/request/spec");
    kt_format(expected, sizeof(expected), "task resume build --id task_2 --trust-spec %s", digest);
    CHECK(strstr(lines, expected), "resume exact task/spec");
    mutations(before, sizeof(before));
    kt_send(&s, "Y");
    kt_until(&s, "Approve remote task", 3);
    kt_write(mode, "stale");
    kt_sleep(2.2);
    kt_pump(&s, .4);
    kt_send(&s, "y");
    kt_pump(&s, .5);
    kt_until(&s, "Task evidence changed during confirmation", 3);
    unchanged(before);
    kt_close(&s, "q", 0, 0);
    kt_write(mode, "fresh");
    launch(&s);
    mutations(before, sizeof(before));
    kt_send(&s, "Y");
    kt_until(&s, "Approve remote task", 3);
    kt_write(mode, "changed");
    kt_sleep(2.2);
    kt_pump(&s, .4);
    kt_send(&s, "y");
    kt_pump(&s, .5);
    kt_until(&s, "Task evidence changed during confirmation", 3);
    unchanged(before);
    kt_close(&s, "q", 0, 0);
    kt_write(mode, "fresh");
    launch(&s);
    confirm(&s, "X", "cancel");
    kt_pump(&s, 1);
    kt_read(logpath, lines, sizeof(lines));
    CHECK(strstr(lines, "task_2") && strstr(lines, "cancel build --id task_2"),
          "cancel bound task");
    CHECK(kt_file_equals(count, "1\n"), "one cancellation despite lost reply");
    preserved();
    kt_close(&s, "q", 0, 0);
    for (i = 0; i < 3; i++) {
        kt_write(mode, stages[i]);
        launch(&s);
        CHECK(kt_contains(&s, markers[i]), "receiver cancellation stage visible");
        CHECK(kt_file_equals(count, "1\n"), "restart does not replay");
        kt_close(&s, "q", 0, 0);
    }
    kt_write(mode, "unknown");
    launch(&s);
    kt_send(&s, "X");
    kt_until(&s, "stale or uncertain", 3);
    CHECK(kt_file_equals(count, "1\n"), "uncertain task cannot cancel");
    kt_close(&s, "q", 0, 0);
    preserved();
    {
        const char *remove[] = {"rm", "-rf", base, NULL};
        kt_command_ok(NULL, remove);
    }
    puts("PASS fleet controls: exact Y/N/R, stale and changed modal, lost cancel, receiver stages, "
         "restart, preservation");
    return 0;
}
