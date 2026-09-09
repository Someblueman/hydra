#include "terminal_internal.h"
#include <stdio.h>
#include <string.h>

static int argument(const struct tv_terminal_model *t, unsigned index, int fallback) {
    return index < t->parameter_count && t->parameters[index] ? (int)t->parameters[index] : fallback;
}

void tv_term_reply(struct tv_terminal_model *t, const char *text) {
    size_t length = strlen(text);
    if (length <= sizeof(t->reply) - t->reply_length) {
        memcpy(t->reply + t->reply_length, text, length); t->reply_length += length;
    }
}

size_t tv_term_take_reply(struct tv_terminal_model *t, char *out, size_t capacity) {
    size_t count = t->reply_length < capacity ? t->reply_length : capacity;
    if (!out) return 0;
    memcpy(out, t->reply, count);
    memmove(t->reply, t->reply + count, t->reply_length - count);
    t->reply_length -= count;
    return count;
}

static void mode(struct tv_terminal_model *t, unsigned value, bool enabled) {
    struct tv_screen *s = tv_term_screen(t);
    if (!t->private_mode) {
        if (value == 4) t->insert = enabled;
        else if (t->unsupported != UINT64_MAX) t->unsupported++;
        return;
    }
    switch (value) {
    case 1: t->application_cursor = enabled; break;
    case 6: t->origin = enabled; s->x = 0; s->y = enabled ? s->top : 0; break;
    case 7: t->autowrap = enabled; s->wrap_pending = false; break;
    case 25: t->cursor_visible = enabled; break;
    case 47: case 1047: case 1049:
        if (enabled == t->alternate_active) break;
        if (value == 1049 && enabled) tv_term_save(t);
        t->alternate_active = enabled;
        if (enabled && value != 47) {
            int y;
            t->alternate.x = t->alternate.y = 0; t->alternate.wrap_pending = false;
            for (y = 0; y < t->alternate.canvas.height; y++) tv_term_erase(t, y, 0, t->alternate.canvas.width - 1);
        }
        if (!enabled && value == 1049) tv_term_restore(t);
        break;
    case 1048: if (enabled) tv_term_save(t); else tv_term_restore(t); break;
    case 1000: case 1002: case 1003: t->mouse_mode = enabled ? value : 0; break;
    case 1006: t->mouse_sgr = enabled; break;
    case 2004: t->bracketed_paste = enabled; break;
    default: if (t->unsupported != UINT64_MAX) t->unsupported++; break;
    }
}

static void shift_chars(struct tv_terminal_model *t, bool insert, int count) {
    struct tv_screen *s = tv_term_screen(t);
    struct tv_cell *row = s->canvas.cells + (size_t)s->y * (size_t)s->canvas.stride;
    struct tv_cell empty = tv_term_blank(t);
    int x;
    if (count > s->canvas.width - s->x) count = s->canvas.width - s->x;
    if (insert) {
        memmove(row + s->x + count, row + s->x, (size_t)(s->canvas.width - s->x - count) * sizeof(*row));
        for (x = s->x; x < s->x + count; x++) row[x] = empty;
    } else {
        memmove(row + s->x, row + s->x + count, (size_t)(s->canvas.width - s->x - count) * sizeof(*row));
        for (x = s->canvas.width - count; x < s->canvas.width; x++) row[x] = empty;
    }
    for (x = 0; x < s->canvas.width; x++) {
        if ((!row[x].width && (!x || row[x-1].width != 2)) ||
            (row[x].width == 2 && (x == s->canvas.width - 1 || row[x+1].width != 0))) row[x] = empty;
    }
}

