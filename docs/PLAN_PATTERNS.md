# Bounded decomposition patterns

`examples/planning/patterns/serial.json` and `forkjoin.json` are reusable
schema-1 plans for a fixed two-member decomposition. Copy the complete
`examples/planning/patterns/` directory into the root of a new disposable Git
repository before compiling. Build `make build-plan-example` and copy
`build/plan-example` into that repository before committing its source. The source scripts, plan, policy, and data are
therefore bound to the copied repository commit.

Both plans have the same objective, policy, envelope, deliverable, requirement,
check, source scripts, and artifact declarations. They differ only in the
necessary dependency edge: serial `work-b` needs `work-a`; fork/join lets both
workers proceed after their own spawn and joins both artifacts at `compose`.
The member set is finite and fixed at two. There is no dynamic join, provider,
or unbounded task creation.

Each worker produces one bounded artifact. The composer consumes both artifacts
through explicit step outputs and emits one report. The checker consumes that
sealed report and independently compares both lines, returning a structured
object. A missing artifact reference is a contract error and is covered by
`tests/native/test_plan_patterns.c`; a process exit alone is not acceptance evidence.

Compile and inspect copied plans with:

```sh
hydra workflow plan validate serial.json policy.json
hydra workflow plan compile serial.json policy.json ../serial.compiled.json
hydra workflow plan compile forkjoin.json policy.json ../forkjoin.compiled.json
hydra workflow plan explain ../serial.compiled.json
hydra workflow plan compare ../serial.compiled.json ../forkjoin.compiled.json
```

The two alternatives must be compiled from the same copied source repository
for a matched comparison. Execution uses separate disposable repositories so
existing head names cannot conflict. Each member is a headless task; the checker
compares every byte of the assembled report against a fixed independent expected
value and emits schema-3 evidence. A failed content check is valid negative
evidence, distinct from a checker that could not execute.

The fixed held-out artifact inventory contains one correct report and six wrong
reports: wrong content, a missing member, reversed order, truncated final newline,
extra bytes and empty content. The observed false-accept count is 0/6 and the
false-reject count is 0/1. These deterministic cases establish this byte-level
contract only; they do not estimate performance on arbitrary planning tasks.
`tests/native/test_plan_patterns.c` also exercises complete node explanation, same-source
public comparison and missing-artifact rejection.

Public workflow qualification (10 September 2026):

| Case | Run | Accepted plan SHA-256 | Result gate |
| --- | --- | --- | --- |
| serial | `run_6d2c345507ef21da2a7b` | `73660819318a56d6ec22f395da2c4a4f20c669d5f63e908a0c90be40dd3aa33c` | pass |
| forkjoin | `run_89ef62a2e3d626c3708d` | `8bc8c6d3fcc319a730a8bb7154271d8d2647a2f0cd99b44fa6e23cb8e7d54cf9` | pass |
| wrong | `run_4ec8d07f818769928259` | `4598c0d1a4a30e7f1a198b7ba11b971116f276800ed470ee117c8176ed19c533` | rejected |

Exact retained evidence: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-patterns-public-j_d07m2j/summary.json`. The wrong-composition process exited successfully; the independent checker failed and the public result gate refused acceptance.

The [finite manifest pattern](PLAN_MANIFEST.md) additionally lowers up to eight
members and boolean selection into a static plan, including checked empty maps
and explicit skipped-member records.

No timing advantage, semantic planning improvement or calibrated rework estimate is claimed. These are finite static patterns and a reproducible deterministic comparison baseline. A new scientific question belongs in a separately compiled plan with its own acceptance; it is not a retry of the old claim.
