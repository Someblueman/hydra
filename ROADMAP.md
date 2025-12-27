# Hydra Roadmap

This document outlines the research findings and planned features for Hydra releases.

## Current State (v1.3.1)

### Strengths
- **Solid Core**: 197+ tests, POSIX-compliant, works on Linux/macOS
- **Performance**: Worktree caching, state caching, lazy loading
- **Multi-Agent**: Supports claude, aider, gemini, codex, cursor, copilot
- **GitHub Integration**: `spawn --issue <#>` for issue-to-session workflow
- **TUI**: Interactive management with tags, search, filtering
- **Grouping**: Bulk operations on related sessions
- **Dashboard**: Multi-session monitoring

---

## Competitive Landscape

### Direct Competitors

| Tool | Key Differentiator | What Hydra Lacks |
|------|-------------------|------------------|
| **[Phantom](https://github.com/aku11i/phantom)** | MCP server for AI autonomy, PR integration | MCP server mode |
| **[gwq](https://github.com/d-kuro/gwq)** | Task queue with priority/dependencies | Task scheduling |
| **[CCManager](https://github.com/kbwo/ccmanager)** | Session context transfer, status hooks | Context copying |
| **[Git Worktree Runner](https://github.com/coderabbitai/git-worktree-runner)** | Auto config copying, dep install | Env automation |

### Session Managers (Tmux-focused)

| Tool | Key Differentiator |
|------|-------------------|
| **[tmuxinator](https://github.com/tmuxinator/tmuxinator)** | YAML templates with dynamic variables |
| **[Zellij](https://zellij.dev/)** | Rust-based multiplexer |
| **[tmux-tea](https://github.com/2KAbhishek/tmux-tea)** | Session previews, fzf integration |

---

## Feature Gap Analysis

### High-Value Gaps

1. **Session Context Transfer** - CCManager copies Claude Code session data to new worktrees
2. **Status Change Hooks** - Execute commands when session states change (idle -> busy)
3. **Task Queue System** - Priority-based task scheduling with dependency resolution
4. **MCP Server Mode** - Let AI autonomously manage worktrees
5. **Environment Setup Automation** - Auto-copy configs, install dependencies
6. **Session Metrics** - Track session time, command counts

### Medium-Value Gaps

7. **PR Integration** - Create worktrees from PRs (not just issues)
8. **Multi-Repository Support** - Manage multiple repos from one interface
9. **Remote Session Support** - SSH + remote tmux
10. **Session Templates** - Shareable project templates

---

## v1.3.2 - Polish & Scriptability

Focus: Quick wins that improve multi-agent workflows

### Features

#### 1. JSON Output Mode
```sh
hydra list --json           # Machine-readable session list
hydra status --json         # Parseable status for scripts
hydra list --groups --json  # Include group information
```
- Enables scripting, external monitoring, CI integration
- Format: `{"branch": "...", "session": "...", "ai": "...", "group": "...", "status": "..."}`

#### 2. Session Duration Tracking
```sh
hydra list
# feature-x  | 2h 15m | claude | active
```
- Store spawn timestamp in state file
- Calculate duration on `list`/`status`

#### 3. Orphan Detection in `doctor`
```sh
hydra doctor
# Orphaned worktrees: 2 (run `hydra cleanup` to remove)
```
- Detect worktrees without matching sessions
- Detect sessions without matching worktrees
- Optional `hydra cleanup` command

#### 4. TUI Multi-Select
- Shift+arrow or space to select multiple sessions
- Bulk kill, bulk tag, bulk group assignment

#### 5. Better Error Recovery
- Auto-recovery from corrupted state file
- Graceful handling of tmux server restart
- Stale lock cleanup improvements

---

## v1.4.0 - Scale & Integration

Theme: Multi-agent coordination + deep GitHub integration

### Must Have (Core Value)

#### 1. Session Dependencies
```sh
hydra spawn feature-tests --after feature-impl
hydra spawn docs --after feature-impl,feature-tests
```
- Wait for dependent sessions to be killed/completed
- Dependency graph stored in state
- `hydra list --deps` shows dependency tree
- Use case: Staged agent workflows (impl -> test -> docs)

#### 2. PR Integration
```sh
hydra spawn --pr 123        # Create worktree from existing PR
hydra spawn --pr-new        # Create branch and open draft PR
hydra pr <branch>           # Open PR for existing session
```
- Extends existing `--issue` pattern
- Auto-links PR to session in state
- `hydra list` shows PR status (open/merged/closed)

#### 3. Cross-Session Communication
```sh
hydra send feature-a "check this when done"   # Queue message
hydra recv                                     # Receive messages
hydra broadcast "run tests" --group backend    # Message to group
```
- Message queue per session (file-based)
- Enables loose coordination between agents
- HYDRA_MESSAGE env var for hooks

### Should Have (Significant Value)

#### 4. Resource Limits
```sh
HYDRA_MAX_SESSIONS=8                # Global limit
hydra spawn feature-x --priority high  # Queue when at limit
hydra queue                            # Show pending spawns
```
- Prevent overwhelming system with too many agents
- Priority queue for pending spawns

#### 5. Enhanced Group Workflows
```sh
hydra group create backend feature-a feature-b
hydra group wait backend              # Wait for all to complete
hydra group status backend --json     # Group health check
```
- Named groups with lifecycle management
- Group-level operations beyond current grouping

#### 6. Environment Setup Automation
```yaml
# .hydra/config.yml
setup:
  - npm install
  - cp .env.example .env
  - docker-compose up -d db
```
- Run on spawn via existing hooks
- `HYDRA_SKIP_SETUP=1` to bypass

### Nice to Have

#### 7. Session Templates
```sh
hydra template list
hydra template create fullstack
hydra spawn feature-x --template fullstack
```
- Reusable configurations (layout, startup, env)
- Share across repos via `~/.hydra/templates/`

#### 8. Multi-Repo Awareness
```sh
hydra list --all-repos
hydra switch --repo other-project
```
- Track multiple repos in single state
- Use case: Microservices, monorepo sub-projects

---

## Backlog (Future Consideration)

1. **MCP Server Mode** - Let AI autonomously spawn/kill sessions (v1.5.0?)
2. **Session Snapshots** - Save/restore session state
3. **Real-time Dashboard** - Live output from all sessions
4. **Session Metrics/Analytics** - Time tracking, command counts
5. **Context Transfer** - Copy Claude conversation to new session

---

## Quick Wins Reference

| Feature | Inspired By | Effort |
|---------|-------------|--------|
| `--pr` flag for spawn | Phantom | Low |
| Session duration in `list` | CCManager | Low |
| JSON output for `list` | gwq | Low |
| Post-spawn hooks async | CCManager | Medium |
| Session preview in TUI | tmux-tea | Medium |

---

## Sources

### Competitors
- [Phantom](https://github.com/aku11i/phantom) - MCP server, PR integration
- [gwq](https://github.com/d-kuro/gwq) - Task queue, JSON logging
- [CCManager](https://github.com/kbwo/ccmanager) - Status hooks, context transfer
- [tmuxinator](https://github.com/tmuxinator/tmuxinator) - YAML templates
- [Git Worktree Runner](https://github.com/coderabbitai/git-worktree-runner) - Environment automation

### Market Research
- [tmux alternatives](https://alternativeto.net/software/tmux/)
- [awesome-tmux](https://github.com/rothgar/awesome-tmux)
- [Parallel AI coding workflows](https://docs.agentinterviews.com/blog/parallel-ai-coding-with-gitworktrees/)
