# Hydra performance matrix (Linux container baseline)

Generated 2026-10-17T14:23:53Z; Linux 6.18.44-fc-v139 x86_64, 4 logical CPUs; tools: {'git': 'git version 2.43.0', 'tmux': 'tmux 3.4', 'dash': 'sh', 'cc': 'cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0'}.
Load note: main: shared 4-core Linux container (Firecracker VM); other agents ran builds and tests concurrently, load average recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h10-headless-1s: retry of the 10-head headless fixture after the 125 ms producer interval ended every supervised run as observation_unavailable; producer interval 1 s; shared 4-core Linux container with concurrent peer agents, load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h10-headless-lock: diagnostic rerun of the 10-head headless fixture under a NON-DEFAULT lock budget (HYDRA_LOCK_RETRIES=200, about 10 s instead of the default 5 retries of 50 ms) after two default-configuration attempts ended every supervised run as observation_unavailable when ten owners ingested their first output observation at the same instant; producer interval 1 s; shared 4-core Linux container with concurrent peer agents, load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h10-headless-3c: three-client 10-head headless cells without sustained output (sustained output stopped every supervised worker in three earlier attempts, so a fixture that ran it could not continue to the three-client group); shared 4-core Linux container with concurrent peer agents, load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h10-headless-output: fourth attempt at the 10-head headless sustained-output cell under the default configuration and 125 ms producer interval, run alone; shared 4-core Linux container with concurrent peer agents, load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h50: 50-head quiescent and one-changing cells; shared 4-core Linux container with concurrent peer agents, load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h50-interactive: retry of the 50-head interactive fixture after the first attempt lost its quiescent cell to the post-cell view restore timeout; shared 4-core Linux container with concurrent peer agents (load above 10 at launch), load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); h50-headless: retry of the 50-head headless fixture after the first attempt timed out waiting 15 s for the first client to show 50 owned heads at load average 24; client readiness bound now 15 s + 0.5 s per head; shared 4-core Linux container with concurrent peer agents, load recorded per fixture; reduced windows (settle 3 s, idle 8 s, gap 1.5 s); load at start [1.66552734375, 2.625, 1.39306640625], end [8.037109375, 9.37255859375, 5.89453125].

Revisions: baseline 61f9a7288112bcf94abd4fcd351458fcff268f75 (identities unchanged: False)
Durations: {'settle_s': 3.0, 'idle_s': 8.0, 'stimulus_gap_s': 1.5, 'interaction_s': 10.0, 'poll_ms': 100, 'sample_interval_ms': 500, 'stimulus_deadline_ms': 4000}
Declared policy: {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000, 'per_run': {'main': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000}, 'h10-headless-1s': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 1000000000}, 'h10-headless-lock': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 1000000000, 'fixture_env': {'HYDRA_LOCK_RETRIES': '200'}}, 'h10-headless-3c': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000, 'fixture_env': {}}, 'h10-headless-output': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000, 'fixture_env': {}}, 'h50': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000}, 'h50-interactive': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000, 'fixture_env': {}}, 'h50-headless': {'tui_refresh_seconds': '2', 'change_interval_ns': 2000000000, 'output_line_bytes': 128, 'default_output_interval_ns': 31250000, 'workload_seed': 104729, 'worker_phase_bound_ns': 30000000000, 'interactive_output_interval_ns': 31250000, 'headless_output_interval_ns': 125000000, 'fixture_env': {}}}}

Cells planned 74, measured 45, unsupported/not planned 37. Validation: {'ok': True, 'problems': []}

## Idle cost after settling (per cell, idle window)

