#!/bin/sh
set -eu
printf '%s change\n' "$1" > "$1.txt"
git add "$1.txt"
git -c user.name=Review -c user.email=review@example.invalid -c commit.gpgSign=false commit -qm "$1 output"
