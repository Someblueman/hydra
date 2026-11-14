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
/* Runs the validation hook now; true when the text is valid. */
bool kt_textinput_validate(struct kt_textinput *input);
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

/* ---- table -------------------------------------------------------------- */

/* A column: its heading, how it shares the width (layout.h constraints) and
 * how its cells align. */
struct kt_table_column { const char *title; struct kt_constraint width; enum kt_align align; };
/* One cell as the callback describes it. `color` KT_DEFAULT is the row's
 * text colour; attrs are added to it. The text is borrowed, as for lists. */
struct kt_table_cell { const char *text; kt_color color; unsigned attrs; };
typedef void (*kt_table_cell_fn)(void *context, size_t row, size_t column, struct kt_table_cell *cell);
#define KT_TABLE_COLUMNS 16

/* Rows of columns from the caller's data, with a header, the list's
 * selection and scrolling, cells cut with an ellipsis where they do not fit,
 * and a sort indicator. Sorting is the application's: 's' (or a click on a
 * heading) asks for the next column or the clicked one, 'S' (or a second
 * click) reverses the order; the table records the choice in sort_column
 * and sort_descending, reports _SORTED, and the application reorders its
 * data. Columns past KT_TABLE_COLUMNS are not shown. */
enum kt_table_action {
    KT_TABLE_UP, KT_TABLE_DOWN, KT_TABLE_PAGE_UP, KT_TABLE_PAGE_DOWN, KT_TABLE_TOP, KT_TABLE_BOTTOM,
    KT_TABLE_CHOOSE, KT_TABLE_SORT, KT_TABLE_REVERSE, KT_TABLE_ACTIONS
};
enum kt_table_result { KT_TABLE_IGNORED, KT_TABLE_HANDLED, KT_TABLE_MOVED, KT_TABLE_CHOSEN, KT_TABLE_SORTED };
enum { KT_TABLE_SCOPE_BROWSE = 1, KT_TABLE_SCOPE_SORTABLE = 2 };
struct kt_table_style {
    struct kt_style header, sort, cell, cell_selected, selection, bar, bar_unfocused, status, empty, scrollbar, thumb;
};
struct kt_table {
    const struct kt_table_column *columns;
    size_t column_count;
    kt_table_cell_fn cell;
    void *context;
    size_t count, selected, top;     /* rows; selected is KT_NONE when empty  */
    size_t sort_column;              /* KT_NONE: unsorted                     */
    bool sort_descending, sortable;
    bool focused, header, status_line, scrollbar;
    int gap;                         /* columns between columns; default 2    */
    const char *empty;
    struct kt_rect area;
    struct kt_table_style style;
    struct kt_binding keys[KT_TABLE_ACTIONS];
    struct kt_keymap_state chord;
};
void kt_table_init(struct kt_table *table, const struct kt_table_column *columns, size_t column_count,
                   kt_table_cell_fn cell, void *context, size_t count);
