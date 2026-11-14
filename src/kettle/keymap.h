#ifndef KT_KEYMAP_H
#define KT_KEYMAP_H
#include "input.h"

/* One table of bindings drives key matching, on-screen hints, help text and
 * user configuration. The caller owns every table and string; nothing here
 * allocates, reads files or keeps state outside the structures it is given. */
#define KT_CHORD_MAX 4
#define KT_BINDING_CHORDS 2

/* A chord is one to KT_CHORD_MAX keystrokes pressed one after another. */
struct kt_keystroke { uint32_t key; unsigned mods; };
struct kt_chord { struct kt_keystroke keys[KT_CHORD_MAX]; unsigned count; };

/* Reads a chord: keystrokes separated by blanks, each written as modifiers
 * joined to a key with '+' or '-': "ctrl+]", "ctrl+b tab", "shift+tab", "g g",
 * "f5", "alt+enter". Modifiers are ctrl (control), alt (meta) and shift. A key
 * is one character, taken as written ('D' is not 'd'), or a name: tab, enter
 * (return), esc (escape), space, backspace, delete (del), insert (ins), up,
 * down, left, right, home, end, pageup (pgup), pagedown (pgdn), f1 to f12, and
 * comma, hash, plus, minus for those characters. Names and modifiers ignore
 * case.
 *
 * The result is what kt_input reports for the key, because that is all a
 * terminal can send: shift+d is 'D'; ctrl+B is ctrl+b; ctrl+i, ctrl+m and
 * ctrl+[ are tab, enter and esc; ctrl+@ is ctrl+space; ctrl+? is backspace.
 * Shift on any other character is refused: write the character it types.
 * False for empty text, more than KT_CHORD_MAX keystrokes or an unknown key. */
bool kt_chord_parse(const char *text, size_t length, struct kt_chord *out);
/* Display form for hints, such as "Ctrl-]" or "Ctrl-\ x". kt_chord_config is
 * the spelling for configuration text, such as "ctrl+\ x". A chord that
 * kt_chord_parse produced reads back from either form as the same chord. Each
 * returns the length of the whole text; output is cut at capacity, between
 * pieces rather than inside a character, and terminated when capacity allows. */
size_t kt_chord_format(const struct kt_chord *chord, char *out, size_t capacity);
size_t kt_chord_config(const struct kt_chord *chord, char *out, size_t capacity);
/* The compact form footers and help screens show: named keys in lower case
 * ("enter", "esc", "pgdn", "space"), arrows as their glyphs (U+2190-2193,
 * which an ASCII canvas draws as < ^ > v) and modifiers spelled as in
 * configuration ("ctrl+]", "shift+tab", "g g"). Returns the full length and
 * cuts output as kt_chord_format does. */
size_t kt_chord_hint(const struct kt_chord *chord, char *out, size_t capacity);
bool kt_chord_equal(const struct kt_chord *a, const struct kt_chord *b);
/* True when chord begins with every keystroke of prefix and is longer. */
bool kt_chord_extends(const struct kt_chord *chord, const struct kt_chord *prefix);

/* name is the stable identifier configuration uses, such as "terminal.leave";
 * label is short help text, such as "leave input". action and scope belong to
 * the application. scope is a bit set: the binding is live whenever one of
 * its scopes is among those passed to kt_keymap_feed, so an application adds
 * scopes later by adding bits and rows. A binding without chords is unbound.
 * line is the configuration line that last set the binding, or 0.
 *
 * group and priority are for generated help (components.h): group is the
 * heading the binding is listed under in the full form (NULL: the first,
 * untitled group), and priority orders what the one-line form keeps when it
 * runs out of room: higher stays longer, and a negative priority lists the
 * binding in the full form only. A binding without a label is in neither. */
struct kt_binding {
    const char *name, *label;
    uint32_t action, scope;
    struct kt_chord chords[KT_BINDING_CHORDS];
    unsigned chord_count, line;
    const char *group;
    int priority;
};
struct kt_keymap { struct kt_binding *bindings; size_t count; };
/* pending holds the keystrokes of a chord entered so far (count 0 when idle),
 * so a caller can show it. abandoned is valid after KT_KEY_ABANDONED. */
struct kt_keymap_state { struct kt_chord pending, abandoned; };

enum kt_key_match {
    /* The key starts no live chord and nothing was pending: it is the caller's. */
    KT_KEY_UNBOUND,
    /* The key continues a chord that needs more keys. It is consumed. */
    KT_KEY_PENDING,
    /* The key completes a chord; *matched is its binding. It is consumed. */
    KT_KEY_ACTION,
    /* A chord was pending and this key does not continue it. The pending keys
     * and this key are all consumed: none of them is replayed, and this key
     * does not start a new chord. state->abandoned holds the pending keys
     * followed by this key, for the caller to report. A terminal passthrough
     * therefore never sends part of a chord to its child. */
    KT_KEY_ABANDONED
};
/* Matches one key event against the bindings live in scope. Only KT_KEY
 * events take part: any other event returns KT_KEY_UNBOUND and leaves a
 * pending chord pending. Letters match by case. When the table holds both a
 * chord and a longer chord starting with it, the shorter one wins; use
 * kt_keymap_check to refuse such tables. matched may be NULL. */
enum kt_key_match kt_keymap_feed(const struct kt_keymap *map, struct kt_keymap_state *state, uint32_t scope,
                                 const struct kt_event *event, const struct kt_binding **matched);
void kt_keymap_reset(struct kt_keymap_state *state);
const struct kt_binding *kt_keymap_find(const struct kt_keymap *map, const char *name);

/* Applies configuration text: one "name = chord[, chord]" per line, at most
 * KT_BINDING_CHORDS chords; "name = none" unbinds; '#' starts a comment
 * anywhere on a line; blank lines are ignored. A line names a binding by its
 * name and replaces all of its chords. Every valid line is applied even when
 * others are not; a bad line leaves its binding as it was. False when any
 * line was refused: error then holds the first one as "line N: reason", and
 * how many more there were. Conflicts between bindings are a property of the
 * whole table: call kt_keymap_check afterwards. */
bool kt_keymap_configure(struct kt_keymap *map, const char *text, size_t length, char *error, size_t error_capacity);
/* Refuses a table in which two chords live in a common scope are the same, or
 * one is the beginning of the other (the longer could never be entered).
 * error names the bindings and chords involved. */
bool kt_keymap_check(const struct kt_keymap *map, char *error, size_t error_capacity);
/* One configuration line for a binding, without a newline: "name = chord,
 * chord" or "name = none". Returns the full length, as kt_chord_format does. */
size_t kt_binding_config(const struct kt_binding *binding, char *out, size_t capacity);
#endif
