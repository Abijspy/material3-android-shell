# Aura Shell

**v0.0.1** · Arch Linux · Hyprland · Quickshell

Aura is a Material 3 shell for **Hyprland**, built with [Quickshell](https://quickshell.outfoxxed.me/) and coloured by [Matugen](https://github.com/InioX/matugen). It is a real dotfiles starter: the bar, launcher, control centre, notifications, clipboard, power menu, and Android-inspired settings UI live in QML, while Hyprland owns window management.

## Included

- Material 3 adaptive palette generated from the current wallpaper
- Top bar: launcher, workspaces, active window/media area, background tasks, calendar, status and profile
- App launcher for LibreWolf, Thunar, terminal, settings, and user commands
- Control centre with editable Wi-Fi/Bluetooth controls, tiles, volume balancing, screenshot/recording actions, clipboard and notifications
- Settings categories for connectivity, wallpaper/style, display, audio, notifications, security/polkit, location/weather, bar editor, apps, battery, accessibility, updates, accounts, and about
- Hyprland bindings and sensible preferred applications (Thunar, LibreWolf, foot)

## Install (Arch Linux only)

Aura is deliberately an Arch Linux configuration. Its single interactive installer uses `pacman`, lets you choose the preferred applications and font set, can enable NetworkManager/Bluetooth, and then installs the dotfiles:

```sh
./install.sh
```

Place wallpapers in `~/Pictures/Wallpapers` and run `matugen image /path/to/wallpaper`. This produces `~/.config/quickshell/aura/GeneratedColors.qml`; restart Quickshell to apply it.

## Key bindings

`SUPER` opens launcher, `SUPER+C` control centre, `SUPER+N` notifications, `SUPER+S` settings, `SUPER+V` clipboard, `SUPER+P` power menu, `SUPER+Return` terminal, `SUPER+E` Thunar, and `SUPER+B` LibreWolf. See the Hyprland config for workspace and screenshot bindings.

## IPC

All shell controls communicate through the `aura` Quickshell IPC target. Use `auractl controlCenter`, `auractl settings "Sound & vibration"`, `auractl wifi off`, or `auractl volume 40` from a terminal, key binding, or automation. This keeps external integrations independent of the QML layout.

## System integrations

`aura-system` is the service layer used by the shell. It controls Wi-Fi, VPN profiles and Ethernet through NetworkManager (`nmcli`), Bluetooth through BlueZ, reads battery details through UPower, and uses `swww` + Matugen for wallpaper and dynamic colour generation.

```sh
auractl wifi on
aura-system wifi-connect "Network name" "password"
aura-system vpn "My VPN"
auractl wallpaper ~/Pictures/Wallpapers/forest.png
auractl battery
auractl about
```

Clipboard history is captured at Hyprland startup by `wl-paste --watch` into cliphist. `SUPER+V` opens the live history; clicking an entry decodes and copies it. `SUPER+SHIFT+S` saves a selected screenshot to `~/Pictures/Screenshots`, copies it to the clipboard, and reports the saved location. `SUPER+SHIFT+V` starts/stops a selected-area recording in `~/Videos`.
