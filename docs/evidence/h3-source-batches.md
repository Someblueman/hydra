# H3 source batches and bounded enrollment evidence

This note records local fixture evidence for the H3 source and batch changes.
It does not claim acceptance against external providers or live hosts.

The enrollment regression `test_fifty_host_apply_advances_in_sixteen_host_batches`
uses 50 targets in an inventory fixture, an explicit SSH configuration, and one
absolute `--progress` export path. Qualification is resumed through bounded
16-host batches. The test interrupts a public qualification process and resumes
the same private progress authority, then reviews the exact completed export;
peer fingerprints come from the fake SSH diagnostic path (`SHA256:fixture`) and
are not edited into the export.

The same test drops one initialization response, verifies the first apply leaves
`outcome_unknown`, and reconciles it on retry. It interrupts a later apply
batch during preflight, resumes it, and checks that the final 50 rows are
enrolled. A duplicate apply produces no new mutations. The fixture records 50
`init` effects and checks one completed durable receiver operation record per
target.

Verification performed in the H3 worktree:

- `python3 tests/test_enrollment.py -k fifty`: 1 test passed.
- `python3 tests/test_enrollment.py`: 17 tests passed.
- `python3 -m unittest discover -s tests/discovery -p 'test_*.py'`: 11 tests passed.

These checks exercise the local fake transport and receiver. They are bounded
qualification and enrollment fixtures, not full provider, jump-host, or
external SSH integration coverage.
