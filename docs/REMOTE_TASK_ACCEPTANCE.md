# Remote task acceptance — 5 September 2026

This records local and real Ubuntu SSH qualification of the unreleased remote-task
implementation. It supplements the historical [fleet pilot](FLEET_ACCEPTANCE.md).
It does not claim a release, publication, provider-harness qualification, or hosted
CI. The scenario used disposable repositories and isolated Hydra homes, with actual
command output, Git commits, workflow attempts, and gate records.

## Qualification

- `make test-all` passed, covering shell contracts, fleet/task tests, C helpers, native TUI,
  PTY, parity, fresh-prefix/native installation, packaging, and onboarding.
- `make sanitize` passed, qualifying core, TUI, and fleet with supported macOS UBSan flags.
- On Ubuntu x86_64, strict C99/GCC builds and `make sanitize-fleet` with
  AddressSanitizer/UBSan passed. JSON-C development files were downloaded and
  extracted privately; no system package installation was needed.
- The task tests exercise concurrent acceptance, lost acknowledgments, source/input
  verification, unsafe paths, deadlines, cancellation, owner loss, immutable result
  sealing, tampered evidence, collection retries, dirty/linked checkouts, isolated
  ref movement, symbolic refs, and integration approval/promotion.
- Ubuntu qualification found and fixed checked ref-formatting truncation and
  private home creation under umask `002`. Existing shared homes remain refused;
  initialization never changes their permissions. That umask and refusal are now
  exercised by the task acceptance test. Teardown also removes only fixture-bound
  terminal instances when public cleanup refuses deliberately dirty worktrees; the
  affected tests passed again on macOS and Linux ASan/UBSan.

## Installed SSH path

Batch SSH used a private temporary alias, strict host-key verification, and no
connection multiplexing. The private address is absent from this document and
committed assets. The public `fleet package` and `fleet bootstrap` commands installed
and verified the Linux helper with its matching shell source. No global PATH or
existing installation was changed. The final tested immutable package pin is:

```text
682aa199aff7e35a0ee886b34bbbaf79088fa7e1bb49416a163f14dad66fdd30
```

The package was installed beneath the remote user's
`.local/share/hydra/fleet/PIN/`. Repeating bootstrap with the final pin succeeded; every installed file also
matched its packaged checksum. All task operations used the final
installation and a newly initialized private remote state directory under the
host's normal umask. Before cleanup, every remote C source/include file matched
the final local implementation.

These are the substantive public operations, with private/disposable paths shown
as variables. `build` was an explicitly registered alias; no placement was inferred.

```sh
hydra remote add build "$ssh_alias" --home "$remote_state"
hydra fleet package --source "$source" --binary "$linux_binary" --output "$package"
hydra fleet bootstrap build --input "$package" --sha256 "$pin"
hydra fleet init build --project "$remote_project" -- --no-agent --json
hydra fleet task prepare --source "$project" --spec "$spec" --output "$task_package"
hydra fleet task inspect --input "$task_package"
hydra fleet task submit build --input "$task_package" --key real-exec --trust-spec "$digest"
# The submitting process and its SSH connection exited here.
hydra fleet task status build --id "$task"
hydra fleet task logs build --id "$task" --source work --limit 1024
hydra fleet task result build --id "$task" --output "$snapshot"
hydra fleet task collect build --id "$task" --into "$local_repository"
hydra fleet task collect --input "$snapshot" --into "$local_repository"
```

## Observed execution and collection

The command task waited ten seconds, hashed the exact selected context file into
`result.txt`, printed its completion line, and committed the result. A fresh SSH
connection observed `succeeded` with real exec run `run_7ad0b52011ae7db7330e`.
Its source commit was `a2a1704ef3eb734dc836287b6986851c81cd5061`; its recorded result
commit was `06db040160574b3d2d46b2f45f23670ba2de9d46`.

The independently verified snapshot digest was:

```text
4ae802932c0bcd6a2872ab1020bb90bfafe3ccede9227318371a467e0c1ecf95
```

The artifact and `git show` of the collected isolated ref both matched the expected
input checksum bytes. Repeated online/offline collection returned the same bindings
and preserved a dirty tracked file, an untracked note, the index tree, ordinary
branch refs, and a `FETCH_HEAD` sentinel. Bounded work-log retrieval returned the
actual `remote command completed` line and its exact bytes.

A separate finite workflow used real spawn, exec, and gate steps. Run
`run_9a9bd6c9e060feb00ed2` succeeded; its result contained 41 evidence files,
including the gate record. Source `b07888e2bc4290d03256d12fe3f42eee9f5bd191`
produced clean result commit `950b19c2f61c0068053d9379f7d4f92908de9d8b`.
Its snapshot digest was:

```text
9c3e18f01dffd00b326db3dca63dfe9cc4abbaea531d38df21e1e0f45ec5a523
```

The existing local integration engine assembled that collected workflow commit and
ran `test -s result.txt`. It refused promotion without approval, then promoted the
exact result after explicit local QA approval. The integration run was
`run_0c9ed1c86726ccced166`; its disposable assembly worktree was cleaned up using the
public command. Remote gate evidence did not grant local approval.

## Failure and recovery evidence

- A temporary test transport wrapper forwarded the real SSH request, then discarded
  the receiver's successful acceptance response. The client returned
  `outcome_unknown`. Repeating the same package/key returned the original acceptance
  fields; inspection of the receiver found exactly one task for that submission.
  A changed valid package with that key returned `submission_conflict`.
- A controlled transport outage refused status and submission. Reconnection showed
  the same accepted task and unchanged acceptance count, with no launch or replay.
  This was an injected transport failure, not a physical network outage.
- Cancelling that queued task reported `cancelled`; subsequent start did not launch
  work. A separate running task lost its start response, reconciled without replay,
  and reached `confirmed_stopped` in 3.54 seconds with a three-second cancellation
  grace. Its external execution marker contained `once` exactly once.
- A deliberate crash stopped the task's known owner before terminating its exact
  child process group and owner. Reconnection reported `outcome_unknown` with
  `owner_unavailable`. Explicit start refused replay; its execution marker remained
  `once`. This qualifies refusal and reconciliation, not automatic reboot recovery.
- Copies of the real workflow snapshot with `../outside` as an artifact path or a
  wrong artifact checksum were refused before local collection writes. The outer
  checksum was recomputed in additional probes so the inner path/file checks were
  exercised. Existing C probes also reject altered bundle digests, foreign refs,
  unsafe evidence paths, and missing required execution evidence.

## Boundaries and release state

Task submission targets one explicit trusted host and an existing project mapping.
There is no automatic placement, cross-host graph, provider conversation transfer,
or replay after uncertain execution. Command/workflow success follows the declared
completion policy; it does not assert that every agent session has stopped or that
a reviewer approved the work. Dirty remote work remains collectable evidence but
cannot become an integration candidate without committed clean result heads.

The final immutable installation is retained for review. Acceptance-created
worktrees and terminal instances were removed and verified absent. Temporary
remote source, dependencies, task state, and the superseded test pin were removed;
unrelated installations and global configuration were preserved.
The demo is unchanged because this work adds CLI/task behavior, not native rendering.
Release-time version assignment, qualification of the eventual `main` merge and
release archives, hosted publication, and tags remain separate maintainer actions
under [VERSIONING.md](VERSIONING.md). No source or artifacts were pushed or published.
