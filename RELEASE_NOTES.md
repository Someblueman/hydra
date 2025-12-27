# Hydra v1.3.3 - Full Polish Release

A comprehensive polish release featuring TUI multi-select, bug fixes, performance improvements, and documentation updates.

## Highlights

### TUI Multi-Select
Bulk operations are now a breeze with the new multi-select feature:
- **SPACE** - Toggle selection on current session
- **x** - Kill all selected sessions at once
- **G** - Assign group to all selected sessions
- **Esc** - Clear selection (press again to clear filters)

Visual feedback includes `[x]` markers on selected items and a selection count in the header.

### Bug Fixes

| Issue | Description |
|-------|-------------|
| Security | `cmd_switch` now validates numeric input and range bounds |
| Broadcast | Fixed count always showing 0 (subshell variable loss) |
| Bulk Spawn | Fixed off-by-one error causing switch to wrong session |
| JSON | `json_escape` now handles newlines correctly |

### Performance Improvements

- **Session caching**: `cmd_list` now caches all tmux sessions upfront instead of checking each entry
- **Timestamp caching**: Single `date +%s` call per command instead of repeated calls
- **POSIX optimization**: Replaced AWK field counting with shell word splitting in state.sh

### Documentation Updates

README now includes:
- `cleanup` command for removing stale locks and dead mappings
- `tail` command for viewing session output
- `broadcast` command for sending commands to all sessions
- `wait-idle` command for automation workflows
- `group` command for session organization
- Missing environment variables: `HYDRA_SKIP_AI`, `HYDRA_DASHBOARD_NO_ATTACH`, `HYDRA_NONINTERACTIVE`, `HYDRA_REGENERATE_RUN_STARTUP`
- `--json` flag documentation for `list` and `status`

## New Test Suites

- `test_switch.sh` - 8 tests for input validation
- `test_json_output.sh` - 17 tests for JSON escaping and output validity

Total tests: **227** (all passing)

## Upgrade

```sh
git pull && sudo make install
```

## Full Changelog

See [CHANGELOG.md](CHANGELOG.md) for complete details.
