# kettle

Dependency-free C99 terminal infrastructure, developed with Hydra as its first
consumer. The portable core provides width-aware cell surfaces, incremental ANSI
presentation, nested pane layout, focus and scrolling, a navigation tree, charts,
DAG drawing, a bounded input decoder, and an embedded terminal screen/parser.
On top of it, an optional POSIX runtime turns three functions (init, update,
view) into a terminal application: it owns the event loop, terminal modes,
signals, child processes and output. A second optional adapter runs one child
PTY.

This is an internal API under development, not a published stable library.
Headers document ownership and limits. The core does not allocate, read the clock,
open files, start processes, or change terminal modes; its callers own storage.
Everything that touches the operating system is in the optional layer:
`program*.c` (the runtime), `testing.c` (its headless test driver),
`pty_posix.c` and `terminal_posix.c`. No external rendering, Unicode, event-loop,
or PTY library is used.

## Writing an application

An application is a model and three functions. [`examples/counter.c`](../../examples/counter.c)
is a complete one in about a hundred lines; this is its shape:

```c
struct model { int count; unsigned seconds; char date[64]; bool asking; };

static void init(void *model, struct kt_cmds *cmds) {
    kt_cmd_timer(cmds, TICK, 1000, 1000);   /* a KT_MSG_TIMER every second */
    ask_date(model, cmds);                  /* kt_cmd_spawn: a child beside the loop */
}

static bool update(void *model, const struct kt_msg *msg, struct kt_cmds *cmds) {
    struct model *m = model;
    switch (msg->type) {
    case KT_MSG_KEY: return on_key(m, &msg->u.event, cmds);   /* 'q' -> kt_cmd_quit */
    case KT_MSG_TIMER: m->seconds++; return true;
    case KT_MSG_PROCESS: on_date(m, &msg->u.process); return true;
    default: return false;
    }
}

static void view(const void *model, struct kt_frame *frame) {
    struct kt_canvas *canvas = kt_frame_canvas(frame);
    /* kt_panel, kt_text, ... : draw the whole screen from the model */
}

int main(void) {
    static const struct kt_app counter = {init, update, view, style};
    struct model model = {0};
    struct kt_options options = {0};
    options.alt_screen = true;
    struct kt_program *program = kt_program_new(&counter, &model, &options);
    int status = kt_program_run(program);   /* returns with the terminal restored */
    kt_program_free(program);
    return status;
}
```

* **The model** is ordinary data that the application owns. `update` is the only
  place it changes, one message at a time: a key, a mouse report, a whole paste,
  a new terminal size, a timer, a ready descriptor, a finished child, a signal.
  `update` returns whether it handled the message; an unhandled SIGINT, SIGTERM,
  SIGHUP or SIGQUIT quits with status 128 + signal.
* **Effects are commands.** `update` never sleeps, forks or reads a descriptor.
  It queues a request and gets a message when there is news:
  `kt_cmd_timer` (once or repeating), `kt_cmd_watch` (a PTY, socket or pipe joins
  the loop's `poll`), `kt_cmd_spawn` (a child in its own session, output captured,
  time budget enforced, exactly one `KT_MSG_PROCESS`), `kt_cmd_exec` (lend the
  terminal to an editor), `kt_cmd_suspend`, `kt_cmd_post`, `kt_cmd_cancel`,
  `kt_cmd_repaint`, `kt_cmd_quit`. Each takes a `tag` that comes back in the
  message, so a reply finds its request.
* **`view` draws everything, every time,** into the frame's canvas. It must not
  change the model. The runtime compares the frame with what the terminal shows
  and writes only the cells that differ. Cursor position, mouse reporting and
  window title are part of the frame (`kt_frame_cursor`, `kt_frame_mouse`,
  `kt_frame_title`); the runtime emits the changes and undoes them on exit.
* **`style`** maps a cell's semantic style to SGR bytes on the stream it is given.

What the runtime guarantees, so applications cannot get it wrong:

* An idle program is blocked in `poll`; nothing wakes it periodically. Watched
  descriptors and child pipes are in the poll set, so their data is handled the
  moment it arrives, not on a tick.
* Every message from one wake-up goes through `update` before anything is
  drawn: a burst of keys or a 4 KB read costs one frame. `view` runs only after
  a message, and frames that change the screen are at least one frame interval
  apart (`fps`, default 60). A frame that changes nothing writes nothing and
  does not delay the next one.
* A frame (mode changes, changed cells, cursor) leaves in one `write`, wrapped in
  a synchronized update (DEC mode 2026) unless disabled. A full repaint (new
  size, `kt_cmd_repaint`) overwrites every cell and never erases the display;
  the screen is cleared once, when the program takes it over.
