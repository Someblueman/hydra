#!/bin/sh
# Install Git hooks for Hydra development
set -e

# Install pre-push hook (branch behind main check)
cat > .git/hooks/pre-push << 'EOF'
#!/bin/sh
# Pre-push hook to check if branch is up-to-date with main

current_branch=$(git rev-parse --abbrev-ref HEAD)

# Skip check for main branch
if [ "$current_branch" = "main" ]; then
    exit 0
fi

printf "Checking if branch is up-to-date with main..."

# Fetch latest main
git fetch origin main --quiet

# Check if current branch is behind main
behind=$(git rev-list --count HEAD..origin/main)

if [ "$behind" -gt 0 ]; then
    printf "\n⚠️  Your branch is %d commits behind main.\n" "$behind"
    printf "Consider running: git rebase origin/main\n\n"
    printf "Push anyway? (y/N) "
    read -r response
    case "$response" in
        [yY][eE][sS]|[yY]) 
            exit 0
            ;;
        *)
            printf "Push cancelled.\n"
            exit 1
            ;;
    esac
else
    printf " ✓\n"
fi
EOF

chmod +x .git/hooks/pre-push

# Install pre-commit hook (ShellCheck + dash syntax on staged shell scripts)
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/sh
set -eu

# Collect staged shell files (added/modified)
files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\\.(sh)$|^bin/hydra$' || true)

# Nothing to check
[ -z "$files" ] && exit 0

echo "Running pre-commit checks for shell scripts..."

# Require ShellCheck
if ! command -v shellcheck >/dev/null 2>&1; then
  echo "Error: ShellCheck not found. Install it (e.g., 'brew install shellcheck' or 'sudo apt-get install -y shellcheck')." >&2
  exit 1
fi

failed=0
for f in $files; do
  if [ -f "$f" ]; then
    echo "Checking $f..."
    shellcheck --shell=sh --severity=style "$f" || failed=1
    if command -v dash >/dev/null 2>&1; then
      dash -n "$f" || failed=1
    else
      # Fall back to sh syntax check
      sh -n "$f" || failed=1
    fi
  fi
done

if [ "$failed" -ne 0 ]; then
  echo "Pre-commit checks failed. Fix issues or commit with --no-verify to bypass." >&2
  exit 1
fi

exit 0
EOF

chmod +x .git/hooks/pre-commit

echo "✅ Git hooks installed successfully!"
echo "- pre-push: warns if branch is behind main."
echo "- pre-commit: runs ShellCheck and syntax checks on staged shell scripts."
