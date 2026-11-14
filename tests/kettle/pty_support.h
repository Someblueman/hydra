#ifndef HYDRA_TEST_PTY_SUPPORT_H
#define HYDRA_TEST_PTY_SUPPORT_H
#define _XOPEN_SOURCE 700
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

/* Caller owns sessions and screen storage; kt_close/kt_abort release resources. */
struct kt_cell {
    char text[32];
    unsigned fg, bg;
    bool bold;
    int width;
};
struct kt_screen {
    int cols, rows, x, y, clears, overflow;
    /* Cells painted by the synchronized update (DEC mode 2026) being read. */
    int frame_cells, frame_overflow, repaints;
    bool awaiting_clear, in_frame, bold, reverse;
    unsigned fg, bg, utf_value;
    int utf_remaining;
    char escape[260];
    size_t escape_size;
    struct kt_cell *cells;
    char *text;
};
struct kt_session {
    pid_t pid;
    int master, slave, status;
    bool closed, exited;
    struct termios original;
    struct kt_screen screen;
    char *raw;
    size_t raw_size, raw_capacity;
};
void kt_init(void);
/* Internal screen observer boundary shared by the native test harness. */
void kt_screen_reset(struct kt_screen *s, int cols, int rows, bool await);
void kt_screen_feed(struct kt_screen *s, const unsigned char *bytes, size_t n);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void kt_fail(const char *file, int line, const char *message);
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition))                                                                          \
            kt_fail(__FILE__, __LINE__, (message));                                                \
    } while (0)
void kt_open(struct kt_session *s, const char *const argv[], int cols, int rows, const char *cwd);
/* Like kt_open, and the PTY is the child's controlling terminal, as under a
 * terminal emulator: kt_resize then raises SIGWINCH in the child. */
void kt_open_terminal(struct kt_session *s, const char *const argv[], int cols, int rows,
                      const char *cwd);
size_t kt_pump(struct kt_session *s, double seconds);
void kt_send(struct kt_session *s, const char *text);
void kt_send_n(struct kt_session *s, const void *bytes, size_t length);
void kt_repeat(struct kt_session *s, const char *text, int count);
const char *kt_text(struct kt_session *s);
const char *kt_row(struct kt_session *s, int row, char *buffer, size_t capacity);
bool kt_contains(struct kt_session *s, const char *marker);
void kt_until(struct kt_session *s, const char *marker, double timeout);
bool kt_match(struct kt_session *s, const char *pattern, char groups[][256], size_t count);
void kt_until_match(struct kt_session *s, const char *pattern, char groups[][256], size_t count,
                    double timeout);
void kt_resize(struct kt_session *s, int cols, int rows);
void kt_close(struct kt_session *s, const char *keys, int signum, int expected);
void kt_abort(struct kt_session *s);
void kt_save(struct kt_session *s, const char *path);
double kt_now(void);
void kt_sleep(double seconds);
/* Bounded formatting and file/process helpers. Output buffers remain caller-owned. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void kt_format(char *out, size_t capacity, const char *format, ...);
void kt_mkdir(const char *path);
void kt_write(const char *path, const char *text);
void kt_append(const char *path, const char *text);
void kt_read(const char *path, char *out, size_t capacity);
bool kt_exists(const char *path);
bool kt_file_equals(const char *path, const char *text);
void kt_temp(char *out, size_t capacity, const char *prefix);
void kt_copy(const char *source, const char *destination, bool recursive);
int kt_command(const char *cwd, const char *input, char *out, size_t capacity, double timeout,
               const char *const argv[]);
void kt_command_ok(const char *cwd, const char *const argv[]);
void kt_paths(char *root, size_t root_capacity, char *build, size_t build_capacity);
#endif
