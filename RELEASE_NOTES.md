# Hydra v2.0.0 Release Notes

Release date: 2026-09-04

Hydra 2.0.0 establishes the stable local orchestration interface. Native mission
control is now the default TUI when available, the basic shell TUI remains the
compiler-free fallback, and state v2 is the sole runtime authority.

## Highlights

- `hydra tui` launches native mission control by default, with visible fallback to
  `hydra tui --basic` for missing, unsuitable, crashing, hanging, or version-skewed
  native binaries.
- Native mission control now covers structured spawn, keyboard help, dashboard
  actions, search, branch-stable multi-selection, group assignment, and confirmed
  bulk kill through public shell commands.
- State-v2 project, head, and instance records replace runtime reads and writes of
  the 1.9 compatibility maps across session, lifecycle, messaging, limits,
  maintenance, dashboard, completion, and TUI surfaces.
- Stable contracts define JSON success and error envelopes, lifecycle and workflow
  evidence, native/basic parity, supported platforms, upgrades, deprecation, and the
  consolidated local trust model.
- Migration provides dry-run validation, full backup, verified removal of 1.9
  projections, interruption safety, and explicit rollback for intentional downgrade.

## Upgrade from 1.9

```sh
hydra state migrate --dry-run
hydra state migrate
hydra tui
```

Use `hydra tui --basic` to select the shell fallback explicitly. Use
`hydra state rollback` only when intentionally downgrading to 1.9 after reviewing the
recorded backup.

## Compatibility and safety

The POSIX shell CLI remains the sole mutation authority. Native core protocol v1 and
native TUI protocol v2 remain unchanged, with exact 2.0.0 release handshakes. Hydra
2.0 removes the runtime compatibility-map fallback, basic-TUI tag storage and tag
actions, the one-key kill-all shortcut, preview-follow toggle, and redundant
`hydra tui --native` mode.

See [docs/MIGRATING_TO_2.0.md](docs/MIGRATING_TO_2.0.md),
[docs/CONTRACTS.md](docs/CONTRACTS.md), and
[docs/RELEASE_CHECKLIST_2.0.0.md](docs/RELEASE_CHECKLIST_2.0.0.md) for the migration,
stable interfaces, and exact-candidate release boundary.
