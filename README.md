# Material3UI Shell

**v1.0.0** · Arch Linux · Hyprland · Quickshell

Material3UI Shell is a Material 3 shell for **Hyprland**, built with [Quickshell](https://quickshell.outfoxxed.me/) and coloured by [Matugen](https://github.com/InioX/matugen). It is a real dotfiles starter: the bar, launcher, control centre, notifications, clipboard, power menu, and Android-inspired settings UI live in QML, while Hyprland owns window management.

## Included

- Original Midnight Dunes default wallpaper, restored automatically at login
- Material 3 adaptive palette generated from the current wallpaper
- Expandable Dynamic Island for media controls and quick settings access
- Top bar: launcher, workspaces, active window/media area, background tasks, calendar, status and profile
- App launcher for LibreWolf, Thunar, terminal, settings, and user commands
- Control centre with editable Wi-Fi/Bluetooth controls, tiles, volume balancing, screenshot/recording actions, clipboard and notifications
- Settings categories for connectivity, wallpaper/style, display, audio, notifications, security/polkit, location/weather, bar editor, apps, battery, accessibility, updates, accounts, and about
- Native Quickshell global shortcuts plus Hyprland bindings and sensible preferred applications (Thunar, LibreWolf, foot)
- Live bar editor: enable or hide launcher, workspaces, active-window title, Dynamic Island, screenshot, clipboard, notifications, quick settings, clock, battery, and profile widgets
- Bar appearance controls for compositor-backed blur/translucency, height, and top/bottom placement
- Native Hyprland Qt/QML Polkit authentication agent and Material 3 Hyprlock screen

## Install (Arch Linux only)

Material3UI Shell is deliberately an Arch Linux configuration. Its single interactive installer uses `pacman`, lets you choose the preferred applications and font set, can enable NetworkManager/Bluetooth, and then installs the dotfiles:

```sh
./install.sh
```

The installer uses `pacman --needed`, so Hyprland, Quickshell, Matugen, and every runtime dependency are installed only when missing. It deploys `hyprland.conf`, `hyprlock.conf`, the Material3UI Quickshell profile, Matugen template, helper commands, and a daily user-level update-notification timer. Existing Hyprland and Material3UI configuration is backed up before replacement.

The installer bootstraps [Yay](https://github.com/Jguer/yay) for AUR integrations, then optionally installs [HyprMod](https://github.com/BlueManCZ/hyprmod), a graphical Hyprland editor for keybinds, monitors, rules, workspaces, profiles, and Lua configuration. Open it from Settings → System → HyprMod or with `SUPER+SHIFT+M`.

The installer ships with the original Midnight Dunes wallpaper and restores it on first login. Place additional wallpapers in `~/Pictures/Wallpapers`, then use `material3uictl wallpaper /path/to/wallpaper`. Matugen generates the adaptive palette.

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
