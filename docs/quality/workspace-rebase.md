# Workspace rebase complexity inventory

The branch was rebased from `84a0f6f` onto `c0b6f82`. Main's existing per-function ceilings are retained. Branch-only modules are added to the same gate at their measured pre-rebase scores. No threshold or warning suppression changed. This records inherited complexity; it does not claim those functions are simple or analyzer-warning-free.

The original TUI translation unit and its include fragments were analyzed with clang-tidy 22.1.8, cognitive threshold 15 and the macOS SDK. Each moved function was checked against that measurement. The termviz and statistics files and C tests are byte-identical to the pre-rebase branch; the plan projection body is identical after the main helper rename. The integrated report is `build/rebase-port/quality-final.log`; the original TUI report is `build/rebase-port/original-complexity.log`.

| Current source | Function | Original | Ceiling | Comparison |
|---|---|---:|---:|---|
| `src/fleet/plan/plan_preview.c` | `plan_tui` | 17 | 17 | unchanged plan_tui body |
| `src/hydra_statistics.c` | `hs_load` | 91 | 91 | unchanged source file |
| `src/hydra_statistics.c` | `hs_summarize` | 25 | 25 | unchanged source file |
| `src/hydra_statistics.c` | `iso_time` | 17 | 17 | unchanged source file |
| `src/termviz/canvas.c` | `tv_bar` | 21 | 21 | unchanged source file |
| `src/termviz/canvas.c` | `tv_panel` | 51 | 51 | unchanged source file |
| `src/termviz/canvas.c` | `tv_put` | 18 | 18 | unchanged source file |
| `src/termviz/canvas.c` | `tv_text` | 17 | 17 | unchanged source file |
| `src/termviz/graph.c` | `tv_graph` | 93 | 93 | unchanged source file |
| `src/termviz/graph.c` | `tv_graph_layout` | 29 | 29 | unchanged source file |
| `src/termviz/input.c` | `mouse` | 16 | 16 | unchanged source file |
| `src/termviz/input.c` | `tv_input_feed` | 27 | 27 | unchanged source file |
| `src/termviz/output.c` | `tv_write_row` | 65 | 65 | unchanged source file |
| `src/termviz/plot.c` | `tv_plot` | 56 | 56 | unchanged source file |
| `src/termviz/present.c` | `tv_present` | 39 | 39 | unchanged source file |
| `src/termviz/pty_posix.c` | `tv_pty_close` | 21 | 21 | unchanged source file |
| `src/termviz/pty_posix.c` | `tv_pty_spawn` | 24 | 24 | unchanged source file |
| `src/termviz/terminal_csi.c` | `mode` | 28 | 28 | unchanged source file |
| `src/termviz/terminal_csi.c` | `tv_term_csi` | 68 | 68 | unchanged source file |
| `src/termviz/terminal_parser.c` | `consume` | 58 | 58 | unchanged source file |
| `src/termviz/terminal_screen.c` | `tv_term_draw` | 22 | 22 | unchanged source file |
| `src/termviz/terminal_screen.c` | `tv_term_glyph` | 28 | 28 | unchanged source file |
| `src/termviz/terminal_screen.c` | `tv_term_resize` | 22 | 22 | unchanged source file |
| `src/termviz/terminal_screen.c` | `tv_term_scroll` | 26 | 26 | unchanged source file |
| `src/termviz/terminal_sgr.c` | `tv_term_sgr` | 40 | 40 | unchanged source file |
| `src/termviz/tree.c` | `tv_tree_draw` | 35 | 35 | unchanged source file |
| `src/termviz/unicode.c` | `tv_utf8_decode` | 19 | 19 | unchanged source file |
| `src/tui/capture.c` | `native_capture_step` | 26 | 26 | hydra_tui include fragments |
| `src/tui/controls.c` | `native_controls_tick` | 27 | 27 | hydra_tui include fragments |
| `src/tui/controls_input.c` | `native_control_key` | 28 | 28 | hydra_tui include fragments |
| `src/tui/dashboard.c` | `dashboard_charts` | 17 | 17 | hydra_tui include fragments |
| `src/tui/dashboard.c` | `dashboard_distribution` | 34 | 34 | hydra_tui include fragments |
| `src/tui/dashboard.c` | `dashboard_heads` | 26 | 26 | hydra_tui include fragments |
| `src/tui/dashboard.c` | `render_dashboard` | 49 | 49 | hydra_tui include fragments |
| `src/tui/evidence.c` | `native_evidence_tick` | 22 | 22 | hydra_tui include fragments |
| `src/tui/forms.c` | `form_draw` | 23 | 23 | hydra_tui include fragments |
| `src/tui/forms.c` | `prompt_text` | 45 | 45 | hydra_tui include fragments |
| `src/tui/hosts.c` | `render_hosts` | 30 | 30 | hydra_tui include fragments |
| `src/tui/links.c` | `native_links_load` | 21 | 21 | hydra_tui include fragments |
| `src/tui/observations.c` | `native_observations_tick` | 79 | 79 | hydra_tui include fragments |
| `src/tui/plan_launch.c` | `native_plan_launch_tick` | 61 | 61 | hydra_tui include fragments |
| `src/tui/plan_model.c` | `native_plan_destroy` | 19 | 19 | hydra_tui include fragments |
| `src/tui/plan_projection.c` | `native_plan_accept` | 56 | 56 | hydra_tui include fragments |
| `src/tui/plan_projection.c` | `native_plan_tick` | 27 | 27 | hydra_tui include fragments |
| `src/tui/plan_render.c` | `native_workspace_evidence_text` | 27 | 27 | hydra_tui include fragments |
| `src/tui/plan_render.c` | `native_workspace_graph` | 21 | 21 | hydra_tui include fragments |
| `src/tui/plan_render.c` | `native_workspace_text` | 51 | 51 | hydra_tui include fragments |
| `src/tui/statistics_input.c` | `statistics_key` | 30 | 30 | hydra_tui include fragments |
| `src/tui/statistics_model.c` | `statistics_move` | 20 | 20 | hydra_tui include fragments |
| `src/tui/statistics_model.c` | `statistics_visible` | 34 | 34 | hydra_tui include fragments |
| `src/tui/statistics_render.c` | `render_statistics` | 115 | 115 | hydra_tui include fragments |
| `src/tui/statistics_render.c` | `statistics_fleet` | 60 | 60 | hydra_tui include fragments |
| `src/tui/statistics_render.c` | `statistics_run_detail` | 19 | 19 | hydra_tui include fragments |
| `src/tui/statistics_render.c` | `statistics_run_table` | 18 | 18 | hydra_tui include fragments |
| `src/tui/terminal_input.c` | `native_terminal_event` | 109 | 109 | hydra_tui include fragments |
| `src/tui/terminals.c` | `native_terminal_attach` | 21 | 21 | hydra_tui include fragments |
| `src/tui/terminals.c` | `native_terminals_pump` | 31 | 31 | hydra_tui include fragments |
| `src/tui/workflow_model.c` | `accept_workflows` | 29 | 29 | hydra_tui include fragments |
| `src/tui/workflow_model.c` | `load_workflows` | 57 | 57 | hydra_tui include fragments |
| `src/tui/workflow_model.c` | `workflow_edges` | 27 | 27 | hydra_tui include fragments |
| `src/tui/workflow_model.c` | `workflow_key` | 31 | 31 | hydra_tui include fragments |
| `src/tui/workflow_render.c` | `render_workflow_graph` | 46 | 46 | hydra_tui include fragments |
| `src/tui/workspace.c` | `native_workspace_activity` | 77 | 77 | hydra_tui include fragments |
| `src/tui/workspace.c` | `native_workspace_details` | 64 | 64 | hydra_tui include fragments |
| `src/tui/workspace.c` | `native_workspace_key` | 50 | 50 | hydra_tui include fragments |
| `src/tui/workspace.c` | `native_workspace_mouse` | 19 | 19 | hydra_tui include fragments |
| `src/tui/workspace.c` | `native_workspace_move` | 33 | 33 | hydra_tui include fragments |
| `src/tui/workspace.c` | `native_workspace_tree` | 75 | 75 | hydra_tui include fragments |
| `src/tui/workspace.c` | `render_native_workspace` | 202 | 202 | hydra_tui include fragments |
| `src/tui/workspace_terminals.c` | `native_workspace_show_terminal` | 20 | 20 | hydra_tui include fragments |
| `tests/c/test_statistics.c` | `main` | 38 | 38 | unchanged source file |
| `tests/c/test_termviz.c` | `main` | 32 | 32 | unchanged source file |
| `tests/c/test_termviz_input.c` | `main` | 40 | 40 | unchanged source file |
| `tests/c/test_termviz_present.c` | `main` | 19 | 19 | unchanged source file |
| `tests/c/test_termviz_terminal.c` | `main` | 56 | 56 | unchanged source file |
| `tests/c/test_termviz_unicode.c` | `main` | 27 | 27 | unchanged source file |
| `tests/c/test_termviz_workspace.c` | `main` | 23 | 23 | unchanged source file |

