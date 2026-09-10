#!/bin/sh
set -eu
count=0
while IFS= read -r line; do
    count=$((count + 1))
done < "$1"
printf '%s\n' "$count"
