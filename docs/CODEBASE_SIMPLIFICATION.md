# Codebase simplification review

This work addresses the audit of `main` at `2c4efea` on
`refactor/codebase-simplification`. The original clang-tidy 22.1.8 run analyzed
66 translation units and reported 118 functions above cognitive complexity 15
(88 fleet, 19 core/TUI, 11 tests), plus 21 analyzer diagnostics. These are
maintenance signals, not a count of bugs. ShellCheck and dash passed; shell
cognitive complexity is not measured.

The initial review below records work through `de7dd9c`. The
[follow-up](#follow-up-module-boundaries) records the subsequent module reorganisation.

## Item-by-item disposition

| Audit item | Change or concrete retention decision | Relevant acceptance |
|---|---|---|
| PTY setup failure | Failed setup now leaves invalid handles and every caller stops before using them. Partial acquisitions are closed. Child terminal/environment setup has its own helper. A real descriptor-exhaustion child checks the failure boundary. | `test-tui-pty` |
| PTY environment pointers | Copy executable arguments before changing the environment; build PATH while the borrowed old PATH remains valid. Short-write diagnostics report byte counts without reading stale errno. | `test-tui-pty`, C analysis |
| Fleet task CLI (129) | `task_remote.c` separates the option table, numeric limits, request construction and verified result output from handshake/transport/binding/uncertainty handling. Duplicate options, allowed operations, bounds, error categories and mutation reconciliation remain explicit. | `test-fleet`, task acceptance/collection suites |
| General fleet CLI (108) | `cli.c` gives parsed options one concrete structure and separates parsing and native TUI launch. Package/watch/import/export behavior remains in the command coordinator because each branch is short and owns its response. No generic command framework was added. | `test-fleet` |
| Agent execution (120) | `agent_run.c` separates exact-resume binding, safe-point steering and terminal-state precedence. The coordinator still owns all JSON objects, the immutable prompt copy, invocation, retention and final receipt. Their durability order remains visible. | agent profile/execution and task agent suites |
| Provider decoding (98) | Codex, Claude, Pi and OpenCode now use concrete provider helpers alongside the existing canonical, agy and Cursor decoders. Pi exits early for unrelated messages. Session, permission, answer and usage semantics remain provider-specific. | `test-agent-profile`, recorded provider fixtures |
| Task execution (80) and cancellation | `task_execute.c` isolates head creation/provenance and execution argv construction. It still owns the deadline, remaining budget, approval suspension, uncertain outcomes and sealing. `process.c` makes the optional control/grace invariant explicit; the poll/kill/reap loop remains together so PID ownership and cancellation timing can be reviewed in one place. | task execution, cancellation, approval/resume suites; sanitizers |
| Plan CLI (96) | Compile preparation and its buffers have a dedicated command helper; help, preview, result and admission no longer share compile-only ownership. | `test-plan`, workflow plan suites |
| Plan validation and graph phases | `plan_schema.c` separates exec recipes and policy bounds; common step identity/roles, spawn rules and write scopes remain in `recipe`. `plan_graph.c` names dependency closure and write-conflict checks and retains the existing coverage helper. The bounded closure matrix and ordered diagnostic walk remain direct rather than introducing a graph library. | `test-plan`, workflow plan/data suites |
| Task preparation | Retain the small `task_prepare` coordinator: it already calls `preflight`, `init_bare`, `inputs_prepare`, `task_spec` and `task_json_hash`. One owner of the scratch directory and ordered error category is simpler than returning intermediate owned packages through more helpers. `task_inspect` retains its independent payload/hash/bundle verification and single cleanup path. Shared JSON access and direct includes were updated. | task package tests: exact source, binary inputs, malformed manifests, source preflight, preservation |
| Result sealing | Retain `task_result_seal` as the independent validation plus atomic storage boundary. Its `snapshot` helper already delegates heads, artifacts and evidence to `task_result_heads`, `artifacts`, and `task_result_evidence`. Keep ancestry checks, removal of private workspace fields and bundle encoding together with snapshot ownership. Downloads continue to verify the sealed result rather than recapturing mutable work. | result probes, collection and integration suites |
| Duplicated JSON/protocol helpers | Move identical NUL-free JSON text validation from task code to `f_text`; `f_string` uses it. Keep the capability scans separate: general fleet accepts its existing string comparison, task requires protocol 1 plus NUL-free names, and auth applies a different credential-bound response policy. Merging them with flags would obscure these distinct contracts. | fleet, plan, agent, workflow-data and result tests |
| Fleet build/header coupling | Compile fleet objects once into an internal archive; the executable and seven C test targets link that archive. Compiler `.d` files track actual header and `.inc` dependencies. Agent, plan and workflow-data headers depend on the common fleet types; implementations include task/workflow contracts directly when used. No published CLI/wire symbol was renamed. | native/fleet builds, all seven C test targets, sanitizer build |
| Shell dependency loading | `_load_libs` loads ordered arguments; execution commands share the existing runtime prefix, with spawn's earlier layout dependency preserved. Command-specific tails remain explicit. | full shell suite, installed CLI checks |
| Shell list row enrichment | One traversal computes lifecycle, duration, PR and Git details for both human and JSON formatting. Formatting remains separate. | JSON-output, integration and list-related shell tests |
| `cmd_spawn` | Separate parsing from execution while retaining command-scoped POSIX variables and option precedence. No eval-based option object or new serialization is introduced. | spawn lock, bulk spawn, onboarding, profile, task injection and scope tests |
| Spawn rollback and positional arguments | After durable identity commit and lock release, `spawn_start_session` returns failure to a single retirement/rollback owner. Earlier failures keep their distinct worktree/session-lock cleanup. Retain the explicit ten positional arguments: all callers pass the same fixed contract, while a shell pseudo-structure would require globals, eval or an extra file. | lifecycle, spawn-lock, setup-command and session-hook suites |
| Workflow command dependencies | Run directory and step ID are explicit arguments. Profile command construction is a separate helper with five explicit inputs. Short spawn/wait/message/approve/kill/gate recipes stay in the switch; the durable graph's fixed positional fields and argv/shell permission distinction are preserved. | workflow runtime/data, agent, approval and end-to-end suites |
| Restricted YAML | Separate the existing awk record parser from trusted application; one shared environment loop handles session/window/pane targets in their original order. Retain the deliberately restricted grammar and trust boundary. | YAML config, templates, setup-command and trust tests |
| Native TUI key dispatch | The refresh/render loop calls a separate key switch; fleet-only routing and forbidden local actions are checked before shared keys. Mouse and palette handlers remain separate existing functions with distinct coordinate/input contracts. | headless and real PTY input/mouse tests |
| Native TUI rendering/records | Rendering now has header and footer phases around existing view renderers. Fleet and local head records have separate parsers with unchanged field counts, numeric checks and safety bounds. Retain the compact process-entry argument grammar and mouse/palette parsing: extracting individual flags or escape bytes would spread one grammar across helpers. | native fixtures, malformed numbers, fleet rows, themes and PTY tests |
| Duplicated bounded capture | `hydra_tui_process.inc` owns child spawn, timed reads and deadline-aware reaping for refresh and preview. Output sinks and fallback messages stay at callers. Fix timeout bypass after stdout EOF; preserve process-group termination, byte caps and last-good model replacement. Keep `refresh_current_session` separate: it is a small single-line query with its own bounded wait policy. | hung and EOF-before-exit fixtures, PTY restoration/fallback tests |
| Core state traversal | Collect branch scalars once instead of reopening/rescanning every head for each comparison. Keep in-memory pair comparisons to preserve directory-order and lexicographic-pair diagnostics; no CPU speedup is claimed. Add allocation multiplication guards, separate project scalar paths from the heads directory, and detect sticky snapshot stream errors. Validation still precedes snapshot collection because those are separate public read-only APIs. | native/shell snapshot parity, duplicate/corrupt state tests, output-error unit check |
| Cohesive JSON escaping | Retain the direct escaping switch and its per-write checks. Each case expresses a required JSON byte representation; helper extraction would add indirection without removing decisions. | canonical escaping and snapshot parity tests |
| Packaging | Replace identical core/TUI packagers with `package-native.sh core|tui`. Preserve artifact names, environment overrides, checksums and source metadata. Makefile and install-test callers are updated; public make targets remain. | `test-native-install`, packaging provenance checks |
| Test assertions | Integration, JSON-output and switch suites source the existing common assertions where bodies were equivalent. Leave specialized assertion semantics in their owning tests. | corresponding suites, full shell suite |
| Hooks/CI lint | One filename-safe shell checker is used by `make lint`, generated pre-commit hooks and CI. A disposable installed hook checks filenames containing spaces and newlines and rejects bashisms. Remove the duplicate generic CI syntax step, retaining the shell compatibility matrix and OS/sanitizer coverage. | `test_lint.sh`, `make lint` |
| Complexity enforcement | Use pinned clang-tidy checks and threshold 15, a reviewed per-function ceiling and a required CI analysis job. Compiler/tool failures fail the check. Existing analyzer warnings remain visible. No warning suppression or threshold increase was added. | `test-quality-c`, `quality-c` |
| File-size review findings | Retain `plan_schema.inc` as declarative published schema data and `parallel.sh` as related coordination primitives. Splitting either merely to cross 500 lines would not resolve the measured function-level hotspots. The size threshold remains a review signal. | schema/parallel suites, unchanged size-review policy |

## All 21 original analyzer diagnostics

Locations below are from `2c4efea`, so they remain stable references to the audit
rather than drifting line numbers in the refactored tree. No diagnostics are
suppressed. A clarified invariant is distinguished from a confirmed defect.

| # | Original location / diagnostic | Disposition and evidence |
|---|---|---|
| 1 | `agent_auth_cli.c:100`, null approval | `copy` already requires a 64-character hash, while status/login/preview exit earlier. Add the explicit null guard at comparison so the boundary is visible; approval requirements are unchanged. |
| 2 | `agent_probe.c:12`, null `strdup` | Cache PATH once before its null check and copy. Repeated environment lookups were harder for the analyzer to relate; no evidence of a runtime null dereference in the original single-threaded path. |
| 3 | `cli.c:63`, tainted `execl` | Retain: `HYDRA_TUI_BIN` is an intentional operator-selected executable, invoked directly with argv. Treating it as untrusted remote text or adding a shell would alter the override contract. |
| 4 | `files.c:16`, hex digit index | Retain: `fgetc` returns EOF or an unsigned-byte value; EOF is checked. On supported Linux/macOS targets, shifting by four or masking by 15 produces an index 0–15. Allocation is bounded before the loop. |
| 5 | `json.c:49`, JSON terminator | Retain: allocate `limit + 1`, read at most that amount, and write the terminator only when `length <= limit`. The limit is checked against `F_LIMIT` first. |
| 6 | `json.c:125`, text terminator | Retain: allocate `limit + 1`, read at most `limit`, then write at index `size <= limit`; all current callers pass bounded constants no greater than `F_LIMIT`. |
| 7 | `main.c:17`, global stack reference | Use static storage for the home buffer. Its former lifetime extended to process exit, but static storage states the global lifetime directly. |
| 8 | `plan_json.c:66`, key array | Retain: allocate by `json_object_object_length` and iterate the same, unmodified object's members. Recursive canonicalization starts only after key collection. The analyzer cannot relate JSON-C's iteration count to its length. |
| 9 | `process.c:38`, overwritten errno | Retain: failure of either pipe goes to cleanup and returns `-1`; this API does not consume errno as its error category. The initialized descriptors determine cleanup. |
| 10 | `process.c:58`, optional control | `cap.cancelled` was set only by a present control callback. Capture an explicit optional grace value and guard the stop-unknown output; cancellation timing is unchanged. |
| 11 | `task_endpoints.c:118`, log errno | Fix: preserve `openat` failure errno across descriptor close, and finish metadata/validation before traversing log paths. This prevents JSON/cleanup work from changing missing-versus-invalid log classification. The original owner-path `EINVAL` guard was already present and was not the defect. |
| 12 | `task_execute.c:55`, global Git config | Cache `GIT_CONFIG_GLOBAL` once before checking/copying it. Preserve restoration after source preparation. |
| 13 | `task_execute.c:56`, system Git config | Cache `GIT_CONFIG_NOSYSTEM` once before checking/copying it. Preserve the startup-only isolated Git environment. |
| 14 | `tui.c:6`, empty fallback string | Retain: a null value becomes the empty literal, whose first byte is NUL, so the loop body and pointer increment are never entered. |
| 15 | `test_libhydra.c:28`, overwritten errno | Retain: the test checks returned bytes and exact encoded text; it never interprets errno from rewind/fread. |
| 16 | `test_tui_pty.c:76`, stale errno | Fix the diagnostic: short/failed writes report bytes written versus requested, without claiming an unrelated errno describes a timeout. |
| 17 | `test_tui_pty.c:122`, invalidated PATH | Fix: construct PATH before calling any environment-mutating function. |
| 18 | `test_tui_pty.c:124`, tainted executable | Retain: the test deliberately launches its supplied fixture/native executable using direct argv. This is test configuration, not a remote execution field. |
| 19 | `test_tui_pty.c:124`, invalidated Hydra argument | Fix: duplicate the argument before changing the environment; it may have come from getenv. |
| 20 | `test_tui_pty.c:124`, invalidated TUI argument | Fix: duplicate the executable path before changing the environment. |
| 21 | `test_tui_pty.c:138`, uninitialized session | Fix: initialize all handles/PID to invalid values, close partial acquisitions, and return immediately from callers on setup failure. The descriptor-exhaustion test exercises this failure. |

Refactoring exposes three additional nullability warnings in `task_remote.c`.
They are retained with explicit call-boundary evidence: `parse_options` returns
success only with a host and the operation's required ID/input; `task_inspect`
returns success only with a validated spec containing its host; and the response
binding check validates `spec_sha256` before calling `result_output`. The helper
is static and has no other caller. These warnings do not establish a new missing
validation path. The final report keeps them visible.

## Measurement and acceptance

The checker still uses clang-tidy 22.1.8, the same native flags, all 66 translation
units, header diagnostics, and cognitive threshold 15. See
[the quality policy](QUALITY_HOOK.md) and
[reviewed ceilings](quality/cognitive-complexity.tsv). Comparisons count distinct
source/function pairs, not line numbers or repeated header diagnostics.

The reviewed tree has **123** above-threshold functions versus **118** originally.
The increase in count is caused by named helpers extracted from larger routines;
it is not presented as an improvement by itself. The maximum reported score
falls from **129 to 95**. The sum of the reported above-threshold scores falls
from **4,070 to 3,703**; that sum excludes functions at or below 15, so it is not
a whole-program complexity measure.

| Hotspot | Original score | Refactored coordinator score |
|---|---:|---:|
| `task_remote_cli` | 129 | 34 |
| `agent_run_cli` | 120 | 82 |
| `f_cli` | 108 | 66 |
| `agent_decode` | 98 | at or below 15 |
| `plan_cli` | 96 | 81 |
| `task_execute` | 80 | 59 |
| `plan_graph` | 61 | 34 |
| `recipe` | 56 | 26 |
| `plan_validate` | 38 | 21 |
| `interactive_main` | 57 | 17 |
| `render` | 51 | at or below 15 |
| `refresh_model` | 49 | 28 |
| `load_model_stream` | 41 | 33 |
| `capture_preview` | 32 | at or below 15 |

The first reviewed ceiling includes the following explicit exceptions for the
new decomposition: Codex/Claude decoder helpers (16 each), general option parsing
(32), task option parsing (25), exec recipe validation (22), policy bounds (17),
render header/footer (20/18), and key dispatch (17). Each is a concrete phase of
an existing larger function and is reviewed with its coordinator, not in
isolation. Further subdivision solely to reach 15 is not required.

Four existing functions increase for reviewed correctness/ownership changes:
auth approval's explicit null guard (80 to 81), optional cancellation control
handling (91 to 95), snapshot allocation overflow checking (38 to 42), and branch
collection with allocation/cleanup instead of repeated filesystem traversal
(23 to 29). These increases are recorded rather than hidden by suppressions or
changed checker options. Reduced ceilings are ratcheted down; removed/below-limit
functions no longer carry allowances.

The final analyzer run still reports **12** non-complexity warnings: nine retained
original diagnostics and the three call-boundary warnings described above.
Compiler errors are absent. This does not mean that 12 original bugs were fixed:
several removed warnings were clarified invariants.

Final local acceptance passed on macOS:

| Command | Result |
|---|---|
| `make lint` | Passed, including the final `test-all` run |
| `make test-quality-c` | Passed real compiler/checker policy cases |
| `make quality-c` | Passed all 66 translation units against the reviewed ceilings; retained diagnostics remain visible |
| `make test-all` | Passed shell, fleet, native core/TUI, real PTY, parity, installation and onboarding acceptance |
| `make sanitize` | Passed the repository's macOS UBSan configuration, including fleet task acceptance |
| `git diff --check` | Passed |

Linux ASan and hosted CI were not executed locally. No push or publication was
performed. Existing untracked `.gmcs/` and `output/` content was preserved.

## Follow-up module boundaries

This round starts from `de7dd9c` and keeps the existing CLI, JSON, durable-state
and shell/native argv contracts. The organisation now follows these ownership
boundaries:

| Area | Organisation and ownership |
|---|---|
| Fleet families | `src/fleet/agent`, `auth`, `plan`, `task`, `workflow`, `transport` and `support` contain the existing related implementations. Forty-four relocated C files retain identical bodies outside includes. |
| Fleet headers | `fleet.h` holds common constants and process configuration. JSON, bounded files, process capture, transport, bundles and dispatch have separate headers. Callers include the interfaces they use; there are no forwarding compatibility headers. |
| Support and transport | Filesystem operations moved out of JSON parsing; SSH command construction and remote target validation live with transport. Capture owns pipes, process-group deadlines and reaping; transport calls that API. |
| Plan commands | A local name/arity table dispatches concrete operation handlers. Each handler owns its JSON cleanup. Text-only success is explicit and remains distinct from failed validation. |
| Authentication | Options are parsed once, validated for their operation and passed to credential handling. Preview construction borrows validated metadata. Secret transmission still requires the exact approval hash and still filters the response after transmission. Flag precedence and validation order are preserved. |
| Agent execution | The existing shell argv is decoded into named borrowed fields. Resume binding, steering, invocation and observation construction are explicit phases. The coordinator retains the running/final receipt writes and their ordering. |
| Fleet subprocesses | Each stream groups its pipe descriptors, capture buffer and byte count. One drain helper handles stdout/stderr; the lifecycle loop still owns termination, grace deadlines, PID reuse protection and cancellation uncertainty. |
| Native TUI | Ten independently compiled `.c` files replace the textual `.inc` implementation chain. Model parsing has no terminal or process dependency. The adapter combines model and bounded subprocess APIs. Selection, rendering, actions and input borrow caller-owned app state. Signal flags and the terminal cleanup pointer are private to the terminal module. |
| Shell cancellation | `cmd_exec_cancel_workers` receives the profile policy explicitly rather than reading its caller's `_ce_profile` variable. The trap passes the same policy, preserving supervisor-first cancellation. |
| Build and analysis | Fleet/TUI objects and analysis share recursive native source discovery. Compiler dependency files track private headers. Packaging keeps its existing binary names and installation layout. |

All 23 fleet/TUI headers compile independently with the repository's strict C99
flags. The build/analysis inventory matches all **75** C translation units;
the increase from 66 comes from splitting the native TUI compilation unit.

| Hotspot | Before this round | After |
|---|---:|---:|
| `plan_cli` | 81 | 16 |
| `auth_cli` | 81 | at or below 15 |
| `agent_run_cli` | 82 | 57 |
| Process `run` | 95 | 68 |

The maximum reported cognitive complexity falls from **95 to 68**. The number
of functions above 15 increases from **123 to 125**: authentication now has
separate option parsing (20), operation validation (23) and credential handling
(25), while its dispatcher falls below the reporting threshold. These are
reviewed phases with distinct inputs and ownership, not relaxed checks.
No existing function's ceiling increased after mapping moved paths.
Reduced/removed allowances are ratcheted down. The sum of above-threshold scores
falls from **3,703 to 3,572**; this excludes functions at or below 15 and is not a
whole-program metric. Shell cognitive complexity remains unmeasured.

Analysis retains **13** non-complexity diagnostics: the previous 12, plus the
now independently analyzed adapter's `rewind`/`malloc` errno warning. The adapter
never interprets errno from `rewind`; allocation failure is checked by its return
value. A repeated TERM lookup exposed by separate compilation now caches the
environment value before validation. New capture tests check seek results
directly. No analyzer warnings are suppressed.

The new capture test exercises independent stdout/stderr, stdout closing before
stderr, cumulative stdout observations, separate log truncation budgets and a
nonzero child exit. Existing plan/auth/agent acceptance and all 87 real PTY checks
passed during this round. Final local acceptance passed:

| Check | Result |
|---|---|
| `make test-all` | Passed the complete shell/native, fleet task, PTY, parity, installation and onboarding suites, including `make lint` |
| `make quality-c` | Passed all 75 translation units against reviewed, ratcheted ceilings |
| `make test-quality-c` | Passed the real checker's regression and failure-boundary cases |
| `make sanitize` | Passed macOS UBSan, including complete fleet task acceptance |
| Fresh temporary build directory | All three native executables built without existing objects |
| Header/source inventory checks | All 23 fleet/TUI headers compiled independently; analysis selected all 75 C sources |
| `git diff --check` | Passed |

Linux ASan and hosted CI were not run locally. Work used the existing checkout;
nothing was pushed or published, and `.gmcs/` and `output/` content was preserved.

## Feature-development stopping point

The follow-up makes the native palette a concrete extension point: its fourteen
actions declare their command, optional subcommand and selection scope in one
table. Spawn and comparison retain their existing prompts. First substring match,
literal argv boundaries, missing-selection notices and terminal restoration are
unchanged. `execute_palette` falls from cognitive complexity 25 to at or below
15; its allowance is removed. The gate now reports 124 functions above 15 with
no regressions. This does not change the previous analysis limitations.

The contributor guide now maps common feature types to their owning modules and
focused tests. This is a practical stopping point for general simplification:
command dispatch, data parsing, process ownership and presentation have distinct
homes. Remaining lifecycle and validation coordinators should be changed against
the requirements of the next feature, preserving their ordering and cleanup
contracts. This is an engineering judgment, not evidence that future changes are
risk-free or that every function is small.

This localized follow-up passed 79 deterministic TUI checks, 108 real PTY checks,
the same 108 PTY checks with macOS UBSan, repository shell lint, all-source
`make quality-c` against the reduced baseline, and `git diff --check`. The added
PTY cases exercise ordinary command mappings, first-match precedence, literal
comparison arguments, canceled/unknown searches and missing-head boundaries.
The prior round's full `make test-all` remains the broader qualification; it was
not repeated for this palette-only change. Linux ASan and hosted CI were not run.
