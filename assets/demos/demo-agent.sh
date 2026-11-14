#!/bin/sh
# The recording's stand-in for an AI coding CLI (assets/demos/record.sh).
# Hydra launches it as an ordinary agent profile in a real tmux session and
# worktree; it prints a short canned transcript for its branch, then answers
# each line typed to it from the same script. It is not an AI and changes
# nothing: every word it prints is written below. Lines stay within 31
# columns, the width of the conversation pane at 120x32.
set -u
dim=$(printf '\033[38;5;245m')
dot=$(printf '\033[38;5;117m')
ok=$(printf '\033[38;5;114m')
bad=$(printf '\033[38;5;214m')
off=$(printf '\033[0m')

say() {
    sleep "${DEMO_AGENT_PACE:-0.1}"
    printf '%s\n' "$*"
}
step() { say "${dot}●${off} $*"; }
note() { say "  ${dim}$*${off}"; }

branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)
# Start on a clean screen: only the agent's own words are its conversation.
printf '\033[2J\033[H'
say "${dim}scripted demo agent, not an AI${off}"
say "${dim}task:${off} ${1:-none}"
say ''

case "$branch" in
    search-index)
        step 'Read README.md, src/search.c'
        note 'each query rescans all files'
        step 'Published a plan to Hydra'
        note '1 index  term -> files'
        note '2 query  index, else scan'
        note '3 check  make test'
        step 'Waiting for your approval'
        replies='About 1 MB per 10k documents:
a sorted term file.
|Yes. A scan stays the fallback
while the index is missing.'
        ;;
    fix-tests)
        step 'Ran make test'
        say "  ${bad}2 failed${off}, 40 passed"
        note 'test_rank: doc-7 before doc-2'
        step 'Equal scores kept the order'
        note 'that readdir() returned'
        step 'Edited src/rank.c  +3 -1'
        note 'ties now sort by path'
        step 'Ran make test'
        say "  ${ok}42 passed${off}"
        replies='readdir() order differs
between checkouts, so equal
scores swapped places on
some machines. Sorting ties
by path makes it stable.
|Committed it as "rank: break
score ties by path".'
        ;;
    *)
        step 'Read README.md'
        replies='Done.'
        ;;
esac

# Answer typed lines in order; each reply is one |-separated block above.
while :; do
    printf '%s>%s ' "$dot" "$off"
    IFS= read -r line || exit 0
    [ -n "$line" ] || continue
    reply=${replies%%|*}
    case $replies in *'|'*) replies=${replies#*|} ;; *) replies='Nothing more to add.' ;; esac
    sleep 0.8
    printf '%s\n' "$reply" | {
        IFS= read -r text && say "${dot}●${off} $text"
        while IFS= read -r text; do say "  $text"; done
    }
done
