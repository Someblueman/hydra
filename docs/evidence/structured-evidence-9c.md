# Structured verifier evidence

Schema version 3 reports are the machine-checkable extension of the accepted
plan report contract. Version 1 and 2 reports remain readable for existing
plans; an obligation requiring measurements cannot be satisfied by downgrading
to a narrative-only legacy report.

A v3 report contains `execution_status`, `evidence_status`,
`domain_verdict`, and the compatibility `verdict`, plus the exact
`subject_sha256` and the accepted `validator_sha256`. Its `evidence_records`
array has one record per obligation. Each record binds the obligation to the
subject manifest, validator identity and the hash of the accepted validator
recipe, invocation, execution environment, an explicit case inventory, raw
observations, and a hash of the canonical observations.

For executable v3 checks, the accepted check definition is a structured recipe
with predicate `equals` and a fixed list of case IDs and expected values. Each
observation only supplies its case ID and sealed raw payload; the adapter
extracts `raw.actual`, compares it with the accepted expected value, and
recomputes the raw hash. The trusted adapter recomputes each outcome,
executed/failed counts, and exact recipe case coverage; report supplied
predicates, expected values, and inventories cannot override it. A
pass requires completed execution with exit code zero, valid evidence, no
failed or skipped observations, and a domain verdict derived from those
observations.
Missing or stale bindings, duplicate or incomplete obligation records,
inconclusive execution, and changed raw observations invalidate the report.
When an obligation declares `measurements` as required evidence, at least one
observation must carry a measurement value; prose cannot replace it.

Assessment-specific rubric, source locators, disagreement, and acceptance
authority belong in the `reviewer_decision` object of the corresponding
record. The verifier preserves that material for review and does not treat
additional judges, models, or hosts as independent evidence by themselves.

The collector remains part of the trust boundary: hashes identify retained
observations, while the accepted recipe and executed verifier determine what was
actually checked. The equality adapter is deliberately bounded; its controls do
not establish arbitrary semantic correctness. Assessment reports preserve human
or external reviewer decisions and do not provide an automatic LLM judge.

Local qualification on 9 September 2026 passed the native plan tests, UBSan,
and all fourteen public workflow controls. These cover a correct candidate,
wrong candidate bytes, omitted observations, raw tampering, a stale subject,
missing measurements, a changed recipe, a malformed recipe, nonzero execution,
and assessment pass/fail/inconclusive, missing authority, and stale rubric.
Native controls separately reject inverted PASS claims, dropped failure counts,
unsupported predicates, duplicate recipe cases, and malformed review metadata,
with valid surrounding bindings and positive controls after restoration.
Full C analysis passed with 187 advisory functions and no baseline increases.
A focused independent review closed the executable and assessment findings.