| Cell | TUI CPU % (1 core) | helpers CPU % | tmux CPU % | agents CPU % | observer CPU % | TUI wakeups/s | launches/min (git/tmux/hydra) | sys forks/s | TUI RSS MiB | procs | TUI fds |
|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|
| 1/interactive/1c/closed/quiescent/t0 (main) | 1.38 | 22.00 | 0.00 | 0.00 | 1.38 | 24.4 | 480 (48/8/8) | 387.5 | 44.2 | 22 | 10 |
| 1/interactive/1c/closed/one-changing/t0 (main) | 1.38 | 22.26 | 0.13 | 0.13 | 2.75 | 24.5 | 480 (48/8/8) | 529.4 | 31.4 | 15 | 6 |
| 1/interactive/1c/closed/all-changing/t0 (main) | 1.25 | 20.63 | 0.00 | 0.13 | 2.88 | 24.5 | 480 (48/8/8) | 319.4 | 24.4 | 11 | 6 |
| 1/interactive/1c/closed/sustained-output/t0 (main) | 1.25 | 21.88 | 0.75 | 1.75 | 1.13 | 24.5 | 495 (50/8/8) | 329.8 | 41.9 | 19 | 9 |
| 1/interactive/1c/open/quiescent/t0 (main) | 1.63 | 23.00 | 0.13 | 0.00 | 1.25 | 24.4 | 585 (58/8/12) | 729.6 | 46.8 | 15 | 6 |
| 1/interactive/1c/open/sustained-output/t0 (main) | 2.25 | 25.76 | 1.38 | 2.00 | 1.63 | 24.4 | 600 (60/8/12) | 864.3 | 44.0 | 14 | 6 |
| 1/interactive/3c/closed/quiescent/t0 (main) | 4.13 | 64.01 | 0.25 | 0.00 | 2.25 | 73.4 | 1365 (134/24/24) | 666.3 | 71.2 | 25 | 6 |
| 1/interactive/3c/closed/one-changing/t0 (main) | 4.00 | 63.77 | 0.25 | 0.00 | 3.75 | 73.1 | 1440 (144/24/24) | 479.0 | 63.4 | 21 | 6 |
| 1/interactive/3c/closed/all-changing/t0 (main) | 4.13 | 62.39 | 0.25 | 0.00 | 4.00 | 73.3 | 1440 (144/24/24) | 727.1 | 65.0 | 22 | 6 |
| 1/interactive/3c/closed/sustained-output/t0 (main) | 3.75 | 64.02 | 1.00 | 1.75 | 2.13 | 73.4 | 1440 (144/24/24) | 478.8 | 60.2 | 20 | 6 |
| 10/interactive/1c/closed/quiescent/t0 (main) | 4.25 | 97.89 | 0.00 | 0.25 | 2.50 | 217.2 | 428 (47/4/6) | 606.7 | 26.3 | 30 | 8 |
| 10/interactive/1c/closed/one-changing/t0 (main) | 3.63 | 83.40 | 0.13 | 0.00 | 4.25 | 198.4 | 405 (44/4/6) | 426.0 | 41.5 | 31 | 7 |
| 10/interactive/1c/closed/all-changing/t0 (main) | 3.75 | 96.25 | 0.00 | 0.25 | 4.38 | 195.8 | 412 (45/4/6) | 331.3 | 26.0 | 29 | 8 |
| 10/interactive/1c/closed/sustained-output/t0 (main) | 3.75 | 86.52 | 3.00 | 13.63 | 2.25 | 193.9 | 405 (44/4/6) | 331.1 | 24.7 | 30 | 8 |
| 10/interactive/1c/open/quiescent/t0 (main) | 5.25 | 132.78 | 0.00 | 0.00 | 2.50 | 292.6 | 698 (76/6/11) | 848.3 | 46.3 | 34 | 8 |
| 10/interactive/1c/open/sustained-output/t0 (main) | 4.25 | 92.14 | 3.63 | 13.25 | 2.50 | 171.8 | 525 (56/4/10) | 439.9 | 46.8 | 33 | 8 |
| 10/interactive/3c/closed/quiescent/t0 (main) | 10.63 | 257.42 | 0.13 | 0.00 | 3.25 | 569.2 | 1215 (132/12/18) | 1050.6 | 85.8 | 53 | 8 |
| 10/interactive/3c/closed/one-changing/t0 (main) | 12.25 | 351.12 | 0.25 | 0.25 | 5.50 | 732.9 | 1545 (170/16/20) | 1300.7 | 119.7 | 65 | 9 |
| 10/interactive/3c/closed/all-changing/t0 (main) | 10.88 | 309.09 | 0.25 | 0.00 | 5.00 | 579.3 | 1290 (142/12/18) | 1322.0 | 89.9 | 49 | 8 |
| 10/interactive/3c/closed/sustained-output/t0 (main) | 10.13 | 244.42 | 3.38 | 13.88 | 3.50 | 614.4 | 1222 (132/13/18) | 1197.3 | 94.7 | 52 | 8 |
| 1/headless/1c/closed/quiescent/t0 (main) | 1.25 | 16.63 | 0.00 | 0.00 | 1.38 | 24.5 | 420 (48/0/8) | 380.9 | 33.5 | 26 | 6 |
| 1/headless/1c/closed/one-changing/t0 (main) | 1.88 | 25.89 | 0.00 | 0.00 | 3.50 | 51.8 | 420 (48/0/8) | 474.7 | 31.4 | 24 | 8 |
| 1/headless/1c/closed/all-changing/t0 (main) | 1.50 | 20.13 | 0.00 | 0.00 | 3.63 | 34.4 | 420 (48/0/8) | 344.4 | 32.7 | 24 | 8 |
| 1/headless/1c/closed/sustained-output/t0 (main) | 1.50 | 23.76 | 0.00 | 0.38 | 2.13 | 43.3 | 1192 (151/0/8) | 981.8 | 41.1 | 31 | 9 |
| 1/headless/3c/closed/quiescent/t0 (main) | 4.00 | 54.64 | 0.00 | 0.00 | 2.63 | 73.0 | 1935 (234/0/24) | 1381.3 | 59.5 | 31 | 6 |
| 1/headless/3c/closed/one-changing/t0 (main) | 4.13 | 54.26 | 0.00 | 0.13 | 4.25 | 73.1 | 2010 (244/0/24) | 1583.4 | 67.0 | 33 | 7 |
| 1/headless/3c/closed/all-changing/t0 (main) | 3.75 | 56.01 | 0.00 | 0.00 | 4.50 | 73.4 | 1260 (144/0/24) | 801.7 | 76.8 | 36 | 7 |
| 1/headless/3c/closed/sustained-output/t0 (main) | 4.00 | 59.00 | 0.00 | 0.38 | 3.13 | 82.8 | 2025 (246/0/24) | 1350.5 | 82.2 | 42 | 9 |
| 10/headless/1c/closed/quiescent/t0 (main) | 3.38 | 99.40 | 0.00 | 0.00 | 9.50 | 171.7 | 398 (48/0/5) | 679.8 | 25.3 | 139 | 8 |
| 10/headless/1c/closed/one-changing/t0 (main) | 2.25 | 63.77 | 0.00 | 0.13 | 10.00 | 123.9 | 398 (47/0/6) | 705.9 | 28.3 | 140 | 8 |
| 10/headless/1c/closed/all-changing/t0 (main) | 2.63 | 88.44 | 0.00 | 0.00 | 12.63 | 115.7 | 322 (38/0/5) | 632.2 | 28.0 | 140 | 8 |
| 10/headless/1c/closed/quiescent/t0 (h10-headless-1s) | 2.75 | 91.14 | 0.00 | 0.00 | 9.38 | 152.0 | 510 (62/0/6) | 767.5 | 27.6 | 141 | 9 |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-1s) | 3.13 | 98.90 | 0.00 | 0.13 | 11.00 | 192.8 | 510 (62/0/6) | 812.2 | 30.4 | 141 | 8 |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-1s) | 2.75 | 93.19 | 0.00 | 0.13 | 10.51 | 198.5 | 510 (62/0/6) | 1081.2 | 28.3 | 140 | 8 |
| 10/headless/1c/closed/quiescent/t0 (h10-headless-lock) [non-default env] | 3.63 | 109.52 | 0.00 | 0.13 | 9.25 | 188.7 | 510 (62/0/6) | 617.0 | 40.6 | 146 | 8 |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-lock) [non-default env] | 3.75 | 102.52 | 0.00 | 0.00 | 9.63 | 176.3 | 510 (62/0/6) | 587.1 | 29.9 | 141 | 8 |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-lock) [non-default env] | 3.25 | 105.40 | 0.00 | 0.63 | 11.00 | 174.2 | 510 (62/0/6) | 570.5 | 28.1 | 141 | 8 |
| 10/headless/3c/closed/quiescent/t0 (h10-headless-3c) | 10.00 | 306.68 | 0.00 | 0.13 | 11.38 | 527.2 | 1530 (186/0/18) | 1277.4 | 78.3 | 156 | 8 |
| 10/headless/3c/closed/one-changing/t0 (h10-headless-3c) | 10.38 | 314.73 | 0.00 | 0.13 | 12.75 | 529.4 | 1545 (188/0/18) | 1278.7 | 72.0 | 151 | 8 |
| 10/headless/3c/closed/all-changing/t0 (h10-headless-3c) | 8.62 | 266.62 | 0.00 | 0.75 | 12.50 | 545.9 | 1530 (186/0/18) | 1679.1 | 79.4 | 156 | 8 |
| 50/interactive/1c/closed/quiescent/t0 (h50-interactive) | 9.25 | 312.23 | 0.00 | 0.38 | 6.25 | 514.9 | 488 (60/0/5) | 908.3 | 23.7 | 110 | 8 |
| 50/interactive/1c/closed/one-changing/t0 (h50-interactive) | 7.00 | 182.57 | 0.25 | 0.25 | 8.50 | 472.7 | 412 (48/2/5) | 1145.0 | 29.3 | 112 | 8 |
| 50/interactive/1c/open/quiescent/t0 (h50-interactive) | 9.50 | 205.22 | 0.00 | 0.50 | 7.38 | 533.4 | 608 (72/0/9) | 900.3 | 44.1 | 114 | 8 |
| 50/interactive/3c/closed/quiescent/t0 (h50-interactive) | 18.50 | 506.92 | 0.00 | 0.25 | 8.88 | 1762.1 | 1448 (178/0/15) | 1969.5 | 83.7 | 132 | 8 |
| 50/interactive/3c/closed/one-changing/t0 (h50-interactive) | 15.51 | 237.59 | 0.00 | 0.63 | 11.13 | 1129.8 | 945 (112/0/14) | 1524.1 | 69.2 | 121 | 8 |

