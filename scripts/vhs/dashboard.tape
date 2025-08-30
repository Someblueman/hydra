Output "assets/demos/dashboard.gif"
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

Type "hydra dashboard --panes-per-session 2"
Enter
Sleep 5000ms
#Type "tmux attach -t hydra-dashboard"
#Enter
#Sleep 1200ms
Hide
Type "tmux detach-client"
Enter
Sleep 1200ms
Show
