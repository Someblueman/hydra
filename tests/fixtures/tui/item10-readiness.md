# Item 10 readiness screens

The 1/10/50-head text fixtures were captured from the current native control
centre on 15 September 2026 through `test-tui-pty --item10-session`, using the
existing `Observer`, `fake-hydra.sh` and `HYDRA_TUI_FIXTURE` input mechanism.
The viewport is 120x40 with `LC_ALL=C`. The state rows name owned `i10-hNN`
heads and an `item10-worker` profile. They are rendering fixtures, not measured
provider runs. The 50-head order intentionally leaves worker zero offscreen.

When refreshing them, capture the renderer's actual output and review the
table columns, exact count, selected row and matching selected-head card.
Run `make test-bench-i1` afterward: it checks these retained positive/negative
controls, real installed-state readiness, fixed input deadlines and owned
cleanup. Updating labels in old screenshots alone is not a valid refresh.
