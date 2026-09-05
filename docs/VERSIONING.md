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

## Rolling delivery

Keep `main` releasable and deliver one independently useful backlog slice at a time.
There is no scheduled release train and no requirement to publish every merge.
Use [the reusable release checklist](RELEASE_CHECKLIST.md) when a slice is ready.

During development, describe changes under `Unreleased` in the changelog. At release
preparation, choose the version from the accumulated compatibility impact, update
all executable version handshakes together, and finalize the notes. Do not infer
compatibility solely from commit titles or the number of changes.

Qualify the resulting merge commit on `main`, even when the PR checks passed.
Prepare a draft release with all archives and checksums before publication. Prefer
GitHub release immutability so published tags and assets cannot drift; configure it
explicitly in repository settings before the next publication. An incorrect
published release is corrected by a new version, never by replacing its assets.

Stable users install a numbered release. Contributors may run `main` from source,
but a passing development commit is not a published stable release. Use a numbered
release candidate when wider testing is needed, and qualify the final stable
commit after replacing prerelease version strings.

Publication remains an explicit maintainer action. Automated qualification and
artifact preparation must not grant ordinary PR workflows release-write access.

## Deprecation after 2.0

A public CLI option, environment variable, JSON field, protocol, or documented
on-disk contract may be deprecated only in a released version. The announcement must
name the replacement, compatibility impact, and earliest removal release.

- A compatible public interface receives at least one minor-release window before
  removal and remains functional throughout that window.
- A security flaw or integrity risk may require immediate removal. The release notes
  must explain the exception and provide the safest available migration.
- Provisional interfaces explicitly labeled internal or experimental may change
  without a window; that label must exist before users depend on the interface.
- Durable formats are never abandoned in place. A removal release includes a
  verified migration and rollback path from the immediately preceding stable
  release.

Hydra does not add pass-through aliases, dual writers, or zombie decoders merely to
stage a removal. During a declared window, the existing implementation remains the
single path; the removal release replaces it atomically.
