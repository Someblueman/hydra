#ifndef KT_COMPONENTS_H
#define KT_COMPONENTS_H
/* Components: the pieces an application is built from (a footer of key
 * hints, a list, a table, a text field, a dialog, ...), in the shape of the
 * runtime in program.h.
 *
 * Each component is a plain struct that the application keeps in its model
 * and initialises in place with kt_<name>_init. Nothing here allocates:
 * storage a component needs is inline and bounded, or supplied by the
 * caller, and the data it shows comes through callbacks or caller-owned
 * arrays that are read where they are, never copied.
 *
 *   kt_<name>_update(component, msg, cmds)
 *       feeds it one message and says what happened in the component's own
 *       result enum. Every one starts with _IGNORED (0: not for this
 *       component, the application handles it) and _HANDLED (1: consumed,
 *       nothing to act on); the values after those are outcomes the
 *       application acts on, such as KT_LIST_CHOSEN. Keys are routed by
 *       offering them to the focused component first.
 *   kt_<name>_view(component, canvas, area)
 *       draws it into an area of the frame. It never changes the component.
 *   kt_<name>_place(component, area)
 *       tells it, from update, where its view will draw: scrolling needs the
 *       height and the mouse needs the position. Call it when the layout
 *       changes (on KT_MSG_RESIZE) with the area the view will be given.
 *
 * Keys come from each component's own binding table, `keys`, in keymap.h
 * terms: an application rebinds them with kt_keymap_configure on
 * kt_<name>_keymap(), and the footer and help screen are generated from the
 * same table that drives input (struct kt_help). Every look is a theme token
 * by default, in the component's `style` member, which an application may
 * change per instance after init.
 *
 * A component that keeps time (spinner, toast) asks for a timer with the tag
 * given at init, recognises its own KT_MSG_TIMER by that tag and the timer's
 * id, and cancels the timer whenever it has nothing to show, so an idle
 * program stays asleep.
 *
 * Part of the optional POSIX layer: messages and commands are program.h's. */
#include "program.h"
#include "keymap.h"
#include "layout.h"

/* Every scope bit: a binding table read as a whole. */
#define KT_SCOPE_ALL UINT32_MAX
/* The position of no item. */
#define KT_NONE ((size_t)-1)

/* ---- text -------------------------------------------------------------- */

/* A paragraph: text wrapped at blanks to the area's width, one row per line,
 * with '\n' starting a new line. A word longer than the width is cut between
 * cells. Returns the rows used; rows beyond the area are not drawn.
 * kt_paragraph_height counts the rows the same text takes at a width. */
int kt_paragraph(struct kt_canvas *canvas, struct kt_rect area, const char *text, struct kt_style style);
int kt_paragraph_height(const char *text, int width);

/* ---- fuzzy matching ----------------------------------------------------- */

/* Most matched characters kt_fuzzy reports positions for. */
#define KT_FUZZY_POSITIONS 64
/* Scores `text` against `pattern` typed as an abbreviation: every character
 * of the pattern must appear in the text in order, ignoring ASCII case;
 * blanks in the pattern are skipped. Returns -1 when the text does not match,
 * otherwise a score where higher is better: characters that are adjacent,
 * that start the text or a word, and a short overall span score more. An
 * empty pattern matches everything with score 0. `positions`, when not NULL,
 * receives the byte offset in `text` of each matched character, at most
 * `capacity` of them; their count is the pattern's length in characters
 * less its blanks, up to capacity. */
int kt_fuzzy(const char *pattern, const char *text, size_t *positions, size_t capacity);

/* A filtered item: its index in the caller's data and its score. */
struct kt_match { size_t index; int score; };
/* Orders matches best first: higher score, then lower index. In place, no
 * allocation, O(n log n). */
void kt_fuzzy_rank(struct kt_match *matches, size_t count);

/* ---- help and footer ---------------------------------------------------- */

/* One binding table and the scopes of it that are live, as kt_keymap_feed
 * reads it. A component's table is `keys` with kt_<name>_scope(). */
struct kt_help_source { const struct kt_binding *bindings; size_t count; uint32_t scope; };
#define KT_HELP_SOURCES 8

