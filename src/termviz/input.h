#ifndef TV_INPUT_H
#define TV_INPUT_H
#include "termviz.h"

enum tv_event_type { TV_KEY, TV_MOUSE, TV_PASTE, TV_PASTE_BEGIN, TV_PASTE_END };
enum tv_key { TV_KEY_UP = 0x110000, TV_KEY_DOWN, TV_KEY_LEFT, TV_KEY_RIGHT,
              TV_KEY_HOME, TV_KEY_END, TV_KEY_PAGE_UP, TV_KEY_PAGE_DOWN, TV_KEY_SEQUENCE };
struct tv_event {
    enum tv_event_type type;
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
struct tv_input { char bytes[64]; size_t length; bool discard, paste; unsigned legacy_remaining; };
void tv_input_init(struct tv_input *input);
bool tv_input_feed(struct tv_input *input, unsigned char byte, struct tv_event *event);
bool tv_input_flush(struct tv_input *input, struct tv_event *event);
#endif
