Output "assets/demos/regenerate.gif"
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

# Spawn a head (no switch)
Type "hydra spawn rehyd -l dev"
Enter
Sleep 1000ms
Type "tmux ls"
Enter
Sleep 800ms

Type "Simulate restart by killing tmux server"
Sleep 2000ms
Hide
Enter
Type "tmux kill-server"
Enter
Sleep 800ms
Show

# Regenerate sessions from mappings
Type "hydra regenerate"
Enter
Sleep 1200ms

# View in dashboard and attach explicitly
Type "hydra dashboard"
Enter
Sleep 1000ms
Type "tmux attach -t rehyd"
Enter
Sleep 1200ms
Type "tmux detach-client"
Enter
Sleep 800ms

# Cleanup
Type "hydra kill rehyd"
Enter
Sleep 800ms

