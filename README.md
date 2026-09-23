# Material3UI Shell

**v0.0.2** · Arch Linux · Hyprland · Quickshell

Material3UI Shell is a Material 3 shell for **Hyprland**, built with [Quickshell](https://quickshell.outfoxxed.me/) and coloured by [Matugen](https://github.com/InioX/matugen). It is a real dotfiles starter: the bar, launcher, control centre, notifications, clipboard, power menu, and Android-inspired settings UI live in QML, while Hyprland owns window management.

## Included

- Material 3 adaptive palette generated from the current wallpaper
- Expandable Dynamic Island for media controls and quick settings access
- Top bar: launcher, workspaces, active window/media area, background tasks, calendar, status and profile
- App launcher for LibreWolf, Thunar, terminal, settings, and user commands
- Control centre with editable Wi-Fi/Bluetooth controls, tiles, volume balancing, screenshot/recording actions, clipboard and notifications
- Settings categories for connectivity, wallpaper/style, display, audio, notifications, security/polkit, location/weather, bar editor, apps, battery, accessibility, updates, accounts, and about
- Native Quickshell global shortcuts plus Hyprland bindings and sensible preferred applications (Thunar, LibreWolf, foot)
- Live bar editor: enable or hide launcher, workspaces, active-window title, Dynamic Island, screenshot, clipboard, notifications, quick settings, clock, battery, and profile widgets
- Bar appearance controls for compositor-backed blur/translucency, height, and top/bottom placement

## Install (Arch Linux only)

Material3UI Shell is deliberately an Arch Linux configuration. Its single interactive installer uses `pacman`, lets you choose the preferred applications and font set, can enable NetworkManager/Bluetooth, and then installs the dotfiles:

```sh
./install.sh
```

Place wallpapers in `~/Pictures/Wallpapers` and run `matugen image /path/to/wallpaper`. This produces `~/.config/quickshell/material3ui/GeneratedColors.qml`; restart Quickshell to apply it.

## Key bindings

`SUPER` opens launcher, `SUPER+C` control centre, `SUPER+N` notifications, `SUPER+S` settings, `SUPER+V` clipboard, `SUPER+P` power menu, `SUPER+Return` terminal, `SUPER+E` Thunar, and `SUPER+B` LibreWolf. See the Hyprland config for workspace and screenshot bindings.

## IPC

All shell controls communicate through the Material3UI Shell IPC target. Use `material3uictl controlCenter`, `material3uictl settings "Sound & vibration"`, `material3uictl wifi off`, or `material3uictl volume 40` from a terminal, key binding, or automation. This keeps external integrations independent of the QML layout.

## System integrations

`material3ui-system` is the service layer used by the shell. It controls Wi-Fi, VPN profiles and Ethernet through NetworkManager (`nmcli`), Bluetooth through BlueZ, reads battery details through UPower, and uses `swww` + Matugen for wallpaper and dynamic colour generation.

```sh
material3uictl wifi on
material3ui-system wifi-connect "Network name" "password"
material3ui-system vpn "My VPN"
material3uictl wallpaper ~/Pictures/Wallpapers/forest.png
material3uictl battery
material3uictl about
```

Clipboard history is captured at Hyprland startup by `wl-paste --watch` into cliphist. `SUPER+V` opens the live history; clicking an entry decodes and copies it. `SUPER+SHIFT+S` saves a selected screenshot to `~/Pictures/Screenshots`, copies it to the clipboard, and reports the saved location. `SUPER+SHIFT+V` starts/stops a selected-area recording in `~/Videos`.