void tv_term_csi(struct tv_terminal_model *t, unsigned char final) {
    struct tv_screen *s = tv_term_screen(t);
    int n = argument(t, 0, 1), value = (int)t->parameters[0], y;
    int top = t->origin ? s->top : 0, bottom = t->origin ? s->bottom : s->canvas.height - 1;
    unsigned i;
    char reply[64];
    if (t->invalid_sequence) { if (t->unsupported != UINT64_MAX) t->unsupported++; return; }
    if (final != 'm' && final != 'n' && final != 'c') s->wrap_pending = false;
    if (t->private_mode && final != 'h' && final != 'l' && final != 'n') {
        if (t->unsupported != UINT64_MAX) t->unsupported++;
        return;
    }
    switch (final) {
    case 'A': s->y -= n; break;
    case 'B': case 'e': s->y += n; break;
    case 'C': case 'a': s->x += n; break;
    case 'D': s->x -= n; break;
    case 'E': s->y += n; s->x = 0; break;
    case 'F': s->y -= n; s->x = 0; break;
    case 'G': case '`': s->x = n - 1; break;
    case 'd': s->y = top + n - 1; break;
    case 'H': case 'f': s->y = top + n - 1; s->x = argument(t, 1, 1) - 1; break;
    case 'J':
        if (value == 0) {
            tv_term_erase(t, s->y, s->x, s->canvas.width - 1);
            for (y = s->y + 1; y < s->canvas.height; y++) tv_term_erase(t, y, 0, s->canvas.width - 1);
        } else if (value == 1) {
            for (y = 0; y < s->y; y++) tv_term_erase(t, y, 0, s->canvas.width - 1);
            tv_term_erase(t, s->y, 0, s->x);
        } else if (value == 2) {
            for (y = 0; y < s->canvas.height; y++) tv_term_erase(t, y, 0, s->canvas.width - 1);
        } else if (value == 3) t->history_count = t->history_start = 0;
        break;
    case 'K': tv_term_erase(t, s->y, value == 0 ? s->x : 0, value == 1 ? s->x : s->canvas.width - 1); break;
    case 'X': tv_term_erase(t, s->y, s->x, s->x + n - 1); break;
    case '@': shift_chars(t, true, n); break;
    case 'P': shift_chars(t, false, n); break;
    case 'L': if (s->y >= s->top && s->y <= s->bottom) tv_term_scroll(t, s->y, s->bottom, -n); break;
    case 'M': if (s->y >= s->top && s->y <= s->bottom) tv_term_scroll(t, s->y, s->bottom, n); break;
    case 'S': tv_term_scroll(t, s->top, s->bottom, n); break;
    case 'T': tv_term_scroll(t, s->top, s->bottom, -n); break;
    case 'r':
        y = argument(t, 1, s->canvas.height) - 1;
        if (n - 1 < y && y < s->canvas.height) { s->top = n - 1; s->bottom = y; s->x = 0; s->y = t->origin ? s->top : 0; }
        break;
    case 's': tv_term_save(t); break;
    case 'u': tv_term_restore(t); break;
    case 'm': tv_term_sgr(t); break;
    case 'h': case 'l':
        for (i = 0; i < t->parameter_count; i++) mode(t, t->parameters[i], final == 'h');
        return;
    case 'n':
        if (value == 5 && !t->private_mode) tv_term_reply(t, "\033[0n");
        else if (value == 6) {
            snprintf(reply, sizeof(reply), "\033[%s%d;%dR", t->private_mode ? "?" : "", s->y - top + 1, s->x + 1);
            tv_term_reply(t, reply);
        }
        break;
    case 'c': if (!value) tv_term_reply(t, "\033[?1;0c"); break;
    case 't':
        if (value == 18) { snprintf(reply, sizeof(reply), "\033[8;%d;%dt", s->canvas.height, s->canvas.width); tv_term_reply(t, reply); }
        else if (t->unsupported != UINT64_MAX) t->unsupported++;
        break;
    default: if (t->unsupported != UINT64_MAX) t->unsupported++; break;
    }
    if (s->x < 0) s->x = 0;
    if (s->x >= s->canvas.width) s->x = s->canvas.width - 1;
    if (s->y < top) s->y = top;
    if (s->y > bottom) s->y = bottom;
}
