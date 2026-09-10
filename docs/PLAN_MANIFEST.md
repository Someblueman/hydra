# Finite manifest planning pattern

The shared native `build/plan-precompile manifest` command validates a closed schema-1
manifest before the public planner sees it. The manifest contains only
`schema_version: 1` and an `items` array. Each item has a unique bounded ID, an
integer `value` from -10 through 10, and a boolean `enabled`; there are at most
eight items. Duplicate JSON keys, unknown fields, duplicate or unsafe IDs,
non-integer values (including booleans), non-boolean `enabled` values, and
unsupported sizes are rejected.

The pre-compile step lowers the validated manifest into an ordinary static
schema-1 plan. Enabled items receive one fixed worker each. A fixed compose step
joins those workers and emits an explicit `skipped` record for every disabled
item. Empty and all-disabled manifests still have a compose and independent
check step, so an empty result is checked rather than treated as completion by
absence. The manifest is a declared repository input and cannot select hosts,
tools, commands, or effects. Item IDs and values are data arguments to the fixed
worker command. Generated names use separate prefixes so IDs such as `compose`,
`check` and `manifest` cannot replace a join or its manifest input.

The independent `check.py` reconstructs all expected IDs, values, squares, and
skips from the bound manifest. It emits schema-3 evidence with one checker case,
true validation bindings, and a pass/fail verdict. Structural compilation proves
only that the static plan is bounded and connected; it does not prove runtime
outcomes.

Run the public compiler from a clean disposable source repository. Keep the
state home and compiled plan outside that repository: creating runtime state
inside it changes the source fingerprint and invalidates admission.

The fixed worker and join recipes use one shared POSIX payload. Copy
`examples/planning/native/payload.sh` into the copied example repository before
committing and compiling. The join receives frozen membership in its arguments;
the independent checker still consumes the bound manifest and checks all results.

```sh
make build-plan-precompile
precompiler="$PWD/build/plan-precompile"
fixture="$(mktemp -d)"
cp -R examples/planning/manifest-map "$fixture/source"
export HYDRA_HOME="$fixture/home"
cd "$fixture/source"
git init -q && git add . && git -c user.name=Test -c user.email=test@example.invalid commit -qm source
"$precompiler" manifest manifest.json plan.json
git add plan.json && git -c user.name=Test -c user.email=test@example.invalid commit -qm plan
hydra init --no-agent --trust
hydra workflow plan compile plan.json policy.json "$fixture/compiled.json"
```

`make test-plan-outcomes` includes six manifest tests alongside the thirteen
existing planning outcome tests. The manifest cases cover empty, all-skipped,
conditional and maximum membership, malformed/duplicate/over-limit input,
collision-prone IDs, boolean/integer confusion, and independent checker rejection
of missing members, wrong values, forged skips and unexpected members. The checker
revalidates the manifest contract independently of generation, so an invalid
manifest supplied between generation and compilation cannot acquire acceptance.

Public runtime qualification uses five disposable repositories per build: empty,
all-skipped, mixed, eight enabled members and an intentionally incorrect composer.
The first four pass their independent schema-3 check and public result gate. The
incorrect composer exits zero, its checker fails, and result retrieval rejects it.
The mixed case also changes the manifest after compilation; admission rejects the
change without publishing a run, then accepts the original compiled plan when the
reviewed bytes are restored. Default and UBSan checks retain source, commands,
compiled plans, runs and result bytes; see the [integration evidence](evidence/workstream-integration-20260910.md).

This is a bounded arithmetic planning pattern. Selection happens before compile;
runtime membership changes, late conditional evaluation and graph expansion remain
unsupported. It does not opt into artifact reuse or establish general planner
quality. A new finding can inform a separately compiled plan under the existing
acceptance path.
