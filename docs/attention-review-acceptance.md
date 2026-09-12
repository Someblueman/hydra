# Attention and review acceptance

I2/I3 are locally qualified as of 12 September 2026: their focused behavior,
bounded attention measurements, public CLI/TUI parity and combined repository
validation are accepted. This note records that scope; it does not claim
universal precision/recall, an idle budget, provider or platform parity, or
release readiness. Item 10's broader performance matrix and T2/T3 remain open.

## Reproduce the public routes

Attention is read-only and emits a 19-field wire record containing exact
task/attempt identity, source, freshness, route, and semantic revision. Extract
the 13 named selection fields from that record before invoking an exact review:

```sh
output_parent=$(mktemp -d "${TMPDIR:-/tmp}/hydra-attention.XXXXXX") && make build-tui build-fleet && python3 scripts/bench-attention.py --tui build/hydra-tui --fleet build/hydra-fleet --out "$output_parent/run"
```

The public routes exercised by the harness are:

```sh
hydra workflow attention --json
hydra workflow attention-data
hydra fleet attention --json
hydra fleet attention-data
hydra workflow review-data KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD INSTANCE REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256 [REFERENCES_JSON]
hydra fleet review-data KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD INSTANCE REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256 [REFERENCES_JSON]
```

`review` returns one versioned JSON envelope. `review-data` returns one bounded
framed document ending in `END` and EOF. Exact identity, requested revision, and
identity hash bind the selection; stale, changed, missing, ambiguous, expired,
or unavailable subjects remain explicit. Review and supplied-reference handling
are read-only. Local previews are capped at 4096 bytes; the producer leaves URLs
unopened, while native `o` explicitly passes a selected URL to the operating
system opener without ingesting its content.

In the native TUI, `I` opens attention, `j`/`k` or arrows select, `Enter` shows
detail, `s` marks a revision seen for that client, and `r` opens exact review.
Within review, `i` shows identity, `f` references, `o` follows a selected
reference, `j`/`k` scroll, and `Esc` backs out. Seen state never approves or
accepts a workflow. A stale snapshot must be refreshed before review.

## Accepted bounded measurement

The final report records 9 corpus cases: 6 supported positive cases, 2 supported
negative cases (`working-quiet` and `provider-done-without-result`), and 1
unsupported/unknown case. Exact totals are **17 TP, 0 FP, 0 FN**, with unknown
**1/1**. Class denominators are approval **11/11**, expired approval **1/1**,
result **5/5**, and unknown **1/1**. The two supported negative cases produced
zero alerts. No attention event approves a gate or accepts an outcome.

The timing phase retained all **24 scheduled/sent** stimuli and **22 observed**
complete matching frames. There were no missed stimulus slots; two review-load
observations were censored at the declared deadline. Maximum scheduled-to-sent
lateness was **5.110 ms**, with none over 10 ms.

| Transition | Samples | Observed | Median ms | Range ms |
| --- | ---: | ---: | ---: | ---: |
| changed approval revision | 3 | 3 | 222.729 | 213.657–311.042 |
| changed result revision | 3 | 3 | 260.962 | 218.156–262.960 |
| offline | 3 | 3 | 132.009 | 86.980–136.858 |
| review close | 3 | 3 | 84.606 | 26.298–85.467 |
| review open identities | 3 | 3 | 0.519 | 0.223–1.089 |
| public evidence load | 3 | 1 | 3824.707 | 3824.707 |
| review return evidence | 3 | 3 | 0.695 | 0.651–0.781 |
| same-client reconnect | 3 | 3 | 298.403 | 221.241–485.749 |

The single observed public-load value is not a completed three-sample latency
distribution; p95/p99 are unavailable. The two censored observations remain in
the report and are not discarded.

Six approximately six-second CPU windows recorded direct TUI CPU **0.03–0.04 s
per window**, observer CPU **0.1017–0.1106 s**, receiver-waited CPU
**1.4105–1.4543 s**, and **18 completed transport calls per window**. Full
producer-subtree CPU is **null** because short-lived helpers were missed by
`ps`; sampled descendant counts are lower bounds. The windows ran on a shared
loaded machine, so they do not establish an idle budget or speedup.

