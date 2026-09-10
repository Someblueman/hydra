# Python boundaries

Hydra's installed CLI, coordinator and native interfaces remain C and POSIX
shell. The finite-example precompilers share one C/JSON-C implementation in
`examples/planning/native`, built with `make build-plan-precompile`.

The active tree contains 39 Python files: 29 tests/support files and 10 example
files. Their remaining responsibilities are listed below. These are development
and example dependencies; none is installed as Hydra runtime code.

| Retained example files | Required behavior |
| --- | --- |
| `manifest-map/check.py`, `patterns/check.py` | Independently reject missing, wrong, extra and incorrectly typed output. Emit the bound structured evidence required by public result acceptance. |
| `staged/check.py` | Check both the accepted finding and final join, using separate predicates and one evidence writer. |
| `staged/stage1_run.py`, `staged/unique_json.py` | Produce the bounded typed finding and its exact source/result hashes; reject duplicate JSON keys. The parser is shared by the producer and checker; their arithmetic and acceptance decisions remain separate. |
| `performance/measure.py`, `performance/analyze.py`, `performance/checker.py` | Capture raw timed trials, compute paired uncertainty, and independently verify samples, provenance and conclusions. The analyzer writes the complete report directly. |
| `research/research_report.py`, `research/research_check.py` | Simulate the supplied scheduling trace and independently check its schedules, metrics and bounded claims. |

Paths in that table are relative to `examples/planning/`.

| Retained tests/support | Required behavior |
| --- | --- |
| `tests/termviz/*.py` (9) | Drive real PTYs and independently observe rendered terminal cells, controls, plan launch, attachment and recovery. All eight drivers use one shared PTY implementation. |
| `tests/discovery/*.py`, `tests/test_enrollment*.py` (5) | Exercise SSH configuration, host discovery, identity binding, interrupted enrollment, duplicate requests and real local SSH transport. |
| `tests/test_plan_{manifest,patterns,staged,staged_public,inspection,reuse_invalidation}.py` (6) | Exercise public compilation, refusal boundaries, exact shell payload results, explanations, staged execution and reuse invalidation. |
| `tests/workflow_contract_cases.py` (1) | Check typed handoffs, composition, source/configuration binding and public task admission. |
| `tests/test_{performance,research}_outcome.py` (2) | Challenge independent checkers with correct, negative, malformed and tampered evidence. Performance controls use synthetic samples. |
| `tests/test_retention.py`, `tests/test_retention_accepted.py` (2) | Cover retention failure boundaries and expiry of a copy of a real accepted run, including preserved workspaces and duplicate submission identity. The latter is a manual acceptance driver requiring a retained task fixture, not a default unit-test entrypoint. |
| `tests/statistics_evidence.py`, `tests/test_statistics_export.py`, `tests/test_workflow_task_metrics.py` (3) | Reconcile durable evidence with native statistics and preserve unknown/zero, coverage and export semantics. |
| `tests/test_task_announce.py` (1) | Validate the public bounded task-announcement record and refusal behavior. |

The second pruning pass removed the four separate Python worker/composer scripts
in `manifest-map` and `staged`. One shared POSIX `native/payload.sh` handles their
bounded arithmetic and fixed joins. Copy it into either example repository before
committing its source snapshot. Frozen membership is passed as ordinary validated
arguments; joins consume actual worker files and fail if a file is missing. They
do not parse JSON or run a second planner. Checkers still receive the bound
manifest/finding and independently validate every result.

The two staged checker entrypoints now share `staged/check.py`; the `stage1`
argument selects the finding check. The forwarding `patterns/check.sh` and the
performance example's Python-containing `compose.sh` and extra copy step were
removed. The staged test's static result double now uses shell. No Python was
moved into shell heredocs, and no new interpreter or library dependency was added.
Existing shell tests with Python snippets retain their structured fixture and
assertion code; the feature example retains its structured acceptance-report
writer inside the independent verifier.

The earlier toy planner evaluation is available from its exact source commit;
[replay instructions](../examples/planning/evaluation/README.md) preserve the
historical experiment without another active framework. Earlier inventories and
execution evidence remain snapshots of their recorded commits.

New reusable planning or coordination behavior belongs in native code.
Straightforward command and filesystem orchestration belongs in shell. Keep
Python where its structured-data, terminal or numerical work is useful, with a
concrete caller and behavior to preserve. Test independence comes from separate
assertions and public boundaries; it does not require a particular language.