/* The data changed; the selection is kept in range. */
void kt_table_set_count(struct kt_table *table, size_t count);
enum kt_table_result kt_table_update(struct kt_table *table, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_table_view(const struct kt_table *table, struct kt_canvas *canvas, struct kt_rect area);
void kt_table_place(struct kt_table *table, struct kt_rect area);
/* Where each column is drawn in an area: `parts` holds column_count
 * rectangles (at most KT_TABLE_COLUMNS), the header row's. */
void kt_table_columns(const struct kt_table *table, struct kt_rect area, struct kt_rect *parts);
uint32_t kt_table_scope(const struct kt_table *table);
struct kt_keymap kt_table_keymap(struct kt_table *table);

/* ---- viewport ----------------------------------------------------------- */

/* A line of a viewport, as styled runs: the callback fills at most
 * `capacity` spans and returns how many. Texts are borrowed until the call
 * that asked returns. */
typedef size_t (*kt_viewport_line_fn)(void *context, size_t index, struct kt_span *spans, size_t capacity);
#define KT_VIEWPORT_SPANS 32

/* Scrollable read-only text: lines of styled runs from a callback, or the
 * rows of a canvas the application drew (kt_viewport_set_canvas). Lines are
 * clipped, with left and right scrolling sideways, or wrapped at blanks
 * (`wrap`; a canvas is always clipped). With `follow`, a viewport scrolled to
 * the end stays at the end as lines are added (kt_viewport_set_count), the
 * way a log is read; scrolling up leaves the end and scrolling back to it
 * returns. A scroll bar shows where the view is. Keys: arrows and j/k,
 * pgup/pgdn and b/space, half pages with ctrl+u/ctrl+d, g/G and home/end; the
 * mouse wheel scrolls three rows. */
enum kt_viewport_action {
    KT_VIEWPORT_UP, KT_VIEWPORT_DOWN, KT_VIEWPORT_PAGE_UP, KT_VIEWPORT_PAGE_DOWN, KT_VIEWPORT_HALF_UP,
    KT_VIEWPORT_HALF_DOWN, KT_VIEWPORT_TOP, KT_VIEWPORT_BOTTOM, KT_VIEWPORT_LEFT, KT_VIEWPORT_RIGHT,
    KT_VIEWPORT_ACTIONS
};
enum kt_viewport_result { KT_VIEWPORT_IGNORED, KT_VIEWPORT_HANDLED };
enum { KT_VIEWPORT_SCOPE_SCROLL = 1, KT_VIEWPORT_SCOPE_SIDEWAYS = 2 };
struct kt_viewport_style { struct kt_style scrollbar, thumb; };
struct kt_viewport {
    kt_viewport_line_fn line;
    void *context;
    const struct kt_canvas *source;  /* when set, its rows are the lines      */
    size_t count;                    /* lines                                 */
    size_t top;                      /* first line shown                      */
    int top_row;                     /* rows of it scrolled off, when wrapped */
    int left;                        /* columns scrolled off, when clipped    */
    bool wrap, follow, scrollbar;
    bool at_end;                     /* following the end now                 */
    struct kt_rect area;
    struct kt_viewport_style style;
    struct kt_binding keys[KT_VIEWPORT_ACTIONS];
    struct kt_keymap_state chord;
};
void kt_viewport_init(struct kt_viewport *viewport, kt_viewport_line_fn line, void *context, size_t count);
/* Shows the rows of `source` instead; NULL goes back to the callback. */
void kt_viewport_set_canvas(struct kt_viewport *viewport, const struct kt_canvas *source);
/* The text changed: `count` lines now. Following, the end stays in view. */
void kt_viewport_set_count(struct kt_viewport *viewport, size_t count);
enum kt_viewport_result kt_viewport_update(struct kt_viewport *viewport, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_viewport_view(const struct kt_viewport *viewport, struct kt_canvas *canvas, struct kt_rect area);
void kt_viewport_place(struct kt_viewport *viewport, struct kt_rect area);
/* Scrolls by rows (negative up), to the top, or to the end. */
void kt_viewport_scroll(struct kt_viewport *viewport, long rows);
void kt_viewport_top(struct kt_viewport *viewport);
void kt_viewport_bottom(struct kt_viewport *viewport);
bool kt_viewport_at_bottom(const struct kt_viewport *viewport);
/* Rows a line takes at a width: 1 clipped, its wrapped rows otherwise. */
int kt_viewport_line_rows(const struct kt_viewport *viewport, size_t index, int width);
/* How far down the view is, 0 to 100, by lines. */
int kt_viewport_percent(const struct kt_viewport *viewport);
uint32_t kt_viewport_scope(const struct kt_viewport *viewport);
struct kt_keymap kt_viewport_keymap(struct kt_viewport *viewport);

/* ---- confirm ------------------------------------------------------------ */

/* A question in a dialog panel: a title in its top edge, a body wrapped to
 * its width, buttons at the bottom right and key hints in its bottom edge.
 * y accepts and n or Esc cancels whatever button has the focus; left and
 * right (tab, shift+tab) move the focus and Enter or space presses the
 * focused button; a click presses the button under it. A destructive
 * question draws its border and its accepting button in the danger colour.
 * Buttons are the caller's strings: the first two are cancel and accept as
 * set by init, and a third may be added for another way out (_CHOSEN). */
#define KT_CONFIRM_BUTTONS 3
enum kt_confirm_action { KT_CONFIRM_ACCEPT, KT_CONFIRM_CANCEL, KT_CONFIRM_PREVIOUS, KT_CONFIRM_NEXT,
                         KT_CONFIRM_PRESS, KT_CONFIRM_ACTIONS };
enum kt_confirm_result {
    KT_CONFIRM_IGNORED, KT_CONFIRM_HANDLED,
    KT_CONFIRM_ACCEPTED, KT_CONFIRM_CANCELLED,
    KT_CONFIRM_CHOSEN          /* another button: `chosen` says which         */
};
struct kt_confirm_style {
    struct kt_style border, border_destructive, title, body, button, button_focused, button_destructive;
    struct kt_help_style hints;
};
struct kt_confirm {
    const char *title, *body;
    const char *buttons[KT_CONFIRM_BUTTONS];
    size_t button_count;
    size_t accept, cancel;     /* the buttons y and n press                    */
    size_t focus;              /* the button Enter presses                     */
    size_t chosen;             /* the button pressed last                      */
    bool destructive;
    struct kt_rect area;
    struct kt_confirm_style style;
    struct kt_binding keys[KT_CONFIRM_ACTIONS];
    struct kt_keymap_state chord;
};
/* Buttons "cancel" then "accept", the focus on accept. The hint for y reads
 * the accepting button's label in lower case ("y remove"). */
void kt_confirm_init(struct kt_confirm *confirm, const char *title, const char *body, const char *accept,
                     const char *cancel, bool destructive);
enum kt_confirm_result kt_confirm_update(struct kt_confirm *confirm, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_confirm_view(const struct kt_confirm *confirm, struct kt_canvas *canvas, struct kt_rect area);
void kt_confirm_place(struct kt_confirm *confirm, struct kt_rect area);
/* Rows the dialog needs at a width, its border included. */
int kt_confirm_height(const struct kt_confirm *confirm, int width);
uint32_t kt_confirm_scope(const struct kt_confirm *confirm);
struct kt_keymap kt_confirm_keymap(struct kt_confirm *confirm);

/* ---- form --------------------------------------------------------------- */

/* A sequence of labelled fields: text (a caller's struct kt_textinput),
 * choice (one of the caller's options) and toggle. Tab and down move to the
 * next field, shift+tab and up back; left and right change a choice, space
 * or left and right flip a toggle; Enter submits and Esc cancels. Each field
 * shows its hint, or its error in its place. Submitting checks every text
 * field's validation and then the form's own, focuses the first field with
 * an error and reports _INVALID; otherwise _SUBMITTED. With a title the form
 * draws itself as a dialog panel, as kt_confirm does. */
#define KT_FIELD_ERROR 96
enum kt_field_kind { KT_FIELD_TEXT, KT_FIELD_CHOICE, KT_FIELD_TOGGLE };
struct kt_field {
    enum kt_field_kind kind;
    const char *label, *hint;
    struct kt_textinput *input;            /* TEXT: caller-owned, placed by the form  */
    const char *const *options;            /* CHOICE: the options; TOGGLE: off, on    */
    size_t option_count, choice;
    bool on;                               /* TOGGLE                                  */
    char error[KT_FIELD_ERROR];            /* set by validation or the application    */
};
enum kt_form_action { KT_FORM_NEXT, KT_FORM_PREVIOUS, KT_FORM_SUBMIT, KT_FORM_CANCEL, KT_FORM_LEFT, KT_FORM_RIGHT,
                      KT_FORM_FLIP, KT_FORM_ACTIONS };
enum kt_form_result {
    KT_FORM_IGNORED, KT_FORM_HANDLED,
    KT_FORM_CHANGED,      /* a field's value changed                          */
    KT_FORM_SUBMITTED, KT_FORM_INVALID, KT_FORM_CANCELLED
};
enum { KT_FORM_SCOPE_FIELDS = 1, KT_FORM_SCOPE_CHOICE = 2, KT_FORM_SCOPE_TOGGLE = 4 };
struct kt_form;
/* Cross-field validation: set fields' errors and return false to refuse. */
typedef bool (*kt_form_validate_fn)(void *context, struct kt_form *form);
struct kt_form_style {
    struct kt_style border, title, label, label_focused, hint, error, option, option_selected, field, prompt,
        prompt_focused;
    struct kt_help_style hints;
};
struct kt_form {
    const char *title;            /* NULL: fields only, without a panel       */
    struct kt_field *fields;
    size_t count, focus;
    kt_form_validate_fn validate;
    void *validate_context;
    struct kt_rect area;
    struct kt_form_style style;
    struct kt_binding keys[KT_FORM_ACTIONS];
    struct kt_keymap_state chord;
};
/* Focuses the first field; the fields stay the caller's. */
void kt_form_init(struct kt_form *form, const char *title, struct kt_field *fields, size_t count);
enum kt_form_result kt_form_update(struct kt_form *form, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_form_view(const struct kt_form *form, struct kt_canvas *canvas, struct kt_rect area);
void kt_form_place(struct kt_form *form, struct kt_rect area);
/* Rows the form needs at a width, its panel included when it has a title. */
int kt_form_height(const struct kt_form *form, int width);
/* Runs every check as submitting does; true when nothing has an error. */
bool kt_form_check(struct kt_form *form);
void kt_form_focus(struct kt_form *form, size_t field);
uint32_t kt_form_scope(const struct kt_form *form);
struct kt_keymap kt_form_keymap(struct kt_form *form);

/* ---- tabs --------------------------------------------------------------- */

/* A tab bar: the active tab filled with the accent, the others muted, an
 * optional badge after a label ("Attention 3"). When the tabs do not fit,
 * the inactive ones lose their padding, and then only those around the
 * active tab are shown, with an arrow at each side that hides some. tab and
 * ']' select the next tab, shift+tab and '[' the previous, 1 to 9 a tab by
 * number; a click selects the tab under it. */
struct kt_tab { const char *label, *badge; kt_color badge_color; };
enum kt_tabs_action { KT_TABS_NEXT, KT_TABS_PREVIOUS, KT_TABS_JUMP, KT_TABS_ACTIONS = KT_TABS_JUMP + 9 };
enum kt_tabs_result { KT_TABS_IGNORED, KT_TABS_HANDLED, KT_TABS_CHANGED };
struct kt_tabs_style { struct kt_style active, inactive, badge, badge_active, overflow; };
struct kt_tabs {
    const struct kt_tab *tabs;
    size_t count, active;
    struct kt_rect area;
    struct kt_tabs_style style;
    struct kt_binding keys[KT_TABS_ACTIONS];
    struct kt_keymap_state chord;
};
void kt_tabs_init(struct kt_tabs *tabs, const struct kt_tab *items, size_t count);
enum kt_tabs_result kt_tabs_update(struct kt_tabs *tabs, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_tabs_view(const struct kt_tabs *tabs, struct kt_canvas *canvas, struct kt_rect area);
void kt_tabs_place(struct kt_tabs *tabs, struct kt_rect area);
/* Where tab `index` is drawn in an area; an empty rectangle when hidden. */
struct kt_rect kt_tabs_rect(const struct kt_tabs *tabs, struct kt_rect area, size_t index);
uint32_t kt_tabs_scope(const struct kt_tabs *tabs);
struct kt_keymap kt_tabs_keymap(struct kt_tabs *tabs);

/* ---- spinner, progress, toasts ------------------------------------------ */

/* A spinner ticks only while it is running and shown: kt_spinner_start and
 * kt_spinner_stop say whether work is going on, kt_spinner_show whether the
 * view draws it. Its repeating timer exists exactly while both hold, so a
 * stopped or hidden spinner costs no wake-ups. */
struct kt_spinner {
    const char *const *frames;       /* Unicode frames; default braille dots   */
    size_t frame_count;
    const char *const *ascii_frames; /* for ASCII canvases; default | / - \   */
    size_t ascii_count;
    size_t frame;
    unsigned interval_ms;            /* default 80                             */
    uint64_t tag, timer;
    bool running, visible;
    const char *label;               /* drawn after the frame                  */
    struct kt_style style, label_style;
};
enum kt_spinner_result { KT_SPINNER_IGNORED, KT_SPINNER_HANDLED };
/* Visible and stopped. `tag` marks its timer messages. */
void kt_spinner_init(struct kt_spinner *spinner, uint64_t tag);
void kt_spinner_start(struct kt_spinner *spinner, struct kt_cmds *cmds);
void kt_spinner_stop(struct kt_spinner *spinner, struct kt_cmds *cmds);
void kt_spinner_show(struct kt_spinner *spinner, bool visible, struct kt_cmds *cmds);
/* Advances on its own timer; everything else is ignored. */
enum kt_spinner_result kt_spinner_update(struct kt_spinner *spinner, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_spinner_view(const struct kt_spinner *spinner, struct kt_canvas *canvas, struct kt_rect area);

/* A determinate progress bar: eighths of a cell with block characters, whole
 * cells of '#' on an ASCII canvas, an optional percentage after it. It has
 * no input and no timer: the application sets the value. */
struct kt_progress { double value; bool percent; struct kt_style fill, track, label; };
void kt_progress_init(struct kt_progress *progress);
/* Clamped to 0..1. */
void kt_progress_set(struct kt_progress *progress, double value);
void kt_progress_view(const struct kt_progress *progress, struct kt_canvas *canvas, struct kt_rect area);

/* Notifications that stack in a corner and go away by themselves. Each one
 * has its own one-shot timer for its timeout, so nothing ticks while they
 * wait and nothing at all when there are none. At most `limit` (up to
 * KT_TOASTS_MAX) show; a new one beyond that replaces the oldest. A click
 * dismisses the one under it, toast.dismiss (unbound by default) the newest,
 * as does kt_toasts_dismiss. Text is copied, cut at KT_TOAST_TEXT bytes. */
#define KT_TOASTS_MAX 4
#define KT_TOAST_TEXT 120
enum kt_toast_kind { KT_TOAST_INFO, KT_TOAST_SUCCESS, KT_TOAST_WARNING, KT_TOAST_ERROR };
struct kt_toast { char text[KT_TOAST_TEXT]; enum kt_toast_kind kind; uint64_t timer; };
enum kt_toasts_action { KT_TOASTS_DISMISS, KT_TOASTS_ACTIONS };
enum kt_toasts_result { KT_TOASTS_IGNORED, KT_TOASTS_HANDLED, KT_TOASTS_EXPIRED, KT_TOASTS_DISMISSED };
struct kt_toasts_style { struct kt_style body, info, success, warning, error; };
struct kt_toasts {
    struct kt_toast items[KT_TOASTS_MAX];   /* oldest first                     */
    size_t count, limit;                    /* limit default 3                  */
    unsigned timeout_ms;                    /* default 4000                     */
    int width;                              /* most columns per toast; 48       */
    uint64_t tag;
    struct kt_rect area;
    struct kt_toasts_style style;
    struct kt_binding keys[KT_TOASTS_ACTIONS];
    struct kt_keymap_state chord;
};
void kt_toasts_init(struct kt_toasts *toasts, uint64_t tag);
void kt_toasts_push(struct kt_toasts *toasts, struct kt_cmds *cmds, enum kt_toast_kind kind, const char *text);
/* Removes the newest, or every one; their timers are cancelled. */
void kt_toasts_dismiss(struct kt_toasts *toasts, struct kt_cmds *cmds);
void kt_toasts_clear(struct kt_toasts *toasts, struct kt_cmds *cmds);
enum kt_toasts_result kt_toasts_update(struct kt_toasts *toasts, const struct kt_msg *msg, struct kt_cmds *cmds);
/* Stacked in the bottom-right corner of the area, newest lowest. */
void kt_toasts_view(const struct kt_toasts *toasts, struct kt_canvas *canvas, struct kt_rect area);
void kt_toasts_place(struct kt_toasts *toasts, struct kt_rect area);
struct kt_keymap kt_toasts_keymap(struct kt_toasts *toasts);

/* ---- command palette ---------------------------------------------------- */

/* A dialog listing the application's bindings, filtered as one types
 * (kt_fuzzy over the label and the name, matches highlighted, best first),
 * each with its keys. Up and down (ctrl+p, ctrl+n) move, Enter chooses and
 * Esc cancels; a click chooses. kt_palette_chosen then gives the binding,
 * whose action the application performs as if its keys had been pressed.
 * Bindings with a label are listed, with or without keys. */
#define KT_PALETTE_MAX 128
enum kt_palette_action { KT_PALETTE_UP, KT_PALETTE_DOWN, KT_PALETTE_CHOOSE, KT_PALETTE_CANCEL, KT_PALETTE_ACTIONS };
enum kt_palette_result { KT_PALETTE_IGNORED, KT_PALETTE_HANDLED, KT_PALETTE_CHOSEN, KT_PALETTE_CANCELLED };
struct kt_palette_style {
    struct kt_style border, title, label, name, keys, match, selection, bar, empty;
    struct kt_help_style hints;
};
struct kt_palette {
    const char *title;
    struct kt_help_source sources[KT_HELP_SOURCES];
    size_t source_count;
    struct kt_textinput query;
    struct kt_match matches[KT_PALETTE_MAX];   /* index: position among listed bindings */
    size_t match_count, selected, top;
    int rows;                                  /* rows of results shown; default 10      */
    const struct kt_binding *chosen;
    struct kt_rect area;
    struct kt_palette_style style;
    struct kt_binding keys[KT_PALETTE_ACTIONS];
    struct kt_keymap_state chord;
};
void kt_palette_init(struct kt_palette *palette, const char *title);
/* What it lists, as for kt_help_set; also empties the query. */
bool kt_palette_set(struct kt_palette *palette, const struct kt_help_source *sources, size_t count);
enum kt_palette_result kt_palette_update(struct kt_palette *palette, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_palette_view(const struct kt_palette *palette, struct kt_canvas *canvas, struct kt_rect area);
void kt_palette_place(struct kt_palette *palette, struct kt_rect area);
int kt_palette_height(const struct kt_palette *palette, int width);
const struct kt_binding *kt_palette_chosen(const struct kt_palette *palette);
uint32_t kt_palette_scope(const struct kt_palette *palette);
struct kt_keymap kt_palette_keymap(struct kt_palette *palette);

/* ---- modal stack -------------------------------------------------------- */

/* Dialogs over the application's view. The view draws the base and then the
 * stack: the base is restyled with `backdrop` (faint text by default, which
 * every colour profile can show, dim where there is no colour), and each
 * modal is drawn centred over it, the top one last. Input (keys, mouse,
 * pastes) goes to the top modal only; Esc that it leaves unhandled closes
 * it. Other messages are offered to the top modal and, when it ignores them,
 * reported as ignored so the application's own handling runs. A modal's
 * outcome comes back as _RESULT with the modal's id and its component's
 * result; the application decides what follows, usually kt_modals_pop.
 *
 * A modal is a component seen through three functions; kt_<name>_modal
 * makes one for the confirm, the form, the palette and the help screen. */
#define KT_MODALS_MAX 4
typedef int (*kt_modal_update_fn)(void *component, const struct kt_msg *msg, struct kt_cmds *cmds);
typedef void (*kt_modal_view_fn)(const void *component, struct kt_canvas *canvas, struct kt_rect area);
typedef void (*kt_modal_place_fn)(void *component, struct kt_rect area);
typedef int (*kt_modal_height_fn)(const void *component, int width);
struct kt_modal {
    uint32_t id;                  /* the application's name for it             */
    void *component;
    kt_modal_update_fn update;    /* returns the component's result            */
    kt_modal_view_fn view;
    kt_modal_place_fn place;      /* may be NULL                               */
    kt_modal_height_fn measure;   /* rows at a width; NULL: `height`           */
    int width, height;            /* preferred size; cut to the area           */
};
enum kt_modals_result {
    KT_MODALS_IGNORED,    /* no modal, or a message the top one left alone     */
    KT_MODALS_HANDLED,    /* taken by the top modal                            */
    KT_MODALS_CLOSED,     /* Esc closed the top modal: `id` was its id         */
    KT_MODALS_RESULT      /* the top modal had an outcome: `id`, `result`      */
};
struct kt_modals {
    struct kt_modal stack[KT_MODALS_MAX];
    size_t count;
    struct kt_rect area;
    struct kt_style backdrop;
    uint32_t id;
    int result;
};
void kt_modals_init(struct kt_modals *modals);
/* False when the stack is full. The modal is placed at once. */
bool kt_modals_push(struct kt_modals *modals, const struct kt_modal *modal);
void kt_modals_pop(struct kt_modals *modals);
const struct kt_modal *kt_modals_top(const struct kt_modals *modals);
enum kt_modals_result kt_modals_update(struct kt_modals *modals, const struct kt_msg *msg, struct kt_cmds *cmds);
void kt_modals_view(const struct kt_modals *modals, struct kt_canvas *canvas, struct kt_rect area);
/* The area modals are centred in; every modal is placed again. */
void kt_modals_place(struct kt_modals *modals, struct kt_rect area);
/* Where modal `index` sits in an area. */
struct kt_rect kt_modals_rect(const struct kt_modals *modals, size_t index, struct kt_rect area);
struct kt_modal kt_confirm_modal(struct kt_confirm *confirm, uint32_t id, int width);
struct kt_modal kt_form_modal(struct kt_form *form, uint32_t id, int width);
struct kt_modal kt_palette_modal(struct kt_palette *palette, uint32_t id, int width);
/* The full help in a panel titled "Keyboard"; its toggle and Esc report
 * KT_HELP_CLOSED as the result. Width 0 fits two columns. */
struct kt_modal kt_help_modal(struct kt_help *help, uint32_t id, int width);

#endif
