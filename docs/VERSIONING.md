# Release policy

Hydra uses Semantic Versioning to describe compatibility, not to schedule work. The
public surface includes CLI syntax, machine-readable JSON, durable state and event
schemas, installation layout, and documented shell behavior:

- patch releases fix defects without intentionally changing those contracts;
- minor releases add backward-compatible user-facing capability and may add a new
  explicitly migrated state contract;
- a major release is required for intentional incompatible contract removal or
  replacement.

2.0.0 is the final version assigned in advance on the roadmap. After 2.0, the roadmap
is one prioritized backlog rather than a sequence of numbered release milestones:

- work starts from the highest-value ready backlog item, not from a target version;
- a release is cut when a coherent major feature or meaningful change is ready and
  passes the release definition of done;
- the version is chosen at release time from compatibility impact: patch for fixes,
  minor for compatible capability, and major for incompatible public-contract change;
- unrelated backlog items do not wait for or get pulled into an artificial release
  train.

A large feature does not automatically require a SemVer major release. Compatibility,
not implementation size, determines the version number.

Use `-rc.N` prereleases when packaging or platform qualification needs public testing.
Tags and hosted releases are cut only from the exact commit that passed shell-only,
native, parity, install, onboarding, and applicable security/recovery gates.
