# Hydra v1.1.0 Release Notes

## Overview

Hydra v1.1.0 brings major new features for managing multi-agent workflows, GitHub integration, and improved reliability. This release focuses on productivity enhancements while maintaining the core POSIX-compliant design.

## Key Features

### 🔗 GitHub Issue Integration
Create hydra sessions directly from GitHub issues:
```sh
hydra spawn --issue 42
```
- Automatically fetches issue title and creates appropriate branch name
- Validates issue numbers and handles special characters
- Requires GitHub CLI (`gh`) to be installed and authenticated

### 🚀 Bulk Spawn for Multi-Agent Workflows
Spawn multiple AI sessions at once:
```sh
# Create 3 sessions: feature-x-1, feature-x-2, feature-x-3
hydra spawn feature-x -n 3

# Mixed agents: 2 Claude + 1 Aider sessions
hydra spawn experiment --agents "claude:2,aider:1"
```
- Confirmation prompts for spawning >3 sessions
- Automatic rollback on failure with user choice
- First session auto-selected after bulk spawn

### 🧹 Kill All Sessions
Clean up all hydra sessions with one command:
```sh
hydra kill --all        # Interactive confirmation
hydra kill --all --force # Skip confirmation
```

### 🤖 Google Gemini Support
Added Google's Gemini CLI as a supported AI tool:
```sh
hydra spawn feature --ai gemini
```
- Requires Node.js 18+ and Google account authentication
- Free tier with generous limits (60 req/min, 1000 req/day)

### 🛠️ Enhanced Reliability
- Fixed library path resolution when running hydra from inside hydra sessions
- Added HYDRA_ROOT environment variable for custom installations
- Improved non-interactive mode handling for CI/CD environments

## Installation

```sh
git clone https://github.com/yourusername/hydra.git
cd hydra
sudo make install
```

## Upgrading

If upgrading from v0.2.0 or earlier:
```sh
cd hydra
git pull
sudo make install
```

## Compatibility

- Requires `/bin/sh` (POSIX shell)
- tmux ≥ 3.0
- git
- Optional: GitHub CLI for issue integration
- Optional: Node.js 18+ for Gemini support

## What's Next

Future releases will focus on:
- Enhanced dashboard capabilities
- Remote session support
- Integration with more AI tools
- Performance optimizations

## Contributors

Thanks to all contributors who made this release possible!

---

For detailed changes, see the [CHANGELOG](CHANGELOG.md).