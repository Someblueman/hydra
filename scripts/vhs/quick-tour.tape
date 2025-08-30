Output "assets/demos/quick-tour.gif"
Set Shell zsh
Set FontSize 16
Set Width 1200
Set Height 700
Set Theme "Dracula"
Set Padding 16
Set Framerate 60
Set CursorBlink false
Set TypingSpeed 60ms

# Set environment for an isolated demo
# Environment may be provided by wrapper script (run-quick-tour.sh)
# Expect to already be inside a demo git repo and HYDRA_HOME set

# Show version
Type "hydra --version"
Enter
Sleep 800ms

# Create a new head with a dev layout
Type "hydra spawn feature-auth -l dev"
Enter
Sleep 1500ms

# Create a second head to make dashboard interesting
Type "hydra spawn feature-ui -l full"
Enter
Sleep 1500ms

# List and check status
Type "hydra list"
Enter
Sleep 800ms
Type "hydra status"
Enter
Sleep 800ms

# Show tmux sessions (proof of session)
Type "tmux ls"
Enter
Sleep 800ms

# Dashboard (create, then attach explicitly; debug, detach)
Type "hydra dashboard"
Enter
Sleep 1200ms
Type "Quit/Detach using Q"
Sleep 500ms
Hide
Backspace 19
# Avoid keybindings in demo
Type "tmux detach-client"
Enter
Sleep 800ms
Show

# Clean up the demo heads (HYDRA_NONINTERACTIVE is set by wrapper)
Type "hydra kill feature-auth"
Enter
Sleep 1000ms

Type "hydra kill feature-ui"
Enter
Sleep 800ms

# End of demo
