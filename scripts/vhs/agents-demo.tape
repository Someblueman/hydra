Output "assets/demos/agents-demo.gif"
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

# Spawn mixed agents (no AI launch; tags appear in list/status)
Type "hydra spawn exp --agents claude:2,codex:1"
Enter
Sleep 1600ms
Type "hydra list"
Enter
Sleep 1600ms
Type "hydra status"
Enter
Sleep 1600ms
Type "hydra switch"
Enter
Sleep 1400
Type "exp-2"
Enter


# Cleanup
Type "hydra kill --all --force"
Enter
Sleep 800ms
