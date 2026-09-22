# Material3UI Shell Quickshell profile

Run `quickshell -c material3ui`; its entrypoint is `shell.qml`. The panels are deliberately split into small QML components so they are easy to replace with integrations for NetworkManager, MPRIS, PipeWire, dunst and cliphist.

For generated colours, Matugen writes `GeneratedColors.qml` beside these files. The initial palette in `shell.qml` makes the profile bootable before a wallpaper is selected.