/* Key hints generated from binding tables, so what a footer says is what the
 * keys do. An entry is a binding live in its source's scope that has a label
 * and a chord; bindings next to each other in one table with the same label
 * are one entry ("up" and "down" labelled "select" read "up/down select"),
 * and three or more read first to last ("1-9 jump to tab"). Keys are written
 * with kt_chord_hint.
 *
 * The short form is one line that fits its width, entries separated by
 * a bullet (U+2022; a bar on an ASCII canvas). It lists entries in source
 * order; when they do not all fit, it drops the lowest-priority entries
 * (the latest first among equals), never part of one, and ends with the
 * help's own toggle and "more" ("? more"). A negative priority keeps a
 * binding out of the short form.
 *
 * The full form lists every entry with all of its chords, under the heading
 * of its binding's group, groups in the order they first appear, each
 * group's entries in as many columns as the width holds, filled top to
 * bottom. Key columns line up across groups. */
enum kt_help_action { KT_HELP_TOGGLE, KT_HELP_CLOSE, KT_HELP_ACTIONS };
enum kt_help_result { KT_HELP_IGNORED, KT_HELP_HANDLED, KT_HELP_OPENED, KT_HELP_CLOSED };
struct kt_help_style {
    struct kt_style key, label, separator, heading;
};
struct kt_help {
    struct kt_help_source sources[KT_HELP_SOURCES];
    size_t count;
    bool full;              /* the full form is open (toggled by the keys)    */
    const char *more;       /* label after the toggle when entries dropped    */
    struct kt_help_style style;
    struct kt_binding keys[KT_HELP_ACTIONS];   /* help.toggle "?", help.close "esc" */
    struct kt_keymap_state chord;
};
void kt_help_init(struct kt_help *help);
/* Sets what the help describes. Sources are listed in order; the help's own
 * toggle is not added unless passed. Returns false, keeping the first
 * KT_HELP_SOURCES, when there are more. The tables are not copied and must
 * stay in place while the help uses them. */
bool kt_help_set(struct kt_help *help, const struct kt_help_source *sources, size_t count);
/* Toggle opens and closes the full form; close (Esc) closes it while open. */
enum kt_help_result kt_help_update(struct kt_help *help, const struct kt_msg *msg, struct kt_cmds *cmds);
/* The short form on the first row of the area; returns the columns drawn. */
int kt_help_view(const struct kt_help *help, struct kt_canvas *canvas, struct kt_rect area);
/* The full form: headings and columns from the area's top-left; returns the
 * rows drawn. kt_help_full_height says how many rows it takes at a width,
 * kt_help_full_width how wide it is with a number of columns. */
int kt_help_full_view(const struct kt_help *help, struct kt_canvas *canvas, struct kt_rect area);
int kt_help_full_height(const struct kt_help *help, int width);
int kt_help_full_width(const struct kt_help *help, int columns);
uint32_t kt_help_scope(const struct kt_help *help);
struct kt_keymap kt_help_keymap(struct kt_help *help);
/* The short form of any sources, with a given toggle chord for "more" (NULL
 * for none), for a footer or a dialog's bottom edge. */
int kt_help_line(struct kt_canvas *canvas, struct kt_rect area, const struct kt_help_source *sources, size_t count,
                 const struct kt_binding *toggle, const char *more, const struct kt_help_style *style);

/* ---- text input --------------------------------------------------------- */

/* One line of editable text: a cursor, horizontal scrolling, a prompt, a
 * placeholder, a character limit, validation and suggestions.
 *
 * The text lives in the caller's buffer (kt_textinput_init) or, given NULL,
 * in KT_TEXTINPUT_LOCAL bytes inside the struct. Printable keys insert; a
 * paste arrives as one message and is inserted as one edit, with line breaks
 * and tabs as spaces and other controls left out. Word motions are alt+b and
 * alt+f (ctrl+left and ctrl+right); alt+backspace and ctrl+w delete a word
 * back, ctrl+u and ctrl+k to the start and the end. The cursor is drawn as a
 * reverse-video cell; kt_textinput_cursor gives its position for an
 * application that shows the terminal's cursor there instead.
 *
 * The validation hook runs after every change and on Enter; it returns false
 * with a reason in `error`. Enter then reports _INVALID instead of
 * _SUBMITTED. Suggestions are caller strings: the first one that extends
 * what is typed shows faintly after the cursor, tab or right at the end
 * takes it, ctrl+n and ctrl+p step through the others. */
