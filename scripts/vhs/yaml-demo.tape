Output "assets/demos/yaml-demo.gif"
Set Shell bash
Set FontSize 16
Set Width 1200
Set Height 700
Set Theme "Dracula"
Set Padding 16
Set Framerate 60
Set CursorBlink true
Set TypingSpeed 60ms
Require bash
Require tmux
Require git
Require hydra

# Spawn using YAML config (no switch)
Type "hydra spawn yml-demo -l default"
Enter
# Allow enough time for spawn + YAML layout to apply
Sleep 2500ms

# Attach to the created session directly (yml-demo)
Type "tmux attach -t yml-demo"
Enter
Sleep 1800ms

# Show windows and panes from YAML
Type "tmux list-windows"
Enter
Sleep 800ms
Type "tmux list-panes -a"
Enter
Sleep 800ms

# Detach
Type "tmux detach-client"
Enter
Sleep 800ms

# Clean up
Type "hydra kill yml-demo"
Enter
Sleep 800ms
