#include "layout.h"

static int positive(int value) { return value > 0 ? value : 0; }

/* Shortens a span of `length` cells from both ends; the insets meet in the
 * middle of what they overlap when there is not room for both. */
static void shrink(int *start, int *length, int before, int after) {
    int64_t room = positive(*length);
    before = positive(before); after = positive(after);
    if ((int64_t)before + after > room) {
        before = before < room ? before : (int)room;
        after = (int)(room - before);
    }
    *start += before;
    *length = (int)(room - before - after);
}

struct kt_rect kt_rect_inset(struct kt_rect r, int top, int right, int bottom, int left) {
    shrink(&r.x, &r.width, left, right);
    shrink(&r.y, &r.height, top, bottom);
    return r;
}

struct kt_rect kt_rect_pad(struct kt_rect r, int horizontal, int vertical) {
    return kt_rect_inset(r, vertical, horizontal, vertical, horizontal);
}

struct kt_rect kt_rect_cut(struct kt_rect r, enum kt_side side, int size, struct kt_rect *rest) {
    struct kt_rect strip, left;
    bool horizontal = side == KT_SIDE_LEFT || side == KT_SIDE_RIGHT;
    int length;
    r.width = positive(r.width); r.height = positive(r.height);
    strip = left = r;
    length = horizontal ? r.width : r.height;
    size = positive(size) < length ? positive(size) : length;
    if (horizontal) { strip.width = size; left.width = length - size; }
    else { strip.height = size; left.height = length - size; }
    if (side == KT_SIDE_LEFT) left.x += size;
    else if (side == KT_SIDE_RIGHT) strip.x += length - size;
    else if (side == KT_SIDE_TOP) left.y += size;
    else strip.y += length - size;
    if (rest) *rest = left;
    return strip;
}

struct kt_rect kt_rect_center(struct kt_rect r, int width, int height) {
    r.width = positive(r.width); r.height = positive(r.height);
    width = positive(width) < r.width ? positive(width) : r.width;
    height = positive(height) < r.height ? positive(height) : r.height;
    r.x += (r.width - width) / 2; r.y += (r.height - height) / 2;
    r.width = width; r.height = height;
    return r;
}

/* ---- constraint split --------------------------------------------------- */

/* Sizes are written `stride` bytes apart, so they can land in an int array
 * or in a field of an array of rectangles. */
struct split { const struct kt_constraint *constraints; size_t count, stride; char *sizes; };

static int *size_at(const struct split *s, size_t index) {
    return (int *)(void *)(s->sizes + index * s->stride);
}

/* What a part asks for before anything is shared out. */
static int64_t wanted(const struct kt_constraint *c, int available) {
    int value = positive(c->value);
    if (c->size == KT_SIZE_FIXED || c->size == KT_SIZE_MIN) return value;
    if (c->size == KT_SIZE_PERCENT) return (int64_t)available * value / 100;
    return 0;
}

static int weight(const struct kt_constraint *c) {
    if (c->size == KT_SIZE_MIN) return 1;
    if (c->size != KT_SIZE_FLEX) return 0;
    return c->value > 1 ? c->value : 1;
}

/* Grants each part what it asks for, in order, while there is room. Returns
 * what is left over. */
static int grant(const struct split *s, int available) {
    int left = available;
    size_t i;
    for (i = 0; i < s->count; i++) {
        int64_t want = wanted(&s->constraints[i], available);
        int size = want < left ? (int)want : left;
        *size_at(s, i) = size;
        left -= size;
    }
    return left;
}

/* Shares `left` cells among the parts that grow, by weight. */
static void share(const struct split *s, int left) {
    int64_t total = 0;
    int given = 0;
    size_t i;
    for (i = 0; i < s->count; i++) total += weight(&s->constraints[i]);
    if (!total || left < 1) return;
    for (i = 0; i < s->count; i++) {
        int extra = (int)((int64_t)left * weight(&s->constraints[i]) / total);
        *size_at(s, i) += extra;
        given += extra;
    }
    /* Fewer cells remain than there is weight: one each, earliest first. */
    for (i = 0; given < left; i = (i + 1) % s->count)
        if (weight(&s->constraints[i])) { (*size_at(s, i))++; given++; }
}

static void split(const struct split *s, int length, int gap) {
    int64_t gaps = s->count ? (int64_t)positive(gap) * (int64_t)(s->count - 1) : 0;
    int available = gaps < positive(length) ? (int)(positive(length) - gaps) : 0;
    if (!s->count || !s->constraints || !s->sizes) return;
    share(s, grant(s, available));
}

void kt_split(int length, int gap, const struct kt_constraint *constraints, size_t count, int *sizes) {
    struct split s;
    s.constraints = constraints; s.count = count; s.stride = sizeof(*sizes); s.sizes = (char *)sizes;
    split(&s, length, gap);
}

/* Parts follow one another with the gap between; one that would start past
 * the end is an empty rectangle at the end. */
static void place(struct kt_rect r, int gap, struct kt_rect *parts, size_t count, bool columns) {
    int64_t at = 0, length = columns ? positive(r.width) : positive(r.height);
    size_t i;
    for (i = 0; i < count; i++) {
        int size = columns ? parts[i].width : parts[i].height;
        int64_t start = at < length ? at : length;
        parts[i] = r;
        parts[i].width = positive(r.width); parts[i].height = positive(r.height);
        if (columns) { parts[i].x = r.x + (int)start; parts[i].width = size; }
        else { parts[i].y = r.y + (int)start; parts[i].height = size; }
        at += (int64_t)size + positive(gap);
    }
}

void kt_rect_columns(struct kt_rect r, int gap, const struct kt_constraint *constraints, size_t count,
                     struct kt_rect *parts) {
    struct split s;
    if (!parts || !count) return;
    s.constraints = constraints; s.count = count; s.stride = sizeof(*parts); s.sizes = (char *)&parts[0].width;
    split(&s, r.width, gap);
    place(r, gap, parts, constraints ? count : 0, true);
}

void kt_rect_rows(struct kt_rect r, int gap, const struct kt_constraint *constraints, size_t count,
                  struct kt_rect *parts) {
    struct split s;
    if (!parts || !count) return;
    s.constraints = constraints; s.count = count; s.stride = sizeof(*parts); s.sizes = (char *)&parts[0].height;
    split(&s, r.height, gap);
    place(r, gap, parts, constraints ? count : 0, false);
}
