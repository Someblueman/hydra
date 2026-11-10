#ifndef HYDRA_TUI_KEYS_H
#define HYDRA_TUI_KEYS_H
#include "model.h"
#include "../kettle/keymap.h"
#include <stdio.h>
/* Hydra's configurable keys. One binding table (keys.c) drives matching, the
 * hints and help that name keys, and the user's keys file. */
struct app;
/* Where a binding is live; each scope is one bit, so a binding can belong to
 * several and new scopes take the next bit without touching existing rows.
 * TERMINAL: Hydra's own commands while an agent pane may hold input. */
enum key_scope { KEY_SCOPE_TERMINAL = 1U << 0 };
enum key_action {
    KEY_TERMINAL_LEAVE, KEY_TERMINAL_CLOSE, KEY_TERMINAL_NEXT, KEY_TERMINAL_RECONNECT,
    KEY_TERMINAL_HISTORY, KEY_TERMINAL_LIVE, KEY_APP_QUIT, KEY_VIEW_STATISTICS,
    KEY_LAYOUT_CONVERSATION, KEY_LAYOUT_PLAN, KEY_LAYOUT_MONITOR,
    KEY_WORKSPACE_ZOOM, KEY_WORKSPACE_SPLIT, KEY_TERMINAL_LITERAL, KEY_ACTIONS
};
struct native_keys {
    struct kt_binding bindings[KEY_ACTIONS];
    struct kt_keymap map;
    /* The TERMINAL-scope chord being entered, and the decoder of its keys. */
    struct kt_keymap_state state;
    struct kt_input input;
    /* The keys file read at startup (or where it would be), and why it was
     * not applied exactly as written; problem is empty when it was. */
    char path[4096], problem[TEXT];
    /* The notice shown for the last chord that matched nothing. */
    char unbound[TEXT];
    bool ready;
};
/* The app's keys, with the built-in defaults until native_keys_load runs. */
struct native_keys *native_keys(struct app *app);
/* Reads the keys file: HYDRA_TUI_KEYS, else $HYDRA_HOME/tui/keys.conf (or
 * ~/.hydra). A missing default file means defaults. A refused line keeps that
 * action's default, as does unbinding terminal.leave (input could never
 * return to Hydra); conflicting chords put every key back to its default.
 * Either way keys->problem says which file, line and why. default_file is
 * false for deterministic rendering, which reads only an explicit file. */
void native_keys_load(struct app *app, bool default_file);
/* Writes the effective bindings in the keys-file syntax. */
void native_keys_print(struct app *app, FILE *out);
/* Display text of an action's first chord, such as "Ctrl-]"; false and empty
 * when the action is unbound. */
bool key_text(struct app *app, enum key_action action, char *out, size_t size);
/* Builds hint text from a template of segments separated by '|', written
 * joined by separator. "{terminal.leave}" becomes that action's chord. A
 * segment naming an unbound action is left out, or replaced by the text after
 * '~' when the segment has one: "a or {terminal.reconnect} reconnect~a reconnect". */
void key_hint(struct app *app, char *out, size_t size, const char *separator, const char *template_text);
/* The first template whose text fits width; the last one when none fits. */
void key_hint_fit(struct app *app, char *out, size_t size, const char *separator,
                  const char *const templates[], size_t count, int width);
/* While a chord is partly entered: its keys so far and what may follow, such
 * as "Ctrl-\ ...  x close / n next agent". False when no chord is pending. */
bool key_pending_text(struct app *app, char *out, size_t size);
#endif
