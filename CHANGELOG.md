# Changelog

## v2.0.0 — 2026-10-18

Material3UI Shell is rebuilt on [NAnDoroid-shell](https://github.com/na-ive/nandoroid-shell) and is now licensed under AGPL-3.0.

- Replace the v1 QML shell with the full NAnDoroid feature set: Dynamic Island, status bar, dock, overview, Spotlight, Quick Settings/Actions, dashboard, notification centre, lock screen, Polkit agent, region tools (screenshot, OCR, QR, search, recording), system monitor, wallpaper browser (local, Wallhaven, live), desktop widgets, lyrics, music recognition, onboarding, translations and OTA updates.
- Add the unified `material3ui` IPC target and rewrite `material3uictl` around it; per-panel targets stay available.
- Add `wallpaper set|next|toggleDarkMode` IPC functions.
- Ship modular Hyprland Lua configs (`hypr/material3ui/` plus `extras/hypr/`), Matugen templates for GTK, KDE, terminals and apps, and the Midnight Dunes wallpaper.
- Installer migrates v1 installs: backs up and replaces the shell profile and v1 `hyprland.lua`, and removes `material3ui-system`, `material3ui-polkit` and the update timer.
- Fix a duplicate `SUPER+V` binding that passed an unsupported argument to `spotlight toggle`.
- Upstream history is kept in `docs/UPSTREAM_CHANGELOG.md`.

## v1.0.1 — 2026-10-02

- Rewrite the Arch installer with safe upgrades, backups, unattended flags, and optional AUR setup.
- Migrate the Hyprland profile from deprecated Hyprlang to the current Lua configuration API.
- Preserve Material3UI startup, wallpaper restoration, blur, workspaces, and media controls in the Lua profile.

## v1.0.0 — 2026-10-02

- Ship the original Midnight Dunes desktop wallpaper as the first-run default.
- Persist and restore the selected wallpaper at login through swww.
- Generate the Material 3 palette from the restored wallpaper.
- Promote Material3UI Shell to its first stable major release.

## v0.0.2 — 2026-09-22

- Rename the shell to Material3UI Shell, including the Quickshell profile, IPC target, commands, installer, and documentation.

## v0.0.1 — 2026-09-22

Initial Material3UI Shell release for Arch Linux and Hyprland.

- Material 3 Quickshell interface with Matugen palette generation and compositor blur.
- Hyprland profile, IPC controls, launcher, control centre, clipboard, notifications, and power menu.
- Service-backed network, Bluetooth, wallpaper, battery, screenshot, recording, and settings actions.
- Interactive Arch-only installer with Thunar and LibreWolf as preferred applications.
