# Hydra 1.8.0 release checklist

This checklist separates implemented behavior, local qualification, and publication.
The feature remains on `feature/1.8.0-native-tui` until an exact candidate commit is
authorized and qualified.

## Implemented and locally evidenced

- Native/basic dispatch, visible crash/transient fallback, and shell-only operation.
- Native protocol v2 with recorded adapter capability, notification configuration,
  inspectable sources, and explicit confidence.
- Deterministic fixture rendering and real pseudo-terminal coverage for normal exit,
  SIGINT, SIGTERM, SIGHUP, crash fallback, resize, paste, disabled mouse input,
  keyboard navigation/search, pane isolation, narrow terminals, and exact terminal
  restoration.
- Native/basic agreement for clean, stale, malformed, and atomically changing maps.
- Live isolated-tmux budgets at 5, 20, and 100 heads through `make bench-tui`.
- Keyboard-only, no-color, narrow-layout, and readable-language accessibility review.
- Offline installation, packaging metadata, sanitizer, and shell-only coverage remain
  part of `make test-all` and `make sanitize`.

## Exact-candidate release evidence still required

- [ ] Review the final diff and create an authorized candidate commit.
- [ ] Push the feature branch and run the exact commit through hosted Linux and macOS
      native, shell, sanitizer, install, package, and PTY jobs.
- [ ] Record hosted check URLs and verify the package `.source` metadata equals the
      candidate commit.
- [ ] Merge, tag `v1.8.0`, and publish only with explicit authorization.
- [ ] Verify local, remote, tag, and release-object parity after publication.