#define KT_TEXTINPUT_LOCAL 128
#define KT_TEXTINPUT_ERROR 96
enum kt_textinput_action {
    KT_TEXTINPUT_LEFT, KT_TEXTINPUT_RIGHT, KT_TEXTINPUT_WORD_LEFT, KT_TEXTINPUT_WORD_RIGHT,
    KT_TEXTINPUT_HOME, KT_TEXTINPUT_END, KT_TEXTINPUT_BACKSPACE, KT_TEXTINPUT_DELETE,
    KT_TEXTINPUT_DELETE_WORD, KT_TEXTINPUT_DELETE_START, KT_TEXTINPUT_DELETE_END,
    KT_TEXTINPUT_ACCEPT, KT_TEXTINPUT_NEXT_SUGGESTION, KT_TEXTINPUT_PREVIOUS_SUGGESTION,
    KT_TEXTINPUT_SUBMIT, KT_TEXTINPUT_ACTIONS
};
enum kt_textinput_result {
    KT_TEXTINPUT_IGNORED, KT_TEXTINPUT_HANDLED,
    KT_TEXTINPUT_CHANGED,     /* the text changed                               */
    KT_TEXTINPUT_SUBMITTED,   /* Enter, and the text is valid                   */
    KT_TEXTINPUT_INVALID      /* Enter, refused by validation: see `error`      */
};
struct kt_textinput_style {
    struct kt_style text, prompt, prompt_focused, placeholder, cursor, suggestion, field;
};
typedef bool (*kt_validate_fn)(void *context, const char *text, char *error, size_t capacity);
struct kt_textinput {
    char *buffer;                 /* NULL: `local`                              */
    size_t capacity;              /* bytes in buffer, terminator included       */
    size_t length, cursor;        /* bytes; the cursor is on a scalar boundary  */
    size_t offset;                /* first byte shown                           */
    size_t limit;                 /* most characters; 0: as many as fit         */
    const char *prompt;           /* before the text; default U+2503 and a space */
    const char *placeholder;      /* drawn faintly while empty                  */
    bool focused;                 /* draws the cursor and the focused prompt    */
    kt_validate_fn validate;
    void *validate_context;
    bool valid;
    char error[KT_TEXTINPUT_ERROR];
    const char *const *suggestions;
    size_t suggestion_count, suggestion;   /* which matching one is offered     */
    struct kt_rect area;
    struct kt_textinput_style style;
    struct kt_binding keys[KT_TEXTINPUT_ACTIONS];
    struct kt_keymap_state chord;
    char local[KT_TEXTINPUT_LOCAL];
};
/* buffer may be NULL; otherwise capacity is at least 1 and the buffer's
 * current text, if terminated within it, is kept. */
