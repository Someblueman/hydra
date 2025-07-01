# Branch Protection Rules

This document describes the recommended branch protection rules for the Hydra project.

## Main Branch Protection

**Branch**: `main`

### Required Status Checks
- [x] CI / Lint POSIX Compliance (ubuntu-latest)
- [x] CI / Test (ubuntu-latest)
- [x] CI / Shell Compatibility (ubuntu-latest, sh)
- [x] CI / Shell Compatibility (ubuntu-latest, bash)
- [x] CI / Shell Compatibility (macos-latest, sh)
- [x] CI / Shell Compatibility (macos-latest, bash)

### Settings
- [x] Require branches to be up to date before merging
- [x] Include administrators
- [x] Require pull request reviews before merging
  - Required approving reviews: 1
  - Dismiss stale pull request approvals when new commits are pushed
- [x] Require conversation resolution before merging

## Release Branch Protection

**Branch pattern**: `release/*`

### Required Status Checks
- Same as main branch

### Settings
- [x] Require branches to be up to date before merging
- [x] Require pull request reviews before merging
  - Required approving reviews: 1

## How to Configure

1. Go to Settings → Branches in your GitHub repository
2. Add rule for `main` with the above settings
3. Add rule for `release/*` with the above settings

## Benefits

- **CI on all branches**: Every push to feature/fix/hotfix/release branches runs CI
- **Protected main**: Can't push directly to main without PR and passing tests
- **Quality gates**: All tests must pass before merging
- **Up-to-date requirement**: Branches must be current with base branch before merge