* The canvas is always exactly the terminal's size (SIGWINCH is handled; a
  `KT_MSG_RESIZE` precedes the first frame) and every column is usable.
* The terminal settings, descriptor flags and signal dispositions are restored
  on every way out: `kt_cmd_quit`, a signal, `exit()` and fatal signals
  (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, which then proceed as usual).
* A spawned child that outlives its budget, a `kt_cmd_cancel` or the program
  gets SIGTERM for its process group, then SIGKILL for its session after a grace
  period, and is reaped.

Things to keep in mind: `update` should return quickly, because nothing else
runs meanwhile. Pointers inside a message (paste bytes, process output) are
valid until `update` returns. A watch is level-triggered: read what it reports
or cancel it. Another thread or a signal handler talks to the program only
through `kt_program_post`. Code that must keep its own loop for now can drive
the runtime with `kt_program_begin`, `_next`, `_dispatch`, `_render` and `_end`
instead of `kt_program_run`. A helper called from `update` that cannot return
before it has an answer (a prompt, a confirmation) may run the loop the same
way from inside `update`: it keeps the messages it wants, passes the rest to
`kt_program_dispatch`, and `view` draws its question as part of the frame.
`kt_program_release` and `kt_program_reclaim` lend the terminal for a hand-off
that `update` must finish itself. [program.h](program.h) is the specification.

### Testing an application

[testing.h](testing.h) runs the same `kt_app` through the same loop with no
terminal: a fixed size, a clock that moves only when the test moves it, input
fed as raw bytes through the real decoder, and `kt_cmd_spawn` requests recorded
for the test to answer. [`examples/counter_test.c`](../../examples/counter_test.c)
tests the counter this way:

```c
struct model model = {0};
struct kt_test *test = kt_test_new(&counter, &model, 60, 8, NULL);

const struct kt_test_process *date = kt_test_process(test, 0);   /* what init spawned */
assert(!strcmp(date->spawn.argv[0], "date"));
kt_test_exit(test, date->id, 0, "Tue Nov 10 12:00:00 UTC 2026\n");
assert(kt_test_contains(test, "Date     Tue Nov 10"));           /* the drawn frame, as text */

kt_test_type(test, "++\033[A-");                                  /* an arrow is its escape sequence */
assert(model.count == 2);

kt_test_advance(test, 3500);                                      /* three timer ticks, instantly */
assert(model.seconds == 3);

kt_test_type(test, "q");
assert(!kt_test_running(test) && kt_test_status(test) == 0);
kt_test_free(test);
```

Each call that feeds the program also runs it to rest, as one wake-up of the
real loop would. `kt_test_row` and `kt_test_screen` return the last frame as
text, `kt_test_output` the exact bytes that frame sent to the terminal, and
`kt_test_sleep` how long the loop would now block.

## Run the standalone examples

From the repository root:

```sh
make example-kettle example-counter example-workspace
build/kettle-example
build/kettle-counter
build/kettle-workspace
build/kettle-workspace --shell
```

`kettle-example` prints one static frame with the portable core alone.
`kettle-counter` and `kettle-workspace` are runtime applications.

The workspace without a shell uses explicitly synthetic content. Tab changes pane
focus; `j/k` and arrows navigate/scroll; `h/l` collapse/expand the tree; the mouse
selects and drags dividers. `q` exits. Both content panes retain independent scroll
positions. At small sizes, Tab can reveal panes hidden by minimum-size constraints.

With `--shell`, one pane owns a real `/bin/sh -i`. Ordinary input, including Tab,
Ctrl-C and `q`, reaches that shell when focused. **Ctrl-B then Tab** changes focus;
**Ctrl-B then q** exits the demo and closes its child. Ctrl-B then `[` enters
scrollback, `j/k` scroll, and Ctrl-B then `]` returns to live output. Ctrl-B twice
sends a literal Ctrl-B. `--command PROGRAM ARGS...` runs an explicit alternative
child without command-string interpolation. The inspector shows real child state,
terminal dimensions and history usage. The demo creates no Hydra head or state.

## Standalone build and extraction

