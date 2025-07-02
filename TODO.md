# Project TODOs

## Planning Session Output
Date: 2025-07-01

### PR #15 Fix - Bulk Spawn Feature
- **Goal**: Fix failing CI tests and merge conflicts for bulk spawn feature PR
- **Issue**: Tests were using hardcoded absolute paths that don't exist in CI
- **Solution**: Use relative paths with HYDRA_BIN variable

### Technical Decisions
- Use `HYDRA_BIN="${HYDRA_BIN:-./bin/hydra}"` pattern for portable tests
- Merge both GitHub issue integration and bulk spawn features
- Keep all spawn options compatible

## Completed
- [x] Update release/v1.1.0 branch by merging main into it
  - Resolved CI workflow conflicts
  - Merged CI improvements (quick-check, update-notification, dev-setup)
  - Date: 2025-07-01
  
- [x] Fix hardcoded paths in tests/test_bulk_spawn_integration.sh
  - Replaced `/Users/sws/Code/hydra/bin/hydra` with `$HYDRA_BIN`
  - Added HYDRA_BIN variable definition at top of test
  - Date: 2025-07-01

- [x] Merge updated release/v1.1.0 into feature/bulk-spawn
  - Successfully combined bulk spawn with GitHub issue features
  - Updated usage docs to show all spawn options
  - Fixed completion scripts for both features
  - Date: 2025-07-01

- [x] Run make lint and make test to verify everything works
  - Linting passes completely
  - Bulk spawn tests now pass (10/10)
  - Some unrelated test failures remain
  - Date: 2025-07-01

- [x] Push all branches to update PR #15
  - Pushed feature/bulk-spawn with all fixes
  - Pushed release/v1.1.0 with CI improvements
  - Date: 2025-07-01

- [x] Implement 'kill all' functionality for hydra
  - Added `hydra kill --all` command to kill all active sessions
  - Added `--force` flag to skip confirmation prompt
  - Implemented safe confirmation flow in interactive mode
  - Added comprehensive tests for all scenarios
  - Updated shell completion for bash and zsh
  - All tests pass, ShellCheck clean
  - Date: 2025-07-03

## In Progress
- [ ] Monitor PR #15 CI results
  - Bulk spawn tests should now pass
  - May need to address remaining test failures if they block merge

## Backlog
- [ ] Fix remaining test failures (not related to bulk spawn)
  - Integration tests: version flag handling (--version, -v)
  - Integration tests: no-args should show help instead of error
  - Dashboard test: integer expression error
  - Kill command test: unbound variable (already fixed, needs verification)

- [ ] Improve test robustness
  - Add timeout handling for hanging tests
  - Better cleanup of tmux sessions between tests
  - Consider splitting large test files

## Technical Notes

### Merged Features
The spawn command now supports:
1. **Single session**: `hydra spawn feature-x`
2. **Bulk spawn**: `hydra spawn feature-x -n 3`
3. **AI tool selection**: `hydra spawn feature-x --ai aider`
4. **Mixed agents**: `hydra spawn exp --agents "claude:2,aider:1"`
5. **GitHub issues**: `hydra spawn --issue 42`

The kill command now supports:
1. **Single session**: `hydra kill feature-x`
2. **Kill all**: `hydra kill --all`
3. **Force mode**: `hydra kill --all --force`

### Test Path Fix
Tests now use:
```bash
HYDRA_BIN="${HYDRA_BIN:-$original_dir/bin/hydra}"
output="$("$HYDRA_BIN" spawn ...)"
```

This allows tests to work in any environment by using relative paths from the test location.