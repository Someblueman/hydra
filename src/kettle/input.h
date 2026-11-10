#ifndef KT_INPUT_H
#define KT_INPUT_H
#include "kettle.h"

enum kt_event_type { KT_KEY, KT_MOUSE, KT_PASTE, KT_PASTE_BEGIN, KT_PASTE_END };
enum kt_key { KT_KEY_UP = 0x110000, KT_KEY_DOWN, KT_KEY_LEFT, KT_KEY_RIGHT,
              KT_KEY_HOME, KT_KEY_END, KT_KEY_PAGE_UP, KT_KEY_PAGE_DOWN, KT_KEY_SEQUENCE };
struct kt_event {
    enum kt_event_type type;
    uint32_t key;
    int x, y;
    unsigned button;
    bool release;
    char bytes[64];
    size_t length;
};
/* Pure bounded decoder; caller owns storage and delivers one byte at a time.
 * True produces one event. Call flush after an input timeout to distinguish a
 * lone Escape from a fragmented sequence. Invalid/overlong CSI is drained.
 * Paste content has a distinct event type and cannot become workspace commands. */
struct kt_input { char bytes[64]; size_t length; bool discard, paste; unsigned legacy_remaining; };
void kt_input_init(struct kt_input *input);
bool kt_input_feed(struct kt_input *input, unsigned char byte, struct kt_event *event);
bool kt_input_flush(struct kt_input *input, struct kt_event *event);
#endif
