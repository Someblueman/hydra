# Real maintenance workload: Python cleanup

The selected cleanup replaces both Python precompilers with one shared native
C/JSON-C helper and retires the superseded toy evaluation runner. Python falls
from **56 files / 8,277 lines** to **44 files / 6,490 lines**. Forty-one retained
Python files are byte-identical; three change only their precompiler entrypoints.
All 19 existing shell files containing Python remain byte-identical. No Python
was moved into shell or added to the installed runtime.

Implementation: `9cb84c06879ccde90a16938709983e54680a4f68`, followed by the
one-line POSIX declaration correction in
`6cef5ba8c401020401e5e8817193750a95120beb`. The installed `bin`, `lib` and `src`
trees are unchanged. The native helper contains 415 C lines plus a 16-line header;
it replaces 437 Python lines shared across the two old precompilers. The remaining
reduction comes from retiring the old toy evaluation. Its immutable observations
and [Git-history replay](../../../examples/planning/evaluation/README.md) remain.

## What 9E did here

The [frozen workload contract](contract.json), [before inventory](inventory-before.json)
and [selection](selection.json) precede implementation. A Python consolidation was
rejected because it retained substantive planning logic and compatibility wrappers
in Python. Rewriting all Python tests was rejected because it added work and risked
independent coverage. Codex authored the implementation outside Hydra's execution
graph; the workflow seals that actual patch, and independent heads apply it to the
fixed baseline, rebuild and check the resulting source.

Both compiled schedules have six nodes, the same objective, contracts, artifact,
budgets and local placement. Each has three head-creation nodes, a patch producer,
a functional checker and a sanitizer checker. Parallel validation removes the
serial edge between the two independent checks. Distinct head names isolate the
real executions; they are not a decomposition change. The deterministic
[comparison](estimated-comparison.json) reports matching scope and leaves modeled
preference unresolved. The same pre-execution estimate ranges were rebound without
adjustment to the final comparison digests. They are not calibrated intervals.

| Matched execution at `9cb84c0` | Wall seconds | Public result |
| --- | ---: | --- |
| Parallel, `run_b9024fef69af15c59acd` | 106.69 | Pass |
| Serial, `run_632a68921f47afb2d1f9` | 106.33 | Pass |

There is no observed scheduling advantage in this pair. Planning before selection
already took about 14.5 minutes, exceeding either execution. These are two
consecutive local runs on a shared machine, with fresh build directories but no
randomized order or repeated sampling. They do not establish a causal planning
benefit, stochastic quality, monetary savings or general estimate calibration.
The pair predates the one-line portability correction; the final corrected patch
has its own fresh workflow acceptance in [results](results.json).

## Acceptance and rework

The native port passes the retained 19 outcome tests and five staged tests under
both the default build and LLVM 22 ASan/UBSan. Independent checks compare ten full
original/native graphs and reject reordered/duplicate selections, rehashed result
forgeries, a failed check below an outer pass, changed local finding bytes, FIFO
inputs, NUL-bearing IDs, decimal integers, escaped duplicate keys and trailing
JSON. Parent-relative staged input and GNU-only hash fallback also pass. The real
staged driver executes both stages and rejects stale source and changed findings.

The deliberately incomplete artifact retained both old Python precompilers.
Its 24 focused tests passed, but the independent audit reported
`Retired Python remains`, and public result retrieval refused the artifact.
That is one declared incorrect maintenance artifact rejected; it is not a claim
of safety for arbitrary hostile patches. The three caller migrations were
personally reviewed to preserve every test assertion, alongside the automatic
hash checks for the other 41 Python files.

The first delegated implementation never compiled and left no changes. Root
completed the port. Review repaired staged path handling and GNU hash fallback.
Apple clang 17's ASan runtime stalled before `main`; a sampled stack records
sanitizer initialization, and LLVM 22.1.8 passed both the startup probe and the
actual suites. The first workflow's execution checks passed but its evidence
hashes used the wrong slash encoding; Hydra refused acceptance. The report writer
was corrected to the existing JSON-C canonical format. An existing-head admission
refusal and one incremental-patch packaging error were also repaired. These are
recorded rework, not hidden passes or extra independent corruption cases.

Shell lint passed on an exact source export, including the new workflow scripts.
The root lint traversal was stopped because it recursively scanned retained
build fixtures. Focused native core/unix analysis passes with warnings as errors.
No Linux execution is claimed; the final feature-test macro follows the existing
Hydra convention. Per-request provider cost is unavailable. One human workload
selection and zero subsequent human corrections were observed in this task.

## Reproduce

Offline comparison from the current repository is deterministic and performs no
execution:

```sh
make build-fleet
packet=docs/evidence/python-cleanup-20260910
bin/hydra workflow plan compare "$packet/run-serial-v4-compiled.json" \
  "$packet/run-parallel-v4-compiled.json" --estimates "$packet/estimates.json"
bin/hydra workflow plan explain "$packet/run-parallel-v4-compiled.json"
```

For a fresh actual run, export the original source and bind the supplied checker
recipes before introducing the patch. `workflow/sanitizer-cc` records the compiler
used here; select a working local sanitizer compiler there before committing a
replay snapshot. The scripts are specific to this archived maintenance workload,
not a new general-purpose evaluation framework.

```sh
hydra_root="$PWD"
packet="$hydra_root/docs/evidence/python-cleanup-20260910"
fixture="$(mktemp -d)"
mkdir "$fixture/source"
git archive b0fb5b1d9220189ad2498a244274ae498a3c7d0c | tar -x -C "$fixture/source"
cp "$packet/workflow/"* "$fixture/source/"
(
  cd "$fixture/source"
  git init -q
  git add .
  git -c user.name=Qualification -c user.email=qualification@example.invalid \
    -c commit.gpgSign=false commit -qm 'Baseline and reviewed check recipes'
)
git diff --binary --src-prefix=c/ --dst-prefix=i/ \
  b0fb5b1d9220189ad2498a244274ae498a3c7d0c \
  6cef5ba8c401020401e5e8817193750a95120beb > "$fixture/source/cleanup.patch"
export HYDRA_HOME="$fixture/home" HYDRA_FLEET_BIN="$hydra_root/build/hydra-fleet"
export HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
cd "$fixture/source"
"$hydra_root/bin/hydra" init --no-agent --trust
"$hydra_root/bin/hydra" workflow plan compile "$packet/final-plan.json" \
  "$packet/policy.json" "$fixture/compiled.json" > "$fixture/compile.json"
digest="$(jq -er '.data.sha256' "$fixture/compile.json")"
"$hydra_root/bin/hydra" workflow plan explain "$fixture/compiled.json"
"$hydra_root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$digest"
```

A new source path and commit produce new compilation bindings. Inspect and admit
that exact digest; old execution head identities and old acceptance do not carry
over. [Results and hashes](results.json) distinguish the matched experiment,
final corrected-source acceptance, expected refusal and unsuccessful setup work.
All commits are local; main and the local origin/main tracking reference were
unchanged. No push, release or operational-host/provider campaign occurred.
