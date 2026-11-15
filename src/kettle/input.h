#ifndef KT_INPUT_H
#define KT_INPUT_H
#include "kettle.h"

enum kt_event_type { KT_KEY, KT_MOUSE, KT_PASTE, KT_PASTE_BEGIN, KT_PASTE_END };
/* Keys that are not a Unicode scalar. KT_KEY_SEQUENCE is a complete escape
 * sequence the decoder does not name; its bytes say what it was. */
enum kt_key { KT_KEY_UP = 0x110000, KT_KEY_DOWN, KT_KEY_LEFT, KT_KEY_RIGHT,
              KT_KEY_HOME, KT_KEY_END, KT_KEY_PAGE_UP, KT_KEY_PAGE_DOWN,
              KT_KEY_TAB, KT_KEY_ENTER, KT_KEY_ESCAPE, KT_KEY_BACKSPACE, KT_KEY_DELETE, KT_KEY_INSERT,
              KT_KEY_F1, KT_KEY_F2, KT_KEY_F3, KT_KEY_F4, KT_KEY_F5, KT_KEY_F6,
              KT_KEY_F7, KT_KEY_F8, KT_KEY_F9, KT_KEY_F10, KT_KEY_F11, KT_KEY_F12,
              KT_KEY_SEQUENCE };
enum kt_mod { KT_MOD_SHIFT = 1, KT_MOD_ALT = 2, KT_MOD_CTRL = 4 };
/* key is what was pressed: a Unicode scalar for a printable key, or a KT_KEY_*
 * value, with the modifiers held in mods. A terminal reports Shift on a
 * printable key as the shifted character, so 'D' arrives as 'D' without
 * KT_MOD_SHIFT. Control bytes arrive as the letter plus KT_MOD_CTRL: 0x02 is
 * 'b', 0x1c is '\\', 0x1d is ']', 0x00 is ' '. Tab, Enter, Escape and Backspace
 * (0x09, 0x0d, 0x1b, 0x7f) are their named keys without a modifier. Escape
 * followed by a key is that key plus KT_MOD_ALT. bytes holds exactly the input
 * that produced the event, for every event type, so a terminal passthrough can
 * forward it unchanged. */
struct kt_event {
    enum kt_event_type type;
    uint32_t key;
    unsigned mods;
    int x, y;
    unsigned button;
    bool release;
    char bytes[64];
    size_t length;
};
/* Pure bounded decoder; caller owns storage and delivers one byte at a time.
 * True produces one event. Call flush after an input timeout: it reports a
 * lone Escape, an Escape followed only by '[' or 'O' as that key with Alt, and
 * drops any longer unfinished sequence. An Escape byte always starts a new
 * sequence, ending an unfinished one the same way. Invalid/overlong CSI is
 * drained. Paste content has a distinct event type, whatever bytes it holds,
 * and cannot become workspace commands. */
struct kt_input { char bytes[64]; size_t length; bool discard, paste; unsigned legacy_remaining; };
void kt_input_init(struct kt_input *input);
bool kt_input_feed(struct kt_input *input, unsigned char byte, struct kt_event *event);
bool kt_input_flush(struct kt_input *input, struct kt_event *event);
/* A key event for exactly this key and these modifiers. */
bool kt_event_is(const struct kt_event *event, uint32_t key, unsigned mods);
/* A key event that types a character: a printable scalar without Ctrl or Alt. */
bool kt_event_text(const struct kt_event *event);
#endif
