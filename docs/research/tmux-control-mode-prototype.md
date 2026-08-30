# tmux control-mode prototype, 2026-08-30

Hydra 1.7 does not switch the TUI to tmux control mode. The bounded prototype in
`scripts/bench-tmux-control.sh` compares repeated `capture-pane` processes with one
control-mode attachment carrying the same number of capture requests.

On macOS arm64 with tmux 3.5a, 30 captures produced all 30 expected markers in both
paths. Repeated polling took 97 ms; the single control-mode attachment took 9 ms.
The command is replayable as:

```sh
HYDRA_TMUX_BENCH_ITERATIONS=30 sh scripts/bench-tmux-control.sh
```

This establishes promising local process-amortization and basic response reliability.
It does not cover reconnects, server restart, partial frames, sustained CPU/RSS,
terminal resize, or macOS/Linux parity. Those remain acceptance work for the 1.8 TUI;
the 1.7 TUI continues using its existing bounded polling path.