## Interaction latency (scheduled input to matching PTY frame; not input-to-pixel)

| Cell | navigation ms | search ms | resize ms | cancel ms | statuses | controller lateness max ms |
|---|---:|---:|---:|---:|---|---:|
| 1/interactive/1c/closed/quiescent/t0 (main) | 0.4 | 2.0 | 22.0 | 21.0 | observed,observed,observed,observed | 1.0 |
| 1/interactive/1c/closed/one-changing/t0 (main) | 0.7 | 1.3 | 23.3 | 23.7 | observed,observed,observed,observed | 2.0 |
| 1/interactive/1c/closed/all-changing/t0 (main) | 0.6 | 8.3 | 6.2 | 20.8 | observed,observed,observed,observed | 0.6 |
| 1/interactive/1c/closed/sustained-output/t0 (main) | 0.6 | 1.7 | 5.4 | 21.8 | observed,observed,observed,observed | 0.8 |
| 1/interactive/1c/open/quiescent/t0 (main) | 236.7 | 1.4 | 13.1 | 21.2 | observed,observed,observed,observed | 1.2 |
| 1/interactive/1c/open/sustained-output/t0 (main) | 227.8 | 2.7 | 12.5 | 20.9 | observed,observed,observed,observed | 1.7 |
| 1/interactive/3c/closed/quiescent/t0 (main) | 0.6 | 1.2 | 16.1 | 20.7 | observed,observed,observed,observed | 1.0 |
| 1/interactive/3c/closed/one-changing/t0 (main) | 0.7 | 1.4 | 6.3 | 28.1 | observed,observed,observed,observed | 1.2 |
| 1/interactive/3c/closed/all-changing/t0 (main) | 1.7 | 2.0 | 14.0 | 20.6 | observed,observed,observed,observed | 2.9 |
| 1/interactive/3c/closed/sustained-output/t0 (main) | 0.9 | 1.3 | 12.3 | 21.0 | observed,observed,observed,observed | 0.9 |
| 10/interactive/1c/closed/quiescent/t0 (main) | 0.8 | 1.8 | 11.5 | 21.4 | observed,observed,observed,observed | 0.7 |
| 10/interactive/1c/closed/one-changing/t0 (main) | 0.7 | 1.8 | 16.6 | 21.0 | observed,observed,observed,observed | 1.0 |
| 10/interactive/1c/closed/all-changing/t0 (main) | 0.7 | 1.4 | 38.5 | 21.0 | observed,observed,observed,observed | 0.8 |
| 10/interactive/1c/closed/sustained-output/t0 (main) | 0.6 | 1.2 | 6.2 | 20.9 | observed,observed,observed,observed | 0.9 |
| 10/interactive/1c/open/quiescent/t0 (main) | 236.1 | 1.7 | 13.1 | 20.8 | observed,observed,observed,observed | 1.0 |
| 10/interactive/1c/open/sustained-output/t0 (main) | 230.5 | 1.2 | 12.7 | 20.9 | observed,observed,observed,observed | 0.8 |
| 10/interactive/3c/closed/quiescent/t0 (main) | 0.4 | 1.4 | 8.2 | 21.9 | observed,observed,observed,observed | 0.9 |
| 10/interactive/3c/closed/one-changing/t0 (main) | 0.5 | 1.9 | 15.9 | 22.0 | observed,observed,observed,observed | 0.6 |
| 10/interactive/3c/closed/all-changing/t0 (main) | 0.6 | 2.1 | 7.3 | 20.7 | observed,observed,observed,observed | 1.7 |
| 10/interactive/3c/closed/sustained-output/t0 (main) | 1.3 | 1.3 | 6.4 | 21.9 | observed,observed,observed,observed | 2.4 |
| 1/headless/1c/closed/quiescent/t0 (main) | 0.7 | 1.3 | 18.9 | 23.3 | observed,observed,observed,observed | 0.9 |
| 1/headless/1c/closed/one-changing/t0 (main) | 0.7 | 2.0 | 31.6 | 22.5 | observed,observed,observed,observed | 2.8 |
| 1/headless/1c/closed/all-changing/t0 (main) | 0.9 | 1.2 | 7.9 | 20.9 | observed,observed,observed,observed | 0.6 |
| 1/headless/1c/closed/sustained-output/t0 (main) | 0.7 | 2.2 | 10.4 | 20.6 | observed,observed,observed,observed | 1.5 |
| 1/headless/3c/closed/quiescent/t0 (main) | 0.9 | 2.0 | 16.3 | 20.7 | observed,observed,observed,observed | 2.7 |
| 1/headless/3c/closed/one-changing/t0 (main) | 0.5 | 2.3 | 23.7 | 20.5 | observed,observed,observed,observed | 3.6 |
| 1/headless/3c/closed/all-changing/t0 (main) | 0.5 | 4.5 | 14.4 | 20.6 | observed,observed,observed,observed | 0.8 |
| 1/headless/3c/closed/sustained-output/t0 (main) | 2.8 | 1.4 | 11.8 | 20.7 | observed,observed,observed,observed | 2.3 |
| 10/headless/1c/closed/quiescent/t0 (main) | 0.5 | 1.6 | 10.9 | 20.6 | observed,observed,observed,observed | 0.6 |
| 10/headless/1c/closed/one-changing/t0 (main) | 1.3 | 1.7 | 11.7 | 21.5 | observed,observed,observed,observed | 9.2 |
| 10/headless/1c/closed/all-changing/t0 (main) | 0.9 | 2.0 | 13.3 | 480.2 | observed,observed,observed,observed | 0.7 |
| 10/headless/1c/closed/quiescent/t0 (h10-headless-1s) | 1.6 | 1.8 | 5.6 | 20.9 | observed,observed,observed,observed | 4.1 |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-1s) | 0.5 | 4.2 | 24.0 | 20.9 | observed,observed,observed,observed | 1.5 |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-1s) | 0.6 | 1.4 | 6.5 | 23.4 | observed,observed,observed,observed | 0.6 |
| 10/headless/1c/closed/quiescent/t0 (h10-headless-lock) [non-default env] | 1.0 | 3.7 | 14.1 | 20.9 | observed,observed,observed,observed | 0.9 |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-lock) [non-default env] | 1.2 | 1.5 | 10.1 | 20.6 | observed,observed,observed,observed | 0.9 |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-lock) [non-default env] | 1.1 | 1.7 | 14.1 | 21.2 | observed,observed,observed,observed | 2.1 |
| 10/headless/3c/closed/quiescent/t0 (h10-headless-3c) | 0.7 | 3.8 | 13.4 | 20.9 | observed,observed,observed,observed | 0.7 |
| 10/headless/3c/closed/one-changing/t0 (h10-headless-3c) | 1.5 | 3.1 | 6.3 | 20.8 | observed,observed,observed,observed | 0.7 |
| 10/headless/3c/closed/all-changing/t0 (h10-headless-3c) | 1.5 | 2.1 | 10.3 | 22.4 | observed,observed,observed,observed | 1.1 |
| 50/interactive/1c/closed/quiescent/t0 (h50-interactive) | 0.9 | 2.3 | 11.3 | 3912.8 | observed,observed,observed,observed | 0.8 |
| 50/interactive/1c/closed/one-changing/t0 (h50-interactive) | 0.9 | 6.0 | 7.6 | 2023.8 | observed,observed,observed,observed | 0.6 |
| 50/interactive/1c/open/quiescent/t0 (h50-interactive) | 225.1 | 2.5 | 7.5 | n/a | observed,observed,observed,miss | 0.6 |
| 50/interactive/3c/closed/quiescent/t0 (h50-interactive) | 0.6 | 5.0 | 25.0 | n/a | observed,observed,observed,miss | 0.6 |
| 50/interactive/3c/closed/one-changing/t0 (h50-interactive) | 0.7 | n/a | 14.6 | n/a | observed,miss,observed,miss | 2.0 |
- navigation: n=45 median 0.7 ms, p95 230.473273, p99 None, max 236.7 ms, not observed 0; p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound
- search: n=44 median 1.8 ms, p95 5.01442, p99 None, max 8.3 ms, not observed 1; p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound
- resize: n=45 median 12.5 ms, p95 25.041267, p99 None, max 38.5 ms, not observed 0; p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound
- cancel: n=42 median 21.0 ms, p95 480.227788, p99 None, max 3912.8 ms, not observed 3; p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound

