Output assets/demos/quick-tour.gif
Output assets/demos/quick-tour.mp4
Require hydra
Set Shell "bash"
Set FontSize 20
Set Width 1440
Set Height 800
Set Padding 20
Set Theme "Catppuccin Mocha"
Set WindowBar Colorful
Set TypingSpeed 25ms
Set Framerate 15
Hide
Type 'export PS1="$ " PROMPT_COMMAND=""; cd "$HYDRA_DEMO_PROJECT"; clear'
Enter
Sleep 1s
Show
Type "# Native mission control | Agents, tasks, and worktrees"
Enter
Type "hydra tui"
Enter
Sleep 5s
Type "/search"
Enter
Sleep 2s
Type "p"
Sleep 5s
Enter
Sleep 4s
Type "q"
Sleep 1s
Ctrl+L
Type "# Launch work on a remote host over SSH"
Enter
Type 'hydra fleet spawn build --project "$REMOTE_PROJECT" -- "$REMOTE_HEAD" --no-agent'
Enter
Sleep 4s
Type "hydra fleet tui"
Enter
Sleep 5s
Hide
Type "a"
Sleep 2s
Type "export PS1='build $ '; tmux set-option status-right 'build | Linux'; clear"
Enter
Sleep 1s
Show
Type "uname -s"
Enter
Sleep 3s
Ctrl+B
Type "d"
Sleep 2s
Enter
Sleep 4s
Type "q"
Sleep 2s
