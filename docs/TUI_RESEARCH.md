# Native visualization research

Research checked 7 September 2026. This is a design comparison, not a claim that
Hydra has matched every application below. Implementation and acceptance remain
separate from the capabilities advertised by upstream projects.

## What modern terminal interfaces can do

| Reference | Relevant capabilities | Application to Hydra |
| --- | --- | --- |
| [btop](https://github.com/aristocratos/btop) (C++) | Resource graphs, process trees, filtering, sorting, mouse input, detail views, selectable graph symbols | Keep overview, selection, and detail visible together; use charts for patterns and numbers for precise inspection. |
| [Ratatui widgets](https://ratatui.rs/concepts/widgets/) (Rust) | Composable panels, tables, charts, and a drawing canvas | Give each visualization a bounded rectangular region and keep application state outside drawing code. |
| [Textual widget gallery](https://textual.textualize.io/widget_gallery/) (Python) | A broad collection of interactive terminal widgets | Make navigation and focus discoverable; reveal details without turning the overview into a diagnostic dump. |

btop documents Braille, block, and TTY graph choices, along with truecolor,
256-color, and 16-color modes. Its font and wide-character requirements matter:
more graphical resolution is useful only when the display remains legible.
Hydra therefore needs an explicit ASCII mode as well as generated Unicode
graphics, and status words must remain meaningful with color disabled.

## C rendering options

| Option | Evidence | Decision for this implementation |
| --- | --- | --- |
| [Notcurses](https://github.com/dankamongmen/notcurses) | C API with composited planes, Unicode, colors, and terminal image protocols. A core-only build avoids the multimedia stack. | Strong candidate when image composition is a product requirement. Introducing its terminal/input lifecycle now would replace substantial tested Hydra code. |
| [Notcurses plots](https://notcurses.com/notcurses_plot.3.html) | Moving bounded sample windows, explicit or automatic domains, multiple cell subdivisions | Adopt bounded history and honest domains. Keep invalid observations as gaps and do not imply a time series from a single snapshot. |
| [libtickit](https://www.leonerd.org.uk/code/libtickit/) | C window hierarchy, overlapping regions, Unicode width handling, line art, keyboard/mouse events, synchronous or asynchronous operation | Attractive for a broader windowing system. Hydra's immediate need is domain visualization within an existing event loop. |
| [ncurses](https://invisible-island.net/ncurses/ncurses.html) | Established C terminal library with resizing, color, forms, and panels | Remains a viable terminal backend, but does not itself supply workflow layout or telemetry semantics. |
| A small C cell canvas | Existing Hydra terminal ownership, input handling, and bounded subprocess boundary can remain in place | Implement panels, gauges, plots, and DAG layout as an independent module; integrate that module with Hydra rather than introducing a second application framework. |

This choice is specific to the current requirements. It does not claim a custom
canvas replaces the Unicode shaping, terminal negotiation, multimedia, or input
support of the established libraries. Current text cells deliberately sanitize
untrusted non-ASCII bytes; generated graphical glyphs use a separate restricted
alphabet. General multilingual text layout is a future capability requiring a
proper width/grapheme strategy, not a byte-counting patch.

[Kitty's graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
provides a route to terminal bitmap graphics. It is not the baseline for this
work: dependency diagrams and operational charts should work through ordinary
SSH/tmux text terminals. A future image backend should be capability-tested and
retain equivalent textual information.

## Data and interaction decisions

* Workflow arrows must come from recorded dependency edges. Group membership,
  desired state, and task ordering must never be presented as execution evidence.
* Remote desired state is distinct from observed process liveness. Missing CPU,
  memory, and load measurements must be displayed as unavailable.
* Charts identify units, range, and sampling domain. Repaints must not create new
  observations. Failed refreshes create gaps and leave the last valid snapshot
  explicitly stale.
* Dense layouts pair overview and selected details on wide terminals. Narrow
  terminals prioritize readable rows, navigation, and the selected item.
* Graph navigation must reach nodes outside the viewport. Layout rejects cycles
  and invalid endpoints rather than inventing an acyclic interpretation.

These decisions are implemented in the native views and the independent C99
module. See [verification evidence](VISUALIZATION.md#qualification-on-2026-09-07)
for adapter integration, PTY input/resize/restoration checks, rendered inspection,
and the limits of the local qualification.
