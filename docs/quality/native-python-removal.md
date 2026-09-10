# Native test and example review

The September 2026 interpreter removal extends the existing Clang-Tidy 22.1.8
gate to `tests/native`, `tests/fixture`, `tests/termviz` and
`examples/planning/native`. The default cognitive-complexity threshold remains
15. The baseline adds individual reviewed functions in these directories; no
existing function ceiling or analyzer check is relaxed.

The CLI and PTY drivers retain explicit sequences of independent assertions.
Their fail-fast `CHECK` macros contribute a loop and conditional at each call,
including inside bounded case loops. Keeping these checks beside the operation
and its expected result makes failed protocol steps identifiable. The largest
PTY sequences exercise attached execution, recovery, approval and exact controls.
They are not production dispatch paths.

The subprocess helpers were reviewed for bounded capture, nonblocking I/O,
timeouts, process-group termination, interrupted waits and descriptor ownership.
The separate terminal observer retains ANSI/UTF-8, screen-cell and color checks;
it does not derive expected output from Hydra's renderer. `tv_read` has one
line-local analyzer suppression for its terminator: every caller supplies the
actual array capacity, `fread` is limited to capacity minus one, and explicit
checks enforce positive capacity and a bounded returned count.

The example checkers retain closed-schema, source/hash, protocol, raw-data and
result checks. Their complexity follows those independent rejection conditions.
The performance checker recomputes estimates independently of the analyzer;
the research checker retains a separate scheduler. Review found and repaired
null-subject handling, malformed CSV handling, integer conversion overflow and
embedded-NUL output acceptance. Negative controls preserve these boundaries.
No performance campaign or speedup claim follows from synthetic controls.

The fixture helpers preserve the former shell-test assertions and fail closed
on malformed or oversized input. The earlier precompiler files are now included
in the same gate. Module splits follow existing fixture and example boundaries;
the baseline is not permission for subsequent complexity growth.

Coverage mappings are maintained in [native tests](../../tests/native/README.md)
and [PTY tests](../../tests/termviz/README.md). Final integrated results are recorded
separately from this review rationale.
