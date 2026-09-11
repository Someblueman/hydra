#!/bin/sh
set -eu
count=0
# Count records without interpreting their contents.
# shellcheck disable=SC2034
while IFS= read -r line; do
    count=$((count + 1))
done < "$1"
printf '%s\n' "$count"
