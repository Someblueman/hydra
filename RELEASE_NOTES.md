# Hydra v2.1.0 Release Notes

Release date: 2026-09-05

These notes describe the published v2.1.0 release. New source-branch additions
are recorded under [Unreleased in the changelog](CHANGELOG.md#unreleased); they
are not part of v2.1.0.

Hydra 2.1.0 adds trusted remote fleets and disconnected task execution, with
verified result collection and local integration. Native mission control gains
clearer layouts, themes, and mouse navigation.

## Highlights

- Manage trusted SSH hosts with pinned installation, capability negotiation,
  bounded observations, reconnect/reconcile, and instance-bound actions.
- Submit an exact source commit and selected inputs, disconnect while a task or
  finite workflow runs, then inspect status and logs or cancel it. Durable records
  prevent duplicate execution and preserve uncertainty after lost responses.
- Verify and collect result commits, artifacts, checksums, and gate evidence
  without changing a dirty checkout. Integration still requires local gates and
  explicit approval.
- Use terminal, dark, or light themes, clearer branch/detail panels, mouse row
  selection and scrolling, and complete keyboard navigation. Terminal modes are
  restored on exit, signals, delegated commands, and fallback.
- Install an optional prebuilt fleet helper with required/auto/never modes and
  validation that preserves the existing installation when a helper is rejected.

## Upgrade and installation

This is a backward-compatible upgrade from 2.0.0; state v2 and the existing core
and TUI protocols remain unchanged. Upgrade the shell and optional native helpers
together because their executable version handshakes must match.

The attached tar.gz and zip archives contain the exact release source; SHA256SUMS
verifies both downloads. The shell CLI remains compiler-free. To build the optional
native helpers, use `make build-core build-tui build-fleet`; fleet additionally
requires JSON-C development files and pkg-config. JSON-C is linked statically.
Run `sh install.sh` from the unpacked source to install into the documented prefix.

Users upgrading from 1.9 should first follow the documented 2.0 state migration.

## Trust and scope

Fleet is for trusted hosts and uses strict SSH host-key checks. Remote commands
use structured arguments; bootstrap and result collection validate content and
paths. Host-local trust and live state are never copied between hosts. A successful
remote task does not confer review approval or permission to promote its result.

Automatic placement, cross-host workflow graphs, scheduled task pools, and provider
adapter expansion remain outside this release. Existing real-host acceptance is
recorded separately from release-commit platform qualification.

See [fleet setup](https://github.com/Someblueman/hydra/blob/v2.1.0/docs/FLEET.md),
[remote tasks](https://github.com/Someblueman/hydra/blob/v2.1.0/docs/REMOTE_TASKS.md),
and [migration guidance](https://github.com/Someblueman/hydra/blob/v2.1.0/docs/MIGRATING_TO_2.0.md).
