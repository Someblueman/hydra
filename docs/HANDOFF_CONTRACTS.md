# Bounded handoff contracts (9B)

Workflow data **schema 2** opts into semantic shape and relational checks. Both
plan schema 1 (local) and plan schema 2 (task) can carry it. Data schema 1 retains
its published type/size/hash behavior, receipt format, compiler name, compiled
projection and acceptance digest. No contracts or relation metadata are injected
into old plans. An existing compiled plan still requires its exact accepted hash
and a matching recompilation.

## Producer and consumer declarations

Every ordinary schema-2 input and output declaration has a `contract`. Every
ordinary consumer reference repeats that **exact** contract, including schema ID,
version, type, cardinality, field names, units and predicates. Object member order
is immaterial; differences are rejected at compilation/`workflow validate`.
There is no subschema implication, coercion, implicit unit conversion or inference
from natural-language schema names. Unknown keys and unsupported constraints fail.

```json
{
  "path": "measurement.json",
  "type": "object",
  "max_bytes": 4096,
  "contract": {
    "schema": "measurement",
    "version": 1,
    "type": "object",
    "cardinality": {"min": 1, "max": 1},
    "fields": {
      "candidate": {"type": "string", "equals": "candidate-7"},
      "duration": {"type": "integer", "unit": "ms", "minimum": 0}
    }
  }
}
```

The corresponding value is
`{"candidate":"candidate-7","duration":{"value":120,"unit":"ms"}}`.
Every field is required; extra fields, empty objects, duplicate JSON members,
missing values, incorrect types/units and failed predicates are rejected.
Supported field types are `string`, `strings`, `integer` and `boolean`. Strings
are nonempty, at most 256 bytes and contain no embedded NUL. `strings` means
1–64 such strings; an `equals` list can pin required evidence IDs. Integers are
signed 64-bit. `equals` supports the field's exact type; `minimum` and `maximum`
apply only to integers. A unit is supported only on an integer and requires the
exact `{value, unit}` representation above. Units are author-pinned labels;
Hydra does not prove their scientific meaning.

Root types are `object`, `array`, or `file`. `array` contains 1–1024 objects with
the same required fields and explicit `min`/`max` cardinality. Objects and files
have cardinality exactly one. Files use an empty `fields` map and retain their
byte bound and optional input SHA-256 pin: that is an opaque byte contract, not
validation of an arbitrary file format. Existing byte limits still apply.
`validation: plan` and `repair: plan` inputs retain their separately versioned,
executor-generated contracts; data 2 does not reinterpret their content.

Producer sealing checks actual values before recording success. Consumer
materialization revalidates copied values against the producer declaration and
sealed receipt before launching an exec command or submitting a task. Data-2
attempts additionally record `contract-inputs.json`, containing the hashes of all
materialized inputs, including generated provenance. After-execution checks
reject changed or missing input snapshots. Failed sealing leaves diagnostic
artifacts but never makes the producer authoritative.

## Cross-input and composition invariants

A data step may declare 1–16 `invariants`. Selectors identify a named input or
output and one top-level field. `before` rules can reference inputs only; `after`
rules can also reference sealed outputs. Selected schemas must have the same
field type and unit. The supported operations are exact `equal`, and integer
`sum` with 2–16 left selectors. Arithmetic overflow fails closed.

```json
{
  "invariants": [{
    "phase": "after", "op": "sum",
    "left": [
      {"input": "part_a", "field": "duration"},
      {"input": "part_b", "field": "duration"}
    ],
    "right": {"output": "composed", "field": "duration"}
  }]
}
```

After rules run at sealing and output verification; a valid individual component
does not waive the composed object's schema or relational checks. Predicates
outside this finite set, nested selectors, arbitrary scripts and logical schema
implication are unsupported. Add a normal independently checked work/verification
node when the required semantic judgment is outside this contract.

## Explicit conversion nodes

An existing `exec` or `task` step can declare one `conversion`:

```json
{"input":"milliseconds", "output":"seconds", "field":"duration",
 "numerator":1, "denominator":1000}
```

