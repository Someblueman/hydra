# Versioning and release sizing

Hydra continues to use Semantic Versioning. The public surface includes CLI syntax,
machine-readable JSON, durable state and event schemas, installation layout, and
documented shell behavior:

- patch releases fix defects without intentionally changing those contracts;
- minor releases add backward-compatible user-facing capability and may add a new
  explicitly migrated state contract;
- a major release is required for intentional incompatible contract removal or
  replacement.

The size of the backlog is not a reason to change versioning schemes. CalVer would
communicate release time but not compatibility, while a single ever-growing minor
milestone makes release risk and lead time worse. Going forward, roadmap themes are
planning containers rather than promises that every adjacent backlog item must ship
together. Each minor should contain the smallest independently useful vertical slice
that passes the release definition of done. Unready work moves to the next milestone
without renumbering shipped behavior.

Use `-rc.N` prereleases when packaging or platform qualification needs public testing.
Tags and hosted releases are cut only from the exact commit that passed shell-only,
native, parity, install, onboarding, and applicable security/recovery gates.