The demo needs a POSIX system, a C99 compiler, and an ANSI/UTF-8 terminal:

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -Isrc/kettle \
  examples/workspace.c examples/workspace_view.c examples/workspace_input.c \
  src/kettle/*.c -o /tmp/kettle-workspace
/tmp/kettle-workspace --shell
```

For a complete independently buildable source tree, run from Hydra's root:

```sh
scripts/package-kettle.sh /tmp/kettle-source-new
make -C /tmp/kettle-source-new all test test-pty
```

The destination must not exist. The exporter copies sources, examples, tests,
licenses and a standalone Makefile; it does not create a Git repository or
publish. The default standalone target builds a portable C99 static library.
POSIX examples and real-PTY tests are separate targets. See
[standalone ownership and compatibility](STANDALONE.md) for API, process, platform
and license boundaries. The copied tests reuse the existing component and PTY
checks; Hydra integration is excluded from the standalone test target.

The POSIX demo is locally qualified on macOS; other platforms require their own
runtime qualification. Python is used only by the optional PTY test target.

`make test-kettle` exercises components, the runtime under its headless driver
and with real child processes, and the counter example's own test.
`make test-workspace-pty` (which includes `make test-program-pty`) drives a
runtime fixture and the workspace example on actual PTYs and reconstructs their
displayed cells independently of the C parser. `make sanitize-workspace` runs
all of it under the supported sanitizer setup. These test tools are not library
or demo runtime dependencies.

## Boundaries and behavior

* `kettle.h`: caller-owned cells, borrowed clipped surfaces, semantic or explicit
  RGB styles, text, panels, bars, plots, graphs and presentation. Unchanged frames
  emit zero bytes. A changed run emits cursor positioning and changed cells;
  resizing or explicit invalidation repaints. Code that calls `kt_present`
  itself invalidates after outside writers, theme changes, terminal suspension,
  or screen changes, and reserves the last outer terminal column to avoid
  automatic wrapping; under the runtime none of that is the application's
  concern.
* `workspace.h`: stable pane IDs, nested column/row splits, minimum sizes, focus,
  drag routing and independent scroll state. Insufficient space shows the focused
  subtree; focus cycling still reaches hidden leaves. No app content is owned.
* `input.h` and `tree.h`: bounded incremental key/mouse/paste decoding and a
  preorder navigation tree. A key event names the key (a Unicode scalar or a
  `KT_KEY_*` value) and its Shift/Alt/Ctrl modifiers, and always carries the
  exact bytes received, so a terminal passthrough forwards input unchanged.
* `keymap.h`: one binding table for key chords ("ctrl+]", "ctrl+\ x") that
  drives matching, hint text and user configuration. A chord under way is
  visible to the caller; one that matches nothing is consumed whole and
  reported. Configuration text is parsed line by line with errors that name
  the line; conflicting chords are refused. No allocation or file access.
* `terminal.h`: bounded child-output interpretation and screen/history storage.
  [TERMINAL.md](TERMINAL.md) specifies the supported protocol subset and limits.
* `program.h`: the optional POSIX runtime described above. It allocates (frame
  buffers, queues, captured output) through the allocator in `kt_options`, the
  C library's by default. One program holds the terminal at a time.
* `testing.h`: the headless driver for runtime applications.
* `pty_posix.h`: an optional adapter for one child PTY; its descriptor joins the
  loop with `kt_cmd_watch`. The application closes it: closing the demo hangs up
  the child terminal and reaps its shell; it is not a sandbox for intentionally
  daemonized processes. `posix.h` only reports the terminal size, for output
  that is not a program.

Hydra-specific aggregation, clocks, identities, workflow/agent semantics, and
mutation policy stay in Hydra. Its workspace uses the same layout/tree/presenter
and uses the optional PTY adapter for attachment clients. Existing head shells
and agents remain owned by tmux, independently of those clients.

## Text and sizing limits

Unicode widths use checked-in Unicode 17.0.0 East Asian Width and General Category
data. W/F codepoints take two cells; ambiguous characters take one; up to three
joined scalars attach to a base cell. Joining follows tmux 3.5: Mn/Me marks, a ZWJ
and the scalar after it, and emoji skin-tone modifiers attach to the preceding
cell; VS16 after a narrow symbol and a regional-indicator pair make that cell two
columns wide. Invisible format characters (ZWSP, bidirectional controls) are
dropped and a soft hyphen shows as `-`. Invalid UTF-8 and C0/C1 controls become
`?`. ASCII mode replaces non-ASCII scalars with readable approximations (box
drawing keeps its direction, arrows, bullets, quotes and Latin-1 letters map to
ASCII, anything else becomes `?`) in the same columns. This is terminal cell
layout, not general grapheme shaping: sequences longer than three joined scalars
are truncated, and bidirectional shaping and font-specific ligatures are not
implemented. Terminal/font width disagreement remains possible.
See the Unicode data license and provenance in `unicode_tables.inc`.

Canvas dimensions are bounded to 4096x4096 and available caller storage; demos and
Hydra workspace cap at 512x256. Layout allows 32 nodes and trees 2048 preorder
nodes. DAGs allow 128 nodes/512 edges; fixed-size layered layout does not minimize
crossings. Plot samples are equally spaced; the caller labels units and sampling
domain. Missing observations and zero observations remain distinct.