The step's normal command produces the declared output. Both sides must be
object contracts, with identical field sets and identical other field contracts;
the selected field must be an integer with an explicit unit on both sides.
The schema name/version and selected field predicates may differ. Ratios are
positive integers no larger than 1,000,000. Sealing verifies exact integer
`input * numerator / denominator`, rejects overflow and nonzero remainders,
and checks that every other field value survived unchanged. Output predicates
are checked as usual. Thus 1500 ms cannot silently become 1 s. This validates the
explicit numeric transformation; it does not establish that the chosen ratio or
unit labels are appropriate for the domain. No converter registry or automatic
rewriting is involved.

## Candidate manifests and provenance

A generated input `{"provenance":"plan"}` contains `source_commit` and
`source_sha256` from the accepted compiled source binding. A generated input
`{"provenance":"step","step":"producer"}` requires a direct task predecessor
and reuses the existing verified collection/result/parent-receipt chain. It
requires one clean collected head. Here `source_commit` is that head's commit,
and `source_sha256` identifies the verified result envelope that binds its source
bundle, heads, artifacts and receipt. This is not the same hash namespace as the
plan's bounded source fingerprint. Both provenance forms are two-field objects;
they are generated by the executor, not supplied by a producer's claim.

A candidate manifest is an ordinary object output with a pinned contract. It
contains `source_commit`, `source_sha256`, and named SHA-256 string fields for
all selected dependency/build inputs, configuration and artifacts. For example,
a consumer can add:

```json
{
  "inputs": {
    "source": {"provenance":"plan"}
  },
  "candidates": [{
    "phase":"before", "manifest":"candidate", "source":"source",
    "bindings": {
      "dependencies_sha256":{"kind":"dependency","input":"dependencies"},
      "config_sha256":{"kind":"configuration","input":"configuration"},
      "artifact_sha256":{"kind":"artifact","input":"program"}
    }
  }]
}
```

The omitted ordinary `candidate`, `dependencies`, `configuration` and `program`
references must have exact contracts. `before` validates an input manifest;
`after` validates an output manifest and permits artifact bindings to outputs.
Each input except the manifest and generated source must be covered exactly once;
after rules must also cover every output except the manifest. The manifest has
exactly the two source fields plus the binding fields, all strings. Binding to
itself, incomplete coverage and unknown binding kinds are rejected. Changing any
selected bytes or source identity invalidates it. This proves coverage of the
**declared** candidate, not that the author declared every environmental influence
or chose a semantically correct candidate. Build dependencies must be selected
explicitly; no ambient dependency scanner or hermetic build claim is made.

## Typed planning relations and enforcement

Optional plan `relations` contain `{type, from, to, enforcement}` records.
`data`, `evidence`, `effect`, and `order` require `enforcement: dependency` and an
explicit direct edge in the consumer's existing `needs`; those edges lower to the
existing DAG unchanged. Actual artifact references and required evidence joins
retain their separate validation. `provenance` and `resource` support
`enforcement: descriptive` only and do not add execution prerequisites. This
bounded contract does not implement resource mutexes: requesting `mutex` or any
other enforcement mode fails rather than manufacturing a permanent ordering.
Normal write-conflict checks and execution admission limits remain in force.

Declarations and receipts do not establish OS isolation, determinism, safe
repetition of external effects, arbitrary semantic correctness or adversarial
protection against the same local account rewriting runtime state.

## Acceptance

`make test-workflow-contracts` builds the native helper and runs the public plan
CLI, native materialization/sealing boundary cases, and real supervised workflow
cases. The supervised cases reuse one throwaway head and verify consumer command
markers and absence of launch records on rejection. The checked-in
`tests/fixtures/plan-9b/data.json` is the compatible measurement contract.
Tests cover missing semantic fields, unit mismatch, stale candidate identity,
lossy conversion, individually valid but incompatible components, compatible
success, source/configuration binding, incomplete candidate coverage, and legacy
opt-in boundaries. The structured fixtures are native C programs built with JSON-C.

Local fixtures and the existing receipt checks do not replace qualification on
an external host or live provider. That qualification remains explicit and is
not claimed by this milestone's local acceptance.
