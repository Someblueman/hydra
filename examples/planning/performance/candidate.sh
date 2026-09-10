#!/bin/sh
set -eu
awk 'END { print NR }' "$1"
