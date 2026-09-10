# Bounded decomposition patterns

`examples/planning/patterns/serial.json` and `forkjoin.json` are reusable
schema-1 plans for a fixed two-member decomposition. Copy the complete
`examples/planning/patterns/` directory into the root of a new disposable Git
repository before compiling. The source scripts, plan, policy, and data are
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
`tests/test_plan_patterns.py`; a process exit alone is not acceptance evidence.

Compile and inspect copied plans with:

```sh
hydra workflow plan validate serial.json policy.json
hydra workflow plan compile serial.json policy.json ../serial.compiled.json
hydra workflow plan explain ../serial.compiled.json
hydra workflow plan compare ../serial.compiled.json ../forkjoin.compiled.json
```

The included tests run compile, explain, dependency-equivalence, and a known
missing-artifact rejection in disposable repositories. They are structural and
functional checks only; no timing result or performance claim is attached to
these synthetic members. Any later comparison must use a separately authorized
measurement window and retain raw observations.
