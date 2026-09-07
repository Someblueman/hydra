# termviz

Dependency-free C99 terminal infrastructure, developed with Hydra as its first
consumer. The portable core provides width-aware cell surfaces, incremental ANSI
presentation, nested pane layout, focus and scrolling, a navigation tree, charts,
DAG drawing, a bounded input decoder, and an embedded terminal screen/parser.
Optional POSIX adapters provide outer-terminal lifecycle and one child PTY.

This is an internal API under development, not a published stable library.
Headers document ownership and limits. The core does not allocate, read the clock,
open files, start processes, or change terminal modes. Callers own storage and the
event loop; POSIX-specific behavior is confined to `terminal_posix.c` and
`pty_posix.c`. No external rendering, Unicode, event-loop, or PTY library is used.

## Run the standalone examples

From the repository root:

```sh
make example-termviz example-workspace
build/termviz-example
build/termviz-workspace
build/termviz-workspace --shell
```

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
cc -std=c99 -Wall -Wextra -Werror -pedantic -Isrc/termviz \
  examples/workspace.c examples/workspace_view.c examples/workspace_input.c \
  src/termviz/*.c -o /tmp/termviz-workspace
/tmp/termviz-workspace --shell
```

For a complete independently buildable source tree, run from Hydra's root:

```sh
scripts/package-termviz.sh /tmp/termviz-source-new
make -C /tmp/termviz-source-new all test test-pty
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

`make test-termviz` exercises components. `make test-workspace-pty` uses the
repository's existing Python 3 test toolchain, standard library only, to interact
with actual PTYs and reconstruct their displayed cells independently of the C
parser. `make sanitize-workspace` runs both under the supported sanitizer setup.
These test tools are not library or demo runtime dependencies.

## Boundaries and behavior

* `termviz.h`: caller-owned cells, borrowed clipped surfaces, semantic or explicit
  RGB styles, text, panels, bars, plots, graphs and presentation. Unchanged frames
  emit zero bytes. A changed run emits cursor positioning and changed cells;
  resizing or explicit invalidation repaints. Invalidate after outside writers,
  theme changes, terminal suspension, or screen changes. Reserve the last outer
  terminal column to avoid automatic wrapping.
* `workspace.h`: stable pane IDs, nested column/row splits, minimum sizes, focus,
  drag routing and independent scroll state. Insufficient space shows the focused
  subtree; focus cycling still reaches hidden leaves. No app content is owned.
* `input.h` and `tree.h`: bounded incremental key/mouse/paste decoding and a
  preorder navigation tree. The application maps events to domain actions.
* `terminal.h`: bounded child-output interpretation and screen/history storage.
  [TERMINAL.md](TERMINAL.md) specifies the supported protocol subset and limits.
* `posix.h` and `pty_posix.h`: optional adapters. The app owns signal policy and
  cleanup. Closing the demo hangs up the child terminal and reaps its shell;
  it is not a sandbox for intentionally daemonized processes.

Hydra-specific aggregation, clocks, identities, workflow/agent semantics, and
mutation policy stay in Hydra. Its workspace uses the same layout/tree/presenter
and uses the optional PTY adapter for attachment clients. Existing head shells
and agents remain owned by tmux, independently of those clients.

## Text and sizing limits

Unicode widths use checked-in Unicode 17.0.0 East Asian Width and General Category
data. W/F codepoints take two cells; ambiguous characters take one; up to three
Mn/Me combining marks attach to a base. Invalid UTF-8 and control/format characters
become `?`. ASCII mode replaces non-ASCII scalars. This is terminal cell layout,
not general grapheme shaping: joined emoji, bidirectional shaping and font-specific
ligatures are not implemented. Terminal/font width disagreement remains possible.
See the Unicode data license and provenance in `unicode_tables.inc`.

Canvas dimensions are bounded to 4096x4096 and available caller storage; demos and
Hydra workspace cap at 512x256. Layout allows 32 nodes and trees 2048 preorder
nodes. DAGs allow 128 nodes/512 edges; fixed-size layered layout does not minimize
crossings. Plot samples are equally spaced; the caller labels units and sampling
domain. Missing observations and zero observations remain distinct.