## Update latency (worker change receipt to selected-head changed-file count)

| Cell | steps seen | median ms | max ms | lower-bound min ms | changes declared | not seen in window | poll error |
|---|---:|---:|---:|---:|---:|---:|---|
| 1/interactive/1c/closed/one-changing/t0 (main) | 4 | 1119.7 | 1151.3 | 1067.6 | 9 | 5 | none |
| 1/interactive/1c/closed/all-changing/t0 (main) | 4 | 1696.0 | 1797.6 | 1682.6 | 9 | 5 | none |
| 1/interactive/3c/closed/one-changing/t0 (main) | 4 | 1630.4 | 1752.4 | 1583.7 | 9 | 5 | none |
| 1/interactive/3c/closed/all-changing/t0 (main) | 3 | 198.6 | 205.3 | 176.4 | 9 | 6 | none |
| 10/interactive/1c/closed/one-changing/t0 (main) | 2 | 3248.3 | 3327.2 | 3169.3 | 9 | 7 | none |
| 10/interactive/1c/closed/all-changing/t0 (main) | 2 | 1743.5 | 1793.3 | 1693.7 | 9 | 7 | none |
| 10/interactive/3c/closed/one-changing/t0 (main) | 3 | 1959.2 | 2117.7 | 1816.0 | 9 | 6 | none |
| 10/interactive/3c/closed/all-changing/t0 (main) | 2 | 1999.0 | 2117.4 | 1880.5 | 9 | 7 | none |
| 1/headless/1c/closed/one-changing/t0 (main) | 4 | 1560.9 | 1745.4 | 1401.0 | 9 | 5 | none |
| 1/headless/1c/closed/all-changing/t0 (main) | 4 | 1884.3 | 2045.5 | 1733.8 | 9 | 5 | none |
| 1/headless/3c/closed/one-changing/t0 (main) | 4 | 241.2 | 290.1 | 151.5 | 9 | 5 | none |
| 1/headless/3c/closed/all-changing/t0 (main) | 4 | 777.0 | 813.4 | 762.5 | 9 | 5 | none |
| 10/headless/1c/closed/one-changing/t0 (main) | 0 | n/a | n/a | n/a | 9 | 9 | none |
| 10/headless/1c/closed/all-changing/t0 (main) | 1 | 3337.0 | 3337.0 | 3337.0 | 9 | 8 | none |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-1s) | 0 | n/a | n/a | n/a | 9 | 9 | none |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-1s) | 2 | 2347.4 | 3046.7 | 1648.1 | 9 | 7 | none |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-lock) [non-default env] | 0 | n/a | n/a | n/a | 9 | 9 | none |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-lock) [non-default env] | 2 | 1251.5 | 1260.3 | 1242.7 | 9 | 7 | none |
| 10/headless/3c/closed/one-changing/t0 (h10-headless-3c) | 0 | n/a | n/a | n/a | 9 | 9 | none |
| 10/headless/3c/closed/all-changing/t0 (h10-headless-3c) | 2 | 2561.5 | 3745.0 | 1378.0 | 9 | 7 | none |
| 50/interactive/1c/closed/one-changing/t0 (h50-interactive) | 0 | n/a | n/a | n/a | 9 | 9 | none |
| 50/interactive/3c/closed/one-changing/t0 (h50-interactive) | 0 | n/a | n/a | n/a | 9 | 9 | none |

Detection/collection/model/render split: unknown; the native TUI exposes no stage timestamps.
Unselected worktrees: no matching display state in the Work view (not applicable).

## Change locality (idle window; git launches per unchanged worktree)

