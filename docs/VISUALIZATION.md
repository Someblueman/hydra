# C visualization architecture

The native overview, workflow graph, and host view use `src/termviz`, an internal
C99 library with a standalone example and explicit extraction boundary. See
[research and decisions](TUI_RESEARCH.md), [usage](NATIVE_TUI.md), and
[the module README](../src/termviz/README.md).

## Separation of responsibilities

`termviz` owns cell drawing, clipping, panels, bars, plots, and layered DAG layout.
The caller owns all memory; the library has no Hydra types, paths, processes,
clock, environment, input loop, or terminal-mode code. Each canvas cell holds a
generated glyph and semantic style. Text never becomes an escape sequence.

Hydra owns data interpretation, snapshot freshness, filtering, selected identities,
input, terminal restoration, and shell-command delegation. Workflow and host
views only navigate; hidden head selections cannot trigger actions from them.
Hit rectangles come from the last painted frame and are invalidated by resize.

The existing local data protocol (`HYDRA_TUI` version 2), fleet data protocol
(`HYDRA_FLEET_TUI` version 1), CLI actions, and durable workflow files remain
unchanged. Two new read projections supply additional information:

* `hydra workflow tui-data`: `HYDRA_WORKFLOW_TUI` version 1; `W` records contain
  run ID, workflow name, and state; `N` records contain run ID, step ID, kind,
  state, attempts, and comma-separated prerequisites (`-` for none); `X` records
  contain an explicit warning. No definition is evaluated or executed.
* `hydra fleet tui-visual-data`: `HYDRA_FLEET_TUI` version 2; existing `F` and `R`
  records plus `T` records containing host alias, `responded`/`failed`, recorded
  head count, and error code (`-` when absent). The original `tui-data` command
  continues to emit version 1 unchanged. The updated UI and fleet helper should
  be built/installed together.

The local snapshot deadline is two seconds; the fleet snapshot deadline is 3.5
seconds. Workflow reads have a separate two-second deadline and run only while
the workflow view is active. Capture limits output to 8 MiB and bounds child exit
even if stdout closes early. Refresh can temporarily delay input; this is bounded
polling, not an asynchronous streaming telemetry implementation.

## Verification and reproduction

```sh
make build-tui build-fleet
make test-termviz test-visualization test-tui test-tui-pty
make sanitize-tui
make test-all
make example-termviz
build/termviz-example
```

`test-visualization` creates a disposable project, executes a real four-step
workflow, checks that its read projection leaves state unchanged, and passes its
records into the graph renderer at three terminal sizes. It rejects corrupted
graphs and excessive dimensions. Evidence is written beneath
`build/visualization-evidence`; the temporary project and head are removed.
Fleet tests use a controlled SSH transport with the real C server to check empty
hosts, partial failures, and preservation of the original data protocol.

PTY tests cover default overview, selected-panel isolation, graph clicks, keyboard
selection, run switching, panning/refocus, 40-column fallback, and exact terminal
restoration alongside existing actions, themes, signals, and crash behavior.
Component tests cover clipping, untrusted text, numeric bounds, missing samples,
dependency validation, and a 128-node graph. On macOS the repository's supported
sanitizer configuration is UBSan; an ASan pass is not implied.

For a deterministic graph render:

```sh
build/hydra-tui --headless-fixture tests/fixtures/tui/native-v2.tsv \
  --workflow-fixture tests/fixtures/tui/workflow-v1.tsv \
  --view workflows --size 140x32
```

Fixtures and the standalone example are explicitly synthetic. The separate
workflow integration evidence comes from execution. Neither implies a live
production fleet, collected machine telemetry, or a published release.

## Qualification on 2026-09-07

Local macOS qualification on `feature/c-tui-visualization` passed:

* `make test-all`: repository lint, shell, fleet/task acceptance, C, native TUI,
  PTY, visualization integration, parity, installation, and onboarding checks.
* After the final UI changes: `make test-tui test-tui-pty test-visualization
  sanitize-tui`, plus `make lint`. The final expanded PTY suite passed all 110
  assertions, including continuous-input refresh failure/recovery, empty-host
  filtering, failed-host mouse selection, and terminal restoration.
* `make sanitize-fleet`: supported sanitizer fleet and task acceptance checks.
* Extraction check: copied only the module, its test, example, and license into
  an isolated directory; both strict C99 builds and the standalone test passed.

Logs are local build artifacts: `build/test-all.log`,
`build/final-visualization-checks.log`, `build/final-pty.log`,
`build/final-lint.log`, and `build/sanitize-fleet.log`.

Rendered inspection used actual ANSI captures from PTYs at 140x40 and 80x24
(overview), 140x32 and 40x10 (workflow), plus a 140x30 host fixture render.
A local browser preview preserved terminal cell widths and captured styles.
Inspection checked panel alignment, selection contrast, graph arrows, readable
narrow fallback, and explicit absent telemetry. Native Ghostty automation was
unavailable, so this is browser inspection of captured terminal output together
with real PTY behavior tests; it does not certify every terminal/font combination.
The primary btop screenshot informed the density and hierarchy review.

Workflow integration executed a four-step dependency graph in a disposable real
Hydra project. Fleet integration used controlled transport and the real server;
no production remote hosts were accessed. CPU/memory/load remain unavailable,
and workflow graphs represent local recorded runs. These checks qualify the
feature locally; they do not constitute publication or a release.