## Integration checks

- Main's modular process capture and input-discard state remain in use. Workspace
  views now compile as separate `src/tui/*.c` modules; the obsolete entry point
  and include-fragment runtime are removed.
- Main's declarative palette and plan command tables retain their original
  commands. Workspace protocol projections are additional entries in those tables.
- The native binaries build with the repository's C99 warnings-as-errors flags.
- `make -j4 test-all` passes on the integrated tree, including shell, fleet,
  workflow/task execution, installation, export, native rendering and PTY checks
  (`build/rebase-port/final-acceptance.log`).
- `make test-quality-c quality-c` passes with clang-tidy 22.1.8 and the macOS SDK.
  The report includes 196 functions above the advisory threshold and existing
  analyzer diagnostics; no warning-free claim is made.
- The combined TUI suite passes 137 PTY assertions. A fresh UBSan build in
  `build/rebase-sanitize` passes TUI, PTY, plan workspace/launch/controls,
  statistics and attached-client tests (`build/rebase-port/final-sanitizers.log`).
- All nine headless views match the pre-rebase binary for local and fleet data at
  40x10, 80x24 and 140x40: 54 comparisons, ignoring trailing space padding only.

The live-provider evidence in `docs/WORKSPACE_REAL_ACCEPTANCE.md` remains tied to
its recorded pre-rebase run. No new provider campaign or publication was performed.