| Cell | changed | git on changed | per unchanged worktree (min/median/max) | captures per unchanged | hydra adapter runs | adapter wall ms (median/max) |
|---|---|---|---|---|---:|---|
| 1/interactive/1c/closed/quiescent/t0 (main) | - | - | 4/4/4 | 0 | 8 | 230.0/470.0 |
| 1/interactive/1c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 4} | None/None/None | None | 8 | 215.0/470.0 |
| 1/interactive/1c/closed/all-changing/t0 (main) | i10-h00 | {'i10-h00': 4} | None/None/None | None | 8 | 205.0/450.0 |
| 1/interactive/1c/closed/sustained-output/t0 (main) | - | - | 4/4/4 | 0 | 8 | 225.0/430.0 |
| 1/interactive/1c/open/quiescent/t0 (main) | - | - | 4/4/4 | 0 | 12 | 90.0/590.0 |
| 1/interactive/1c/open/sustained-output/t0 (main) | - | - | 4/4/4 | 0 | 12 | 100.0/560.0 |
| 1/interactive/3c/closed/quiescent/t0 (main) | - | - | 12/12/12 | 0 | 24 | 80.0/540.0 |
| 1/interactive/3c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 12} | None/None/None | None | 24 | 225.0/550.0 |
| 1/interactive/3c/closed/all-changing/t0 (main) | i10-h00 | {'i10-h00': 12} | None/None/None | None | 24 | 120.0/670.0 |
| 1/interactive/3c/closed/sustained-output/t0 (main) | - | - | 12/12/12 | 0 | 24 | 210.0/450.0 |
| 10/interactive/1c/closed/quiescent/t0 (main) | - | - | 2/2.0/3 | 0 | 6 | 80.0/2250.0 |
| 10/interactive/1c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 2} | 2/2/2 | 0 | 6 | 70.0/2290.0 |
| 10/interactive/1c/closed/all-changing/t0 (main) | i10-h00,i10-h01,i10-h02,i10-h03,i10-h04,i10-h05,i10-h06,i10-h07,i10-h08,i10-h09 | {'i10-h00': 2, 'i10-h01': 2, 'i10-h02': 2, 'i10-h03': 2, 'i10-h04': 2, 'i10-h05': 2, 'i10-h06': 2, 'i10-h07': 2, 'i10-h08': 2, 'i10-h09': 2} | None/None/None | None | 6 | 50.0/1990.0 |
| 10/interactive/1c/closed/sustained-output/t0 (main) | - | - | 2/2.0/2 | 0 | 6 | 50.0/2110.0 |
| 10/interactive/1c/open/quiescent/t0 (main) | - | - | 3/3.0/3 | 0 | 11 | 80.0/2270.0 |
| 10/interactive/1c/open/sustained-output/t0 (main) | - | - | 2/2.0/2 | 0 | 10 | 90.0/2030.0 |
| 10/interactive/3c/closed/quiescent/t0 (main) | - | - | 6/6.0/6 | 0 | 18 | 50.0/2200.0 |
| 10/interactive/3c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 8} | 8/8/8 | 0 | 20 | 100.0/2180.0 |
| 10/interactive/3c/closed/all-changing/t0 (main) | i10-h00,i10-h01,i10-h02,i10-h03,i10-h04,i10-h05,i10-h06,i10-h07,i10-h08,i10-h09 | {'i10-h00': 6, 'i10-h01': 6, 'i10-h02': 6, 'i10-h03': 6, 'i10-h04': 6, 'i10-h05': 6, 'i10-h06': 6, 'i10-h07': 6, 'i10-h08': 6, 'i10-h09': 6} | None/None/None | None | 18 | 80.0/2240.0 |
| 10/interactive/3c/closed/sustained-output/t0 (main) | - | - | 6/6.0/6 | 0 | 18 | 100.0/2590.0 |
| 1/headless/1c/closed/quiescent/t0 (main) | - | - | 4/4/4 | 0 | 8 | 305.0/680.0 |
| 1/headless/1c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 4} | None/None/None | None | 8 | 395.0/1020.0 |
| 1/headless/1c/closed/all-changing/t0 (main) | i10-h00 | {'i10-h00': 4} | None/None/None | None | 8 | 235.0/660.0 |
| 1/headless/1c/closed/sustained-output/t0 (main) | - | - | 4/4/4 | 0 | 8 | 270.0/590.0 |
| 1/headless/3c/closed/quiescent/t0 (main) | - | - | 12/12/12 | 0 | 24 | 280.0/680.0 |
| 1/headless/3c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 12} | None/None/None | None | 24 | 140.0/640.0 |
| 1/headless/3c/closed/all-changing/t0 (main) | i10-h00 | {'i10-h00': 12} | None/None/None | None | 24 | 225.0/450.0 |
| 1/headless/3c/closed/sustained-output/t0 (main) | - | - | 12/12/12 | 0 | 24 | 90.0/480.0 |
| 10/headless/1c/closed/quiescent/t0 (main) | - | - | 2/2.0/2 | 0 | 5 | 120.0/4180.0 |
| 10/headless/1c/closed/one-changing/t0 (main) | i10-h00 | {'i10-h00': 1} | 1/1/1 | 0 | 6 | 120.0/4330.0 |
| 10/headless/1c/closed/all-changing/t0 (main) | i10-h00,i10-h01,i10-h02,i10-h03,i10-h04,i10-h05,i10-h06,i10-h07,i10-h08,i10-h09 | {'i10-h00': 1, 'i10-h01': 1, 'i10-h02': 1, 'i10-h03': 1, 'i10-h04': 1, 'i10-h05': 1, 'i10-h06': 1, 'i10-h07': 1, 'i10-h08': 1, 'i10-h09': 1} | None/None/None | None | 5 | 100.0/5150.0 |
| 10/headless/1c/closed/quiescent/t0 (h10-headless-1s) | - | - | 2/2.0/2 | 0 | 6 | 80.0/2540.0 |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-1s) | i10-h00 | {'i10-h00': 2} | 2/2/2 | 0 | 6 | 95.0/2840.0 |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-1s) | i10-h00,i10-h01,i10-h02,i10-h03,i10-h04,i10-h05,i10-h06,i10-h07,i10-h08,i10-h09 | {'i10-h00': 2, 'i10-h01': 2, 'i10-h02': 2, 'i10-h03': 2, 'i10-h04': 2, 'i10-h05': 2, 'i10-h06': 2, 'i10-h07': 2, 'i10-h08': 2, 'i10-h09': 2} | None/None/None | None | 6 | 70.0/2470.0 |
| 10/headless/1c/closed/quiescent/t0 (h10-headless-lock) [non-default env] | - | - | 2/2.0/2 | 0 | 6 | 105.0/2680.0 |
| 10/headless/1c/closed/one-changing/t0 (h10-headless-lock) [non-default env] | i10-h00 | {'i10-h00': 2} | 2/2/2 | 0 | 6 | 75.0/2450.0 |
| 10/headless/1c/closed/all-changing/t0 (h10-headless-lock) [non-default env] | i10-h00,i10-h01,i10-h02,i10-h03,i10-h04,i10-h05,i10-h06,i10-h07,i10-h08,i10-h09 | {'i10-h00': 2, 'i10-h01': 2, 'i10-h02': 2, 'i10-h03': 2, 'i10-h04': 2, 'i10-h05': 2, 'i10-h06': 2, 'i10-h07': 2, 'i10-h08': 2, 'i10-h09': 2} | None/None/None | None | 6 | 50.0/2340.0 |
| 10/headless/3c/closed/quiescent/t0 (h10-headless-3c) | - | - | 6/6.0/6 | 0 | 18 | 70.0/2560.0 |
| 10/headless/3c/closed/one-changing/t0 (h10-headless-3c) | i10-h00 | {'i10-h00': 6} | 6/6/7 | 0 | 18 | 80.0/2680.0 |
| 10/headless/3c/closed/all-changing/t0 (h10-headless-3c) | i10-h00,i10-h01,i10-h02,i10-h03,i10-h04,i10-h05,i10-h06,i10-h07,i10-h08,i10-h09 | {'i10-h00': 6, 'i10-h01': 6, 'i10-h02': 6, 'i10-h03': 6, 'i10-h04': 6, 'i10-h05': 6, 'i10-h06': 6, 'i10-h07': 6, 'i10-h08': 6, 'i10-h09': 6} | None/None/None | None | 18 | 110.0/3430.0 |
| 50/interactive/1c/closed/quiescent/t0 (h50-interactive) | - | - | 1/1.0/1 | 0 | 5 | 40.0/50.0 |
| 50/interactive/1c/closed/one-changing/t0 (h50-interactive) | i10-h00 | {'i10-h00': 1} | 0/1/1 | 0 | 5 | 75.0/90.0 |
| 50/interactive/1c/open/quiescent/t0 (h50-interactive) | - | - | 1/1.0/1 | 0 | 9 | 115.0/180.0 |
| 50/interactive/3c/closed/quiescent/t0 (h50-interactive) | - | - | 3/3.0/3 | 0 | 15 | 115.0/180.0 |
| 50/interactive/3c/closed/one-changing/t0 (h50-interactive) | i10-h00 | {'i10-h00': 3} | 1/2/3 | 0 | 14 | 120.0/190.0 |

