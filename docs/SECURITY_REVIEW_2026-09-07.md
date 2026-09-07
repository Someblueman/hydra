# Manual security review — 2.2.1 candidate

This review examined the source at `d9ee967` and the fixes in this branch.
It used local source inspection, public CLI regression tests, ShellCheck,
clang-tidy and repository acceptance. It is a scoped manual review, not a Codex
Security deep scan, exhaustive audit, or claim that no other vulnerabilities exist.

## Confirmed findings and fixes

### HSEC-01: repository trust omitted executable configuration

`project_config_hash` excluded every file named `local.yml`, including repository
workflows, and enumerated only regular files, silently omitting symbolic links.
An attacker able to change repository content after the user's approval could
change `.hydra/workflows/local.yml` or add a linked workflow while retaining the
recorded trust hash. The workflow loader accepted those definitions under the old
approval. Setup and workflow commands run with the invoking user's permissions;
this bypassed the intended content-approval boundary.

The hash now exempts only root `.hydra/local.yml`, rejects linked configuration
and special files, validates names before line serialization, uses deterministic
sorting and propagates enumeration/read/hash failures. The public regression
checks that approved content works, changed nested `local.yml` is refused,
new linked workflows are refused and the intended host-local exemption survives.

### HSEC-02: initialization wrote through a linked configuration root

`hydra init` followed a repository `.hydra` directory symlink when writing
`config.yml` and `local.yml`. A crafted repository could therefore redirect those
writes into another directory writable by the invoking user. This is limited to
initialization's fixed configuration filenames; it is not an unrestricted remote
file-write endpoint.

Initialization now rejects a linked `.hydra` root before creating project state
or writing configuration. A regression preserves a sentinel in the linked
destination and verifies that failed initialization leaves it unchanged.

### HSEC-03: terminal input budgets could expose pasted keys

The native TUI stopped discarding a bracketed paste or CSI report after a byte
or time budget, then interpreted remaining bytes as keyboard commands. The time
check also expired on a second-boundary crossing rather than one elapsed second.
A real PTY reproduction with more than 8192 pasted bytes and a trailing `q`
exited the old TUI. This requires terminal input; it is not a network endpoint.

Discard state now persists across bounded input batches until the terminator,
and elapsed time accounts for nanoseconds. The terminal enables bracketed paste
while active and disables it on restoration. Three deterministic parser tests
cover second-boundary timing, oversized paste and oversized CSI; a real PTY test
checks that pasted `q` remains inert and the following normal key still works.

## Reproduction and verification

`tests/test_project_trust.sh` creates an isolated Git repository and Hydra home,
uses the shipped valid workflow example, approves it through `hydra init --trust`,
and exercises the public `workflow validate` and `init` commands after mutations.
It launches no agents and changes no real project state. Run:

```sh
sh tests/test_project_trust.sh
```

The same test copied into a `git archive d9ee967` source snapshot produced
**3 passes and 9 failures**. The fixed source produced **12 passes and 0 failures**.
The tests reproduce trust acceptance and redirected initialization writes; they
do not run a destructive payload or claim an independently demonstrated remote
code-execution exploit. The ability of accepted workflows to run commands follows
from the existing workflow execution contract and implementation.

Local qualification passed `make test-all` (including lint, fleet, native, PTY,
parity, installation and onboarding), `make quality-c` (124 above-threshold
functions with no ceiling regressions), and `make sanitize` with macOS UBSan.
The final TUI changes also passed focused native, PTY and UBSan parser checks.
The final 12-case trust regression and 40-case installation suite also passed
separately, with the latter exercising the 2.2.0-to-2.2.1 version upgrade marker
and preservation of user state. Linux/hosted qualification belongs to the PR
checks; local UBSan is not Linux ASan coverage.

## Other boundaries inspected

| Boundary | Inspection and result |
|---|---|
| Shell setup, YAML, templates and workflow loading | Traced repository trust checks and explicit shell execution; fixes above address the confirmed omissions. |
| Fleet SSH and request dispatch | Inspected target validation, literal argv, shell quoting and request allowlists. Strict host-key checking and noninteractive authentication remain enabled. |
| Credential transfer/storage | Inspected preview binding, post-transmission response filtering, ownership/mode checks, no-follow descriptor traversal and atomic store updates. No additional confirmed finding. |
| Task source, input and collection paths | Inspected path component rejection, bounded transfer sizes, descriptor-relative input/collection access and acceptance binding. No additional confirmed finding. |
| Config/history bundles and pinned bootstrap | Inspected path allowlists, staged writes, package digests and immutable destination checks. Package identity still requires a trusted source; hashes are not publisher authentication. |
| Native JSON and terminal rendering | Inspected input/depth limits, string validation, plan duplicate-member checking and terminal control-byte filtering. Terminal input follow-up confirmed HSEC-03 above. |
| GitHub Actions | Inspected workflow triggers and interpolation. Live repository default token permissions were read-only, with PR review approval disabled. No permissions were changed. |

This does not audit third-party dependencies for current CVEs, test live remote
hosts or provider accounts, or prove resistance to a malicious same-user process
racing filesystem operations. That process already lies outside Hydra's documented
isolation model. C analysis retains the previously documented advisory diagnostics;
a passing complexity gate is not a warning-free or vulnerability-free verdict.

## Upgrade and release scope

The candidate is a patch release: public CLI syntax, state v2 and native protocols
remain unchanged. Refusal of linked configuration is an intentional security
tightening. Replace links with reviewed regular files and run `hydra init --trust`
again where the corrected hash invalidates an old approval. Root host-local
`.hydra/local.yml` remains excluded.

The PR also contains the preceding internal simplification commits. Final release
publication requires successful hosted checks and requalification of the merged
`main` commit under `docs/VERSIONING.md`. Candidate archives must not be represented
as an already published stable release.
