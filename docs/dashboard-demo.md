# Hydra Dashboard Demo

The dashboard feature allows you to view all active Hydra sessions in a single unified view.

## How it Works

The dashboard uses tmux's `join-pane` and `move-pane` commands to temporarily relocate panes from multiple sessions into a single dashboard session. When you exit the dashboard, all panes are restored to their original sessions.

## Technical Implementation

### 1. Pane Collection
When you run `hydra dashboard`, the system:
- Creates a dedicated `hydra-dashboard` session
- Iterates through all active Hydra sessions from the mapping file
- Moves the first pane from each session to the dashboard
- Records the original location for restoration

### 2. Layout Management
The dashboard automatically arranges panes based on count:
- 1 pane: Full screen
- 2 panes: Side by side (even-horizontal)
- 3-4 panes: 2x2 grid (tiled)
- 5+ panes: Optimal grid (tiled)

### 3. Restoration
When exiting (via 'q' key or Ctrl-C):
- Each pane is moved back to its original window/session
- The dashboard session is destroyed
- All mappings are cleaned up

## Usage Example

```sh
# Assuming you have multiple active sessions
$ hydra list
BRANCH              SESSION             STATUS     PATH
feature-auth        feature_auth        active     ../hydra-feature-auth
feature-ui          feature_ui          active     ../hydra-feature-ui
bugfix-api          bugfix_api          active     ../hydra-bugfix-api

# Launch the dashboard
$ hydra dashboard
Dashboard ready. Press 'q' to exit and restore panes.

# The dashboard will show all three sessions in a grid
# Press 'q' to exit and restore all panes
```

## Key Features

1. **Non-disruptive**: Original sessions remain intact, just temporarily missing a pane
2. **Automatic cleanup**: Trap handlers ensure panes are restored even on unexpected exit
3. **Session safety**: If dashboard crashes, panes can be manually moved back
4. **Performance**: Uses native tmux commands for instant pane manipulation

## Limitations

1. Only the first pane from each session is shown
2. Dashboard is read-only by design (for safety)
3. Requires tmux ≥ 3.0 for reliable pane manipulation
4. Maximum useful sessions: ~9 (3x3 grid)

## Error Handling

The dashboard includes several safety mechanisms:
- Prevents multiple dashboard instances
- Validates sessions exist before collecting panes
- Records restoration data before moving panes
- Handles missing sessions gracefully