## Output pressure (sustained-output cells)

| Cell | producers x B/s | peak backlog B | EAGAIN | queue non-empty ms (max) | stop late ms (max) | recovery ms (max) | RSS growth tmux/TUI MiB | PTY reads idle |
|---|---|---:|---:|---:|---:|---:|---|---:|
| 1/interactive/1c/closed/sustained-output/t0 (main) | 1 x 4096 | 128 | 0 | 185.8 | 5.4 | 0.0 | 2.1/0.0 | 0 |
| 1/interactive/1c/open/sustained-output/t0 (main) | 1 x 4096 | 128 | 0 | 169.4 | 0.6 | 0.0 | 0.1/-7.8 | 193 |
| 1/interactive/3c/closed/sustained-output/t0 (main) | 1 x 4096 | 128 | 0 | 217.9 | 0.4 | 0.0 | 0.0/0.0 | 0 |
| 10/interactive/1c/closed/sustained-output/t0 (main) | 10 x 4096 | 128 | 0 | 144.8 | 1.0 | 0.0 | 23.2/0.0 | 0 |
| 10/interactive/1c/open/sustained-output/t0 (main) | 10 x 4096 | 128 | 0 | 142.9 | 3.3 | 0.0 | 16.4/-4.3 | 195 |
| 10/interactive/3c/closed/sustained-output/t0 (main) | 10 x 4096 | 128 | 0 | 185.1 | 1.0 | 0.0 | 0.0/0.0 | 0 |
| 1/headless/1c/closed/sustained-output/t0 (main) | 1 x 1024 | 128 | 0 | 45.5 | 3.3 | 0.0 | n/a/0.0 | 0 |
| 1/headless/3c/closed/sustained-output/t0 (main) | 1 x 1024 | 128 | 0 | 35.6 | 0.4 | 0.0 | n/a/0.0 | 0 |

Result retention after the output phases (headless owners retain every event after the worker exits):