Final native public-frame parity counts were: Fleet retained result **833/833**
wide and **19 critical** narrow; Fleet approval **73/73** wide and **13
critical** narrow; local artifact result **50/50** wide and **10 critical**
narrow; local pending request **75/75** wide and **16 critical** narrow. Native
frames were reviewed for status-first readability, exact identity, references,
terminal restoration, and cancellation cleanup.

## Combined repository validation

Passing evidence covers all **23 `make test-all` prerequisites** and all
**7 `make sanitize` prerequisites**. This combines completed, source-matched
earlier checks with the remaining checks and affected-case reruns; it is not a
claim that either earlier interrupted top-level command exited successfully.
The local target-to-log inventory is
`/Users/sws/Code/hydra/build/i3-acceptance-cleanup/INVENTORY.md`, with exact
commands, environments, exit codes, source/runtime hashes and coverage in the
adjacent JSON records.

The shell inventory covers all 61 currently eligible scripts: 53 completed
before the earlier interruption, followed by 8 complete passing runs. The
previous workflow E2E errors followed termination and fixture removal; its new
complete run passed 12/12. Full lint passed after the final fixture repair.
Normal Fleet coverage is 57/57 cases, and native PTY coverage is 284/284.
Parallel workflow-plan variants exposed a real test-fixture collision on the
shared `plan-smoke` tmux session. Each interactive fixture now uses its own
socket. All three affected variants passed together in normal and UBSan runs;
the failed run and its diagnostics remain in the inventory.

These checks qualify product source at `aeba5490f802c451dc1eff2125f8b1d845fab6c1`
plus the isolated workflow-plan fixture repair (SHA256
`d918e70d4c0bfe7302e1c1401a969ee07aa4431af6256b38e5285b01eed8191f`). The final
normal TUI is
`ac24ad05be1dfe9efe82e5fb4ba45037c01127e77a8719db4a513d4c9b484128`; Fleet
remains the measurement binary identified below. Earlier public parity and
measurement evidence remains applicable to attention/review: the later native
change only preserves explicit workspace root/run selection during asynchronous
head arrival and collapse. Normal and UBSan attached/plan-workspace tests cover
that change. Full C quality and its final workspace delta check passed; existing advisory
findings remain recorded.

## Provenance and limits

The final report SHA256 is
`8966701ebb7cad86c54d58ed91f0bcfe8dec18bdfdef2f7cae70eb0b686dad93`; its JSON
companion is
`04a36080a3b788b92397fabded78d6a91b1f7e6666fe60b93038499c9d1d7a1f`. The
measurement source snapshot is
`5d441d23aceb5a62b9a55fc7a93cd2c09d905dff0a9e79ba7683ba8ac58c08fa`; final
overhead sources are bench
`1b51049fb8fb9986fd1281482467adda04372234de9e37671ac914c65d9236bb`, PTY
`d7a954175de62866c5dd58ac173c4fd41818ab981c9d5e5cb5b5f5753693821b`, native
TUI `cd894a2c94f4471a7e1508cd1f0a5ecb33cd7c22cafb597982bbdd811b3caee1`, and
Fleet `1206cac4090f210f695e37c38f11f02a214f9e30a5c6d8f34588802ae16a082d`.
The base checkout was `f74aa9d68ff59b60b07478002ebb2ba02f060b16`; these qualified
dirty-source snapshots and hashes are the evidence identity, not that base alone.

The full raw reports and fixtures remain local artifacts at
`/Users/sws/.codex/team-leader/01a08fe3-16c1-7212-8401-cb2c1ea37c8d/i2-measurement/final-bounded-report`
and the adjacent measurement phase; they are not durable public URLs. They record fixture-generated
input and controlled local transport; they do not establish authenticated
provider behavior, live SSH, remote platform parity, the item-10 1/10/50 matrix,
or T2/T3 qualification. The accepted attention metrics exercised the attention
view; workspace initial selection was not part of that measurement. Completing
the repository validation does not widen those measurement claims.
