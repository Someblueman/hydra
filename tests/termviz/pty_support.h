#ifndef HYDRA_TEST_PTY_SUPPORT_H
#define HYDRA_TEST_PTY_SUPPORT_H
#define _XOPEN_SOURCE 700
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

/* Caller owns sessions and screen storage; tv_close/tv_abort release resources. */
struct tv_cell {
    char text[32];
    unsigned fg, bg;
    bool bold;
    int width;
};
struct tv_screen {
    int cols, rows, x, y, clears, overflow;
    bool awaiting_clear, bold, reverse;
    unsigned fg, bg, utf_value;
    int utf_remaining;
    char escape[260];
    size_t escape_size;
    struct tv_cell *cells;
    char *text;
};
struct tv_session {
    pid_t pid;
    int master, slave, status;
    bool closed, exited;
    struct termios original;
    struct tv_screen screen;
    char *raw;
    size_t raw_size, raw_capacity;
};
void tv_init(void);
/* Internal screen observer boundary shared by the native test harness. */
void tv_screen_reset(struct tv_screen *s, int cols, int rows, bool await);
void tv_screen_feed(struct tv_screen *s, const unsigned char *bytes, size_t n);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void tv_fail(const char *file, int line, const char *message);
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition))                                                                          \
            tv_fail(__FILE__, __LINE__, (message));                                                \
    } while (0)
void tv_open(struct tv_session *s, const char *const argv[], int cols, int rows, const char *cwd);
size_t tv_pump(struct tv_session *s, double seconds);
void tv_send(struct tv_session *s, const char *text);
void tv_send_n(struct tv_session *s, const void *bytes, size_t length);
void tv_repeat(struct tv_session *s, const char *text, int count);
const char *tv_text(struct tv_session *s);
const char *tv_row(struct tv_session *s, int row, char *buffer, size_t capacity);
bool tv_contains(struct tv_session *s, const char *marker);
void tv_until(struct tv_session *s, const char *marker, double timeout);
bool tv_match(struct tv_session *s, const char *pattern, char groups[][256], size_t count);
void tv_until_match(struct tv_session *s, const char *pattern, char groups[][256], size_t count,
                    double timeout);
void tv_resize(struct tv_session *s, int cols, int rows);
void tv_close(struct tv_session *s, const char *keys, int signum, int expected);
void tv_abort(struct tv_session *s);
void tv_save(struct tv_session *s, const char *path);
double tv_now(void);
void tv_sleep(double seconds);
/* Bounded formatting and file/process helpers. Output buffers remain caller-owned. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void tv_format(char *out, size_t capacity, const char *format, ...);
void tv_mkdir(const char *path);
void tv_write(const char *path, const char *text);
void tv_append(const char *path, const char *text);
void tv_read(const char *path, char *out, size_t capacity);
bool tv_exists(const char *path);
bool tv_file_equals(const char *path, const char *text);
void tv_temp(char *out, size_t capacity, const char *prefix);
void tv_copy(const char *source, const char *destination, bool recursive);
int tv_command(const char *cwd, const char *input, char *out, size_t capacity, double timeout,
               const char *const argv[]);
void tv_command_ok(const char *cwd, const char *const argv[]);
void tv_paths(char *root, size_t root_capacity, char *build, size_t build_capacity);
#endif
