#!/bin/sh
# Install Git hooks for Hydra development
set -e

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

echo "✅ Git hooks installed successfully!"
echo "The pre-push hook will now check if your branch is behind main."