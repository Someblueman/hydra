#include "task_name.h"
#include <string.h>

static bool ascii_alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool branch_bytes(const char *name) {
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (!ascii_alnum(*p) && *p != '.' && *p != '_' && *p != '/' && *p != '-') return false;
    return true;
}

bool task_branch_valid(const char *name) {
    size_t n = name ? strlen(name) : 0U;
    if (!n || n > 255U || !branch_bytes(name)) return false;
    if (name[0] == '-' || name[0] == '/' || name[0] == '.' || name[n - 1U] == '/' || name[n - 1U] == '.') return false;
    if (strstr(name, "//") || strstr(name, "..") || strstr(name, "/.")) return false;
    return n < 5U || strcmp(name + n - 5U, ".lock") != 0;
}

/* A '/' in a separator run keeps the branch component; any other run is '-'. */
static char slug_separator(unsigned char c, char pending) {
    if (c == '/') return '/';
    return pending ? pending : '-';
}

static char ascii_lower(unsigned char c) {
    return (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
}

/* Separators are emitted only between retained characters, so the result
 * never starts or ends with one and never repeats one. */
static size_t branch_slug(const char *name, char *out, size_t limit) {
    size_t used = 0U;
    char separator = '\0';
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (!ascii_alnum(*p)) {
            separator = slug_separator(*p, separator);
            continue;
        }
        size_t need = 1U + (size_t)(separator && used);
        if (used + need > limit) break;
        if (need == 2U) out[used++] = separator;
        separator = '\0';
        out[used++] = ascii_lower(*p);
    }
    out[used] = '\0';
    return used;
}

bool task_branch_name(const char *name, char *out, size_t size) {
    size_t n;
    if (!out || !size) return false;
    out[0] = '\0';
    if (!name) return false;
    n = strlen(name);
    if (task_branch_valid(name) && n < size) {
        memcpy(out, name, n + 1U);
        return true;
    }
    return branch_slug(name, out, size - 1U < TASK_BRANCH_SLUG_MAX ? size - 1U : TASK_BRANCH_SLUG_MAX) > 0U;
}

bool task_profile_tool(const char *profile) {
    size_t n = profile ? strlen(profile) : 0U;
    if (!n || n > 64U || profile[0] < 'a' || profile[0] > 'z' || !strcmp(profile, "none")) return false;
    for (size_t i = 1U; i < n; i++) {
        bool separator = profile[i] == '-' || profile[i] == '_';
        if (separator && (i + 1U == n || profile[i + 1U] == '-' || profile[i + 1U] == '_')) return false;
        if (!separator && !((profile[i] >= 'a' && profile[i] <= 'z') || (profile[i] >= '0' && profile[i] <= '9'))) return false;
    }
    return true;
}
