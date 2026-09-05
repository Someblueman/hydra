# Live tmux dashboard

`hydra dashboard` gathers panes from active heads into one tmux window. Use it to
watch several terminals at once; use [native mission control](NATIVE_TUI.md) for
head details, search, and explicit actions.

```sh
hydra dashboard                         # first pane from each head
hydra dashboard --panes-per-session 2    # up to two panes per head
hydra dashboard --panes-per-session all  # every pane
```

The dashboard temporarily moves live panes using tmux. These are real terminals,
not read-only screenshots: input can reach their running processes. Press `q` to
exit and restore panes to their original sessions. Exiting the dashboard does not
stop the heads.

The layout adapts to the number of collected panes. More panes mean less room for
each terminal; select fewer panes when output becomes difficult to read.

Cleanup attempts to restore panes on exit and handled interruption. If restoration
fails, inspect the warning and the remaining tmux panes before removing a session.
Do not assume a forced process termination completed restoration.

For a reproducible first-run recording, see the [quick tour](../assets/demos/README.md).