- main--baseline-t0-h1-interactive: verified; {"retained_output_events_max_per_worker": 1728, "final_drain_wait_ns": [174729], "owner_exit_after_finish_ns": []}
- main--baseline-t0-h10-interactive: verified; {"retained_output_events_max_per_worker": 1728, "final_drain_wait_ns": [140699, 124203, 186194, 164402, 153745, 122354, 121765, 147348, 149852, 154753], "owner_exit_after_finish_ns": []}
- main--baseline-t0-h1-headless: verified; {"retained_output_events_max_per_worker": 288, "final_drain_wait_ns": [128780], "owner_exit_after_finish_ns": [43050106978]}
- main--baseline-t0-h10-headless: failed; {"error": "OSError(6, 'No such device or address')", "worker_artifact_bytes_verified": {"i10-h00": false, "i10-h01": false, "i10-h02": false, "i10-h03": false, "i10-h04": false, "i10-h05": false, "i10-h06": false, "i10-h07": false, "i10-h08": false, "i10-h09": false}, "owner_exits": [125, null, 125, 125, null, 125, 125, 125, 125, 125]}
- h10-headless-1s--baseline-t0-h10-headless: failed; {"error": "OSError(6, 'No such device or address')", "worker_artifact_bytes_verified": {"i10-h00": false, "i10-h01": false, "i10-h02": false, "i10-h03": false, "i10-h04": false, "i10-h05": false, "i10-h06": false, "i10-h07": false, "i10-h08": false, "i10-h09": false}, "owner_exits": [125, 125, 125, 125, 125, 125, null, 125, null, 125]}
- h10-headless-lock--baseline-t0-h10-headless: failed; {"error": "OSError(6, 'No such device or address')", "worker_artifact_bytes_verified": {"i10-h00": false, "i10-h01": false, "i10-h02": false, "i10-h03": false, "i10-h04": false, "i10-h05": false, "i10-h06": false, "i10-h07": false, "i10-h08": false, "i10-h09": false}, "owner_exits": [125, 125, 125, 125, 125, 125, 125, 125, 125, 125]}
- h10-headless-3c--baseline-t0-h10-headless: failed; {"error": "RuntimeError('supervised worker result failed')", "worker_artifact_bytes_verified": {"i10-h00": true, "i10-h01": true, "i10-h02": true, "i10-h03": true, "i10-h04": true, "i10-h05": true, "i10-h06": true, "i10-h07": true, "i10-h08": true, "i10-h09": true}, "owner_exits": [125, 125, 0, 125, 125, null, 125, 125, 0, 0]}
- h10-headless-output--baseline-t0-h10-headless: failed; {"error": "OSError(6, 'No such device or address')", "worker_artifact_bytes_verified": {"i10-h00": false, "i10-h01": false, "i10-h02": false, "i10-h03": false, "i10-h04": false, "i10-h05": false, "i10-h06": false, "i10-h07": false, "i10-h08": false, "i10-h09": false}, "owner_exits": [125, 125, 125, 125, 125, 125, 125, 125, null, 125]}
- h50--baseline-t0-h50-interactive: verified; {"retained_output_events_max_per_worker": 0, "final_drain_wait_ns": [14056720, 294064, 168453, 217559, 244847, 240957, 202186, 144579, 168964, 218632, 215065, 111927, 136861, 189557, 207553, 137740, 213289, 140757, 138929, 128727, 139279, 226348, 139193, 164351, 123682, 203095, 234366, 171848, 152223, 118969, 121993, 135681, 106714, 209809, 94472, 169355, 142991, 125068, 125436, 105444, 291867, 11
- h50--baseline-t0-h50-headless: failed; {"error": "RuntimeError('supervised worker result failed')", "worker_artifact_bytes_verified": {"i10-h00": true, "i10-h01": true, "i10-h02": true, "i10-h03": true, "i10-h04": true, "i10-h05": true, "i10-h06": true, "i10-h07": true, "i10-h08": true, "i10-h09": true, "i10-h10": true, "i10-h11": true, "i10-h12": true, "i10-h13": true, "i10-h14": true, "i10-h15": true, "i10-h16": true, "i10-h17": true
- h50-interactive--baseline-t0-h50-interactive: verified; {"retained_output_events_max_per_worker": 0, "final_drain_wait_ns": [52950086, 5068497, 205668, 199514, 248230, 223626, 193033, 113682, 246404, 222252, 174964, 226604, 159292, 174492, 190195, 164889, 158913, 214222, 145308, 173201, 120478, 155157, 220688, 117576, 130698, 137823, 140516, 166887, 180678, 109981, 116414, 127798, 198440, 130848, 122664, 121574, 130023, 122076, 245173, 266160, 163734, 
- h50-headless--baseline-t0-h50-headless: failed; {"error": "RuntimeError('supervised worker result failed')", "worker_artifact_bytes_verified": {"i10-h00": true, "i10-h01": true, "i10-h02": true, "i10-h03": true, "i10-h04": true, "i10-h05": true, "i10-h06": true, "i10-h07": true, "i10-h08": true, "i10-h09": true, "i10-h10": true, "i10-h11": true, "i10-h12": true, "i10-h13": true, "i10-h14": true, "i10-h15": true, "i10-h16": true, "i10-h17": true

Dropped/coalesced display updates: not exposed by the renderer (null, see cell pressure reason).

## Fleet scaling and startup

| Fixture | heads | mode | provisioning ms | spawn ms (median) | client time-to-usable ms | cleanup ok | errors |
|---|---:|---|---:|---:|---|---|---|
| main--baseline-t0-h1-interactive | 1 | interactive | 2736 | 2087 | 430,515,552,505,466 | True | 0 |
| main--baseline-t0-h10-interactive | 10 | interactive | 23315 | 2180 | 2095,1916,2002,2045,2085 | True | 0 |
| main--baseline-t0-h1-headless | 1 | headless | 2836 | 1549 | 383,424,430,476 | True | 0 |
| main--baseline-t0-h10-headless | 10 | headless | 51040 | 3737 | 4726 | False | 2 |
| h10-headless-1s--baseline-t0-h10-headless | 10 | headless | 43484 | 2883 | 2206 | False | 2 |
| h10-headless-lock--baseline-t0-h10-headless | 10 | headless | 30722 | 2073 | 2690 | False | 2 |
| h10-headless-3c--baseline-t0-h10-headless | 10 | headless | 32620 | 2151 | 2310,2966,2998 | True | 0 |
| h10-headless-output--baseline-t0-h10-headless | 10 | headless | 31603 | 2103 | 2958 | True | 2 |
| h50--baseline-t0-h50-interactive | 50 | interactive | 177002 | 3629 | 8093 | True | 1 |
| h50--baseline-t0-h50-headless | 50 | headless | 249116 | 3142 |  | False | 1 |
| h50-interactive--baseline-t0-h50-interactive | 50 | interactive | 127700 | 2442 | 9173,9861,9074,9121,9486 | False | 1 |
| h50-headless--baseline-t0-h50-headless | 50 | headless | 321525 | 4424 |  | False | 1 |

## Observer overhead

- main--baseline-t0-h1-interactive: shim adds 4.89 ms per counted launch (median of 12); uptime clock offset bound [-1471005, 8567931] ns
- main--baseline-t0-h10-interactive: shim adds 2.47 ms per counted launch (median of 12); uptime clock offset bound [-1155347, 8881315] ns
- main--baseline-t0-h1-headless: shim adds 4.62 ms per counted launch (median of 12); uptime clock offset bound [-1336521, 8695955] ns
- main--baseline-t0-h10-headless: shim adds 4.01 ms per counted launch (median of 12); uptime clock offset bound [-1007630, 9023939] ns
- h10-headless-1s--baseline-t0-h10-headless: shim adds 5.16 ms per counted launch (median of 12); uptime clock offset bound [-1117732, 8916507] ns
- h10-headless-lock--baseline-t0-h10-headless: shim adds 3.88 ms per counted launch (median of 12); uptime clock offset bound [-1292824, 8727931] ns
- h10-headless-3c--baseline-t0-h10-headless: shim adds 4.21 ms per counted launch (median of 12); uptime clock offset bound [-3257038, 6769699] ns
- h10-headless-output--baseline-t0-h10-headless: shim adds 6.05 ms per counted launch (median of 12); uptime clock offset bound [-3292638, 6727726] ns
- h50--baseline-t0-h50-interactive: shim adds 4.11 ms per counted launch (median of 12); uptime clock offset bound [-2384653, 7638693] ns
- h50--baseline-t0-h50-headless: shim adds 4.46 ms per counted launch (median of 12); uptime clock offset bound [-4309978, 5711435] ns
- h50-interactive--baseline-t0-h50-interactive: shim adds 4.51 ms per counted launch (median of 12); uptime clock offset bound [-2337193, 7683511] ns
- h50-headless--baseline-t0-h50-headless: shim adds 7.89 ms per counted launch (median of 12); uptime clock offset bound [-7760150, 2261305] ns

## Unsupported or unmeasured cells

- {'heads': 1, 'mode': 'interactive', 'clients': 1, 'preview': 'open', 'case': 'one-changing', 'status': 'not planned', 'reason': 'preview-open cells cover quiescent and sustained-output only'}
- {'heads': 1, 'mode': 'interactive', 'clients': 1, 'preview': 'open', 'case': 'all-changing', 'status': 'not planned', 'reason': 'preview-open cells cover quiescent and sustained-output only'}
- {'heads': 1, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'quiescent', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 1, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'one-changing', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 1, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'all-changing', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 1, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'sustained-output', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 10, 'mode': 'interactive', 'clients': 1, 'preview': 'open', 'case': 'one-changing', 'status': 'not planned', 'reason': 'preview-open cells cover quiescent and sustained-output only'}
- {'heads': 10, 'mode': 'interactive', 'clients': 1, 'preview': 'open', 'case': 'all-changing', 'status': 'not planned', 'reason': 'preview-open cells cover quiescent and sustained-output only'}
- {'heads': 10, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'quiescent', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 10, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'one-changing', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 10, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'all-changing', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 10, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'sustained-output', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 1, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'quiescent', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'one-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'all-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'sustained-output', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'quiescent', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'one-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'all-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 1, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'sustained-output', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'quiescent', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'one-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'all-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'sustained-output', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'quiescent', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'one-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'all-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 10, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'sustained-output', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'mode': 'remote-headless', 'status': 'unmeasured', 'reason': 'no loopback receiver stand-in exists in this harness; a live verified fleet host is required'}
- {'platform': 'darwin', 'status': 'not measured', 'reason': 'this run executed on Linux only'}
- {'heads': 50, 'mode': 'interactive', 'clients': 1, 'preview': 'open', 'case': 'one-changing', 'status': 'not planned', 'reason': 'preview-open cells cover quiescent and sustained-output only'}
- {'heads': 50, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'quiescent', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 50, 'mode': 'interactive', 'clients': 3, 'preview': 'open', 'case': 'one-changing', 'status': 'not planned', 'reason': 'multi-client terminal attachment is outside this slice'}
- {'heads': 50, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'quiescent', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 50, 'mode': 'headless', 'clients': 1, 'preview': 'open', 'case': 'one-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 50, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'quiescent', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}
- {'heads': 50, 'mode': 'headless', 'clients': 3, 'preview': 'open', 'case': 'one-changing', 'status': 'unsupported', 'reason': 'headless workers have no terminal to attach'}

## I2 attention qualification (annotated local fixture cases)

Cases 13: false positives 0, missed signals 0, unsupported-by-design silent 1, unknown matched 4/4. Complete: True.

## Proposed budgets (Linux-container provisional; not frozen)

```json
{
 "status": "provisional: derived from this first Linux-container baseline with a 1.5x margin; not frozen release budgets",
 "idle_per_heads": {
  "1": {
   "tui_cpu_percent_one_core_max": 2.062706851079248,
   "helpers_cpu_percent_one_core_max": 33.003309617267966,
   "helper_launches_per_minute_max": 720.0,
   "tui_resident_bytes_max": 69599232.0,
   "git_scans_per_unchanged_worktree_per_idle_window_observed": {
    "n": 2,
    "min": 4,
    "median": 4.0,
    "p95": null,
    "p99": null,
    "max": 4,
    "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
   }
  },
  "10": {
   "tui_cpu_percent_one_core_max": 6.376108032880833,
   "helpers_cpu_percent_one_core_max": 149.09398555604923,
   "helper_launches_per_minute_max": 765.0,
   "tui_resident_bytes_max": 43474944.0,
   "git_scans_per_unchanged_worktree_per_idle_window_observed": {
    "n": 30,
    "min": 2,
    "median": 2.0,
    "p95": 3,
    "p99": null,
    "max": 3,
    "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
   }
  },
  "50": {
   "tui_cpu_percent_one_core_max": 13.879569348543512,
   "helpers_cpu_percent_one_core_max": 468.3416846393668,
   "helper_launches_per_minute_max": 731.25,
   "tui_resident_bytes_max": 37337088.0,
   "git_scans_per_unchanged_worktree_per_idle_window_observed": {
    "n": 50,
    "min": 1,
    "median": 1.0,
    "p95": 1,
    "p99": null,
    "max": 1,
    "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
   }
  }
 },
 "interaction_latency_ms": {
  "observed": {
   "n": 164,
   "min": 0.418589,
   "median": 6.2703705,
   "p95": 38.459274,
   "p99": 480.227788,
   "max": 3912.774334,
   "tail_reason": null
  },
  "proposed_p95_ceiling_ms": 5869.1615010000005,
  "per_heads": {
   "1": {
    "navigation": {
     "observed": {
      "n": 18,
      "min": 0.418589,
      "median": 0.6993214999999999,
      "p95": null,
      "p99": null,
      "max": 236.661389,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 354.99208350000004,
     "not_observed": 0
    },
    "search": {
     "observed": {
      "n": 18,
      "min": 1.218527,
      "median": 1.8306705,
      "p95": null,
      "p99": null,
      "max": 8.33708,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 12.50562,
     "not_observed": 0
    },
    "resize": {
     "observed": {
      "n": 18,
      "min": 5.443966,
      "median": 13.5356515,
      "p95": null,
      "p99": null,
      "max": 31.55024,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 47.325359999999996,
     "not_observed": 0
    },
    "cancel": {
     "observed": {
      "n": 18,
      "min": 20.511874,
      "median": 20.8664085,
      "p95": null,
      "p99": null,
      "max": 28.060891,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 42.091336500000004,
     "not_observed": 0
    }
   },
   "10": {
    "navigation": {
     "observed": {
      "n": 19,
      "min": 0.421791,
      "median": 0.728784,
      "p95": null,
      "p99": null,
      "max": 236.118559,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 354.1778385,
     "not_observed": 0
    },
    "search": {
     "observed": {
      "n": 19,
      "min": 1.188321,
      "median": 1.75431,
      "p95": null,
      "p99": null,
      "max": 4.182452,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 6.273677999999999,
     "not_observed": 0
    },
    "resize": {
     "observed": {
      "n": 19,
      "min": 5.562295,
      "median": 11.479309,
      "p95": null,
      "p99": null,
      "max": 38.459274,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 57.688911000000004,
     "not_observed": 0
    },
    "cancel": {
     "observed": {
      "n": 19,
      "min": 20.646663,
      "median": 21.005134,
      "p95": null,
      "p99": null,
      "max": 480.227788,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 720.341682,
     "not_observed": 0
    }
   },
   "50": {
    "navigation": {
     "observed": {
      "n": 5,
      "min": 0.565156,
      "median": 0.879046,
      "p95": null,
      "p99": null,
      "max": 225.066066,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 337.599099,
     "not_observed": 0
    },
    "search": {
     "observed": {
      "n": 4,
      "min": 2.317397,
      "median": 3.76786,
      "p95": null,
      "p99": null,
      "max": 6.026044,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 9.039066,
     "not_observed": 1
    },
    "resize": {
     "observed": {
      "n": 5,
      "min": 7.478805,
      "median": 11.257863,
      "p95": null,
      "p99": null,
      "max": 25.041267,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 37.5619005,
     "not_observed": 0
    },
    "cancel": {
     "observed": {
      "n": 2,
      "min": 2023.796202,
      "median": 2968.285268,
      "p95": null,
      "p99": null,
      "max": 3912.774334,
      "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
     },
     "proposed_max_ms": 5869.1615010000005,
     "not_observed": 3
    }
   }
  },
  "note": "deadline misses must stay zero; the proposed ceiling is 1.5x the observed maximum"
 },
 "update_latency_ms": {
  "observed": {
   "n": 45,
   "min": 151.477337,
   "median": 1648.069866,
   "p95": 3327.16342,
   "p99": null,
   "max": 3745.024957,
   "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
  },
  "proposed_max_ms": 5617.5374355,
  "per_heads": {
   "1": {
    "observed": {
     "n": 31,
     "min": 151.477337,
     "median": 1401.006551,
     "p95": 1854.222,
     "p99": null,
     "max": 2045.454332,
     "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
    },
    "proposed_max_ms": 3068.181498,
    "changes_not_seen_in_window": 41
   },
   "10": {
    "observed": {
     "n": 14,
     "min": 1377.99919,
     "median": 2038.300795,
     "p95": null,
     "p99": null,
     "max": 3745.024957,
     "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
    },
    "proposed_max_ms": 5617.5374355,
    "changes_not_seen_in_window": 76
   },
   "50": {
    "observed": {
     "n": 0,
     "min": null,
     "median": null,
     "p95": null,
     "p99": null,
     "max": null,
     "tail_reason": "no observations"
    },
    "proposed_max_ms": null,
    "changes_not_seen_in_window": 18
   }
  },
  "note": "selected-head changed-file count; bounded below by the 2 s refresh period"
 },
 "observed_load_average_1m": {
  "min": 0.10205078125,
  "max": 24.20166015625,
  "scope": "1-minute load average recorded before and after each fixture on the shared host"
 },
 "recovery_after_output_stop_ms": {
  "observed": {
   "n": 35,
   "min": 0.0,
   "median": 0.0,
   "p95": 0.0,
   "p99": null,
   "max": 0.0,
   "tail_reason": "p95 needs 20 samples and p99 needs 100; the observed maximum is not a worst-case bound"
  },
  "proposed_max_ms": 0.0,
  "scope": "producer backlog drained after the declared end at the declared rates; display catch-up is unmeasured"
 },
 "locality": {
  "target": "unchanged worktrees should incur zero repeated git scans; the observed per-refresh scan of every worktree is the current cost, not a budget"
 }
}
```

## Limitations

- Deterministic stand-in agents; no authenticated live-agent trials.
- Shared host load is uncontrolled; a short-run maximum is not a worst-case bound.
- PTY frame completion, not visible terminal presentation; the shim counts only git, tmux, hydra, hydra-core and hydra-fleet launches.
- Remote headless, macOS, disconnect/reconnect under pressure and T2/T3 remain unmeasured.