void kt_textinput_init(struct kt_textinput *input, char *buffer, size_t capacity);
enum kt_textinput_result kt_textinput_update(struct kt_textinput *input, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_textinput_view(const struct kt_textinput *input, struct kt_canvas *canvas, struct kt_rect area);
void kt_textinput_place(struct kt_textinput *input, struct kt_rect area);
const char *kt_textinput_value(const struct kt_textinput *input);
/* Replaces the text, cut to the capacity and limit; the cursor goes to the end. */
void kt_textinput_set(struct kt_textinput *input, const char *text);
/* Inserts at the cursor as typing would; false when nothing fitted. */
bool kt_textinput_insert(struct kt_textinput *input, const char *text, size_t length);
/* The suggestion offered now, or NULL. */
const char *kt_textinput_suggestion(const struct kt_textinput *input);
/* Where the cursor is drawn in the placed area; false when unfocused. */
bool kt_textinput_cursor(const struct kt_textinput *input, int *x, int *y);
uint32_t kt_textinput_scope(const struct kt_textinput *input);
struct kt_keymap kt_textinput_keymap(struct kt_textinput *input);

/* ---- list --------------------------------------------------------------- */

/* One row of a list, as the application's callback describes it. Strings
 * are borrowed and must stay valid until the view or update that asked
 * returns. */
struct kt_list_item {
    const char *title;
    const char *detail;     /* height 1: right-aligned; height 2: second line */
    const char *icon;       /* a glyph before the title, such as a status dot */
    kt_color icon_color;    /* KT_DEFAULT: the title's colour                 */
    bool header;            /* a section header: never selected               */
};
typedef void (*kt_list_item_fn)(void *context, size_t index, struct kt_list_item *item);

/* A selectable, scrolling list over the caller's items, read through a
 * callback so nothing is copied, with optional section headers, items one or
 * two rows high, a status line, an empty text and filter-as-you-type.
 *
 * Rows: without a filter, every item in order. With one, the items whose
 * title matches it (kt_fuzzy), best first unless `ranked` is false, matched
 * characters highlighted; headers are left out while filtering. The filter
 * needs caller storage for its matches (kt_list_filter_storage); with
 * fewer slots than matches, the first that fit in item order are shown.
 *
 * '/' starts typing a filter, which goes to an embedded text input: typed
 * characters edit it, the arrow and page keys still move, Enter keeps the
 * filter and returns to the list, and Esc, then or later, clears it. The selected row is filled and marked
 * with a bar at its left edge; an unfocused list keeps the bar only. The
 * mouse wheel scrolls; a click selects, and a click on the selected item
 * chooses it. */
enum kt_list_action {
    KT_LIST_UP, KT_LIST_DOWN, KT_LIST_PAGE_UP, KT_LIST_PAGE_DOWN, KT_LIST_TOP, KT_LIST_BOTTOM,
    KT_LIST_CHOOSE, KT_LIST_FILTER, KT_LIST_FILTER_ACCEPT, KT_LIST_FILTER_CLEAR, KT_LIST_ACTIONS
};
enum kt_list_result {
    KT_LIST_IGNORED, KT_LIST_HANDLED,
    KT_LIST_MOVED,     /* the selection moved to another item (or none)      */
    KT_LIST_CHOSEN     /* Enter or a click on the selected item              */
};
/* Scope bits of the list's bindings; kt_list_scope says which are live:
 * browsing, filtering possible, typing a filter, a filter applied. */
enum { KT_LIST_SCOPE_BROWSE = 1, KT_LIST_SCOPE_FILTERABLE = 2, KT_LIST_SCOPE_FILTERING = 4, KT_LIST_SCOPE_FILTERED = 8 };
struct kt_list_style {
    struct kt_style title, title_selected, detail, header, header_rule, selection, bar, bar_unfocused,
        match, status, empty, scrollbar, thumb;
};
struct kt_list {
    kt_list_item_fn item;
    void *context;
    size_t count;              /* items in the caller's data                     */
    size_t selected;           /* row of the selection, KT_NONE when none        */
    size_t top;                /* first row shown                                */
    int item_height;           /* 1 or 2 rows per item; headers take one         */
    bool focused, ranked, status_line, scrollbar;
    const char *empty;         /* shown when there are no rows                   */
    struct kt_match *matches;  /* filter storage                                 */
    size_t match_capacity, match_count;
    bool filtering;            /* the filter input has the keys                  */
    struct kt_textinput filter;
    struct kt_rect area;
    struct kt_list_style style;
    struct kt_binding keys[KT_LIST_ACTIONS];
    struct kt_keymap_state chord;
};
void kt_list_init(struct kt_list *list, kt_list_item_fn item, void *context, size_t count);
/* Enables filtering with room for `capacity` matches. */
void kt_list_filter_storage(struct kt_list *list, struct kt_match *matches, size_t capacity);
/* The data changed: `count` items now. The filter is applied again and the
 * selection stays on the same item index when it is still a row. */
void kt_list_set_count(struct kt_list *list, size_t count);
/* Applies filter text as if typed (empty clears it). */
void kt_list_set_filter(struct kt_list *list, const char *text);
enum kt_list_result kt_list_update(struct kt_list *list, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_list_view(const struct kt_list *list, struct kt_canvas *canvas, struct kt_rect area);
void kt_list_place(struct kt_list *list, struct kt_rect area);
/* Rows shown (items, or matches while filtering, headers included). */
size_t kt_list_rows(const struct kt_list *list);
/* The item index of a row, KT_NONE beyond the end. */
size_t kt_list_row_item(const struct kt_list *list, size_t row);
/* The selected item's index, KT_NONE when nothing is selected. */
size_t kt_list_index(const struct kt_list *list);
/* Selects the row showing item `index`; false when no row shows it. */
bool kt_list_select(struct kt_list *list, size_t index);
uint32_t kt_list_scope(const struct kt_list *list);
struct kt_keymap kt_list_keymap(struct kt_list *list);

#endif
