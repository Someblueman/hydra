#!/bin/sh

if [ "${1:-}" = --version ]; then
    echo "Hydra TUI 2.0.0 protocol 2"
    exit 0
fi

stty -echo -icanon min 0 time 1
kill -SEGV "$$"
