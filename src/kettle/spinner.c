#include "component_internal.h"
#include <string.h>

/* Activity and progress: a spinner whose timer runs only while it is shown
 * and working, and a bar measured in eighths of a cell. */

static const char *const dots[] = {"\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8", "\xe2\xa0\xbc",
                                   "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7", "\xe2\xa0\x87", "\xe2\xa0\x8f"};
static const char *const lines[] = {"|", "/", "-", "\\"};

void kt_spinner_init(struct kt_spinner *s, uint64_t tag) {
    memset(s, 0, sizeof(*s));
    s->frames = dots;
    s->frame_count = sizeof(dots) / sizeof(dots[0]);
    s->ascii_frames = lines;
    s->ascii_count = sizeof(lines) / sizeof(lines[0]);
    s->interval_ms = 80;
    s->tag = tag;
    s->visible = true;
    s->style = KT_INK(KT_COLOR_ACCENT, 0);
    s->label_style = KT_INK(KT_COLOR_MUTED, 0);
}

/* The timer exists exactly while the spinner runs and is shown. */
static void sync(struct kt_spinner *s, struct kt_cmds *cmds) {
    bool wanted = s->running && s->visible;
    if (wanted && !s->timer) s->timer = kt_cmd_timer(cmds, s->tag, s->interval_ms, s->interval_ms);
    if (!wanted && s->timer) {
        kt_cmd_cancel(cmds, s->timer);
        s->timer = 0;
    }
}

void kt_spinner_start(struct kt_spinner *s, struct kt_cmds *cmds) { s->running = true; sync(s, cmds); }
void kt_spinner_stop(struct kt_spinner *s, struct kt_cmds *cmds) { s->running = false; sync(s, cmds); }
void kt_spinner_show(struct kt_spinner *s, bool visible, struct kt_cmds *cmds) { s->visible = visible; sync(s, cmds); }

enum kt_spinner_result kt_spinner_update(struct kt_spinner *s, const struct kt_msg *msg, struct kt_cmds *cmds) {
    (void)cmds;
    if (msg->type != KT_MSG_TIMER || msg->tag != s->tag || !s->timer || msg->u.timer.id != s->timer)
        return KT_SPINNER_IGNORED;
    s->frame++;
    return KT_SPINNER_HANDLED;
}

void kt_spinner_view(const struct kt_spinner *s, struct kt_canvas *canvas, struct kt_rect area) {
    const char *const *frames = canvas->unicode ? s->frames : s->ascii_frames;
    size_t count = canvas->unicode ? s->frame_count : s->ascii_count;
    int used = 0;
    if (!s->visible || area.width < 1 || area.height < 1) return;
    if (s->running && frames && count) used = kt_text_aligned(canvas, area, frames[s->frame % count], KT_ALIGN_LEFT, s->style);
    if (s->label) (void)kt_text_aligned(canvas, kt_rect_inset(area, 0, 0, 0, used ? used + 1 : 0), s->label,
                                        KT_ALIGN_LEFT, s->label_style);
}

/* ---- progress ----------------------------------------------------------- */

void kt_progress_init(struct kt_progress *p) {
    memset(p, 0, sizeof(*p));
    p->percent = true;
    p->fill = KT_STYLE(KT_COLOR_ACCENT, KT_COLOR_SUNKEN, 0);
    p->track = KT_STYLE(KT_COLOR_FAINT, KT_COLOR_SUNKEN, 0);
    p->label = KT_INK(KT_COLOR_MUTED, 0);
}

void kt_progress_set(struct kt_progress *p, double value) {
    if (!(value >= 0.0)) value = 0.0;   /* NaN as well */
    p->value = value > 1.0 ? 1.0 : value;
}

/* Left eighths of a cell: U+258F (one eighth) to U+2589 (seven). */
static uint32_t eighths_glyph(int eighths) { return 0x2590U - (uint32_t)eighths; }

static void draw_bar(const struct kt_progress *p, struct kt_canvas *canvas, struct kt_rect bar) {
    long total = (long)(p->value * bar.width * 8 + 0.5);
    int full = (int)(total / 8), part = (int)(total % 8), x;
    kt_fill(canvas, bar, p->track);
    for (x = 0; x < bar.width; x++) {
        bool whole = x < full, partial = x == full && part > 0;
        if (!canvas->unicode) {
            kt_put(canvas, bar.x + x, bar.y, whole || (partial && part >= 4) ? '#' : '-', whole ? p->fill : p->track);
            continue;
        }
        if (whole) kt_put(canvas, bar.x + x, bar.y, 0x2588U, p->fill);
        else if (partial) kt_put(canvas, bar.x + x, bar.y, eighths_glyph(part), p->fill);
    }
}

void kt_progress_view(const struct kt_progress *p, struct kt_canvas *canvas, struct kt_rect area) {
    struct kt_rect bar = {area.x, area.y, area.width, 1};
    char text[8];
    if (area.width < 1 || area.height < 1) return;
    if (p->percent && area.width > 6) {
        snprintf(text, sizeof(text), "%3d%%", (int)(p->value * 100 + 0.5));
        bar.width -= 5;
        (void)kt_text_aligned(canvas, (struct kt_rect){area.x + area.width - 4, area.y, 4, 1}, text, KT_ALIGN_RIGHT,
                              p->label);
    }
    draw_bar(p, canvas, bar);
}
