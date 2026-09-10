# Staged bounded planning

`examples/planning/staged` demonstrates a two-stage finite workflow. Stage 1
reads the declared `manifest.json`, computes bounded squares, and emits a schema-3
finding containing the source hash, selected IDs, result hash, and exact results.
The shared native `build/plan-precompile staged` command accepts only that fresh, internally consistent finding; a changed
source, stale hash, unsupported status, forged selection, or altered result is
rejected. It then lowers the frozen selection into an ordinary static schema-1
DAG with one worker per selected item, a fixed join, and an independent checker.

Stage 2 cannot add members, hosts, tools, argv, or effects. Empty selections remain
valid and produce a checked empty report. The checker consumes Hydra's emitted
validation binding and emits schema-3 evidence with the bound validator and recipe
hashes. Structural compilation and a valid stage-1 finding do not claim runtime
success; the stage-2 result must pass its independent checker.

The graph is `stage1 source -> stage1 finding -> precompile -> fixed workers ->
join -> checker`. This is a data handoff and compile boundary, not runtime graph
expansion or a new authority.

Each manifest member lowers to a fixed spawn/work subgraph; the outer pattern
joins those subgraphs before verification. There are at most eight members, strict
integer/boolean types and duplicate-key checks. IDs such as `finding`, `compose`
and `check` are supported through prefixes. Empty and all-skipped selections still
produce an independently checked report.

Build the native helper once with `make build-plan-precompile build-plan-example`. Copy
`examples/planning/native/payload.sh` and `build/plan-example` into the staged example repository before
committing its source. Both check steps use `./plan-example staged-check`: `stage1` selects finding
validation, and no argument selects the final joined-report validation.
Run the stage-1 plan through the ordinary compile/run/result commands. Copy the
accepted `report` deliverable to `finding.json` beside `manifest.json`, then run:

```sh
HYDRA_BIN=/path/to/hydra/bin/hydra /path/to/hydra/build/plan-precompile staged manifest.json RUN_ID plan2.json
```

The precompiler calls the real `workflow plan result RUN_ID`, verifies its passing
check and raw finding hash, requires local `finding.json` to match those accepted
bytes, and recomputes source, selected IDs, arithmetic and result hash. Commit that
source snapshot and compile `plan2.json` with `policy.json`; accept the resulting
digest explicitly with `workflow plan run`. Generated findings do not enlarge the
existing policy, graph, retry or repair limits. Each stage requires a separate
admissible compilation and independently verified result.

Focused tests use a public-result double to isolate semantic gates; the retained
`tests/native/test_plan_staged_public.c` test runs both actual plans, retrieves their real
results, and rejects changed local findings and stale source before compilation.
Current qualification is recorded in the remaining-work evidence index. This
finite pattern does not require runtime expansion; dynamic membership and late
conditional evaluation remain unsupported and must not be inferred from it.
