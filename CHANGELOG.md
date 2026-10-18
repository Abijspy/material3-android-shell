# Changelog

## v2.3.0 — 2026-10-18

- Add the Material3UI **greetd login screen** (`greeter/`). It mirrors the lock screen's layout, sizes, colours, placeholder text, Material-shape password dots, shake and entrance motion, and renders your lock-screen clock style; it adds a user picker, a session picker (from `wayland-sessions`) and power buttons. It handles multi-step PAM prompts (fingerprint/OTP) and remembers the last user and session; with no greetd running it opens in a preview mode.
- Add `material3ui-greeter` (`install [--enable]`, `sync`, `preview`, `status`, `uninstall`). Install backs up `/etc/greetd/config.toml`, makes Google Sans Flex available system-wide, and grants your user write access to `/var/lib/material3ui-greeter` through an ACL so syncing needs no sudo.
- Add **Auto sync login screen** to Settings → Lockscreen, with status, "Sync now" and "Preview". A new `GreeterSync` service watches the generated colours and the shell config and syncs after a short debounce.
- `install.sh` gains an optional login-screen step (`--greeter`) and disables `hyprpolkitagent`, polkit-gnome and the Plasma agent user services, since the shell is the polkit agent and only one agent can register per session. `update.sh` refreshes an installed greeter when sudo is available without a prompt.

## v2.2.0 — 2026-10-18

- Add `material3ui-sysmon`, Material3UI's own system monitor backend replacing dgop. Pure Python 3 standard library, reading `/proc` and `/sys`: CPU (usage, per-core, frequency, temperature), memory and swap, system counts and load, processes with true per-sample CPU deltas, disk mounts and GPUs (named from `pci.ids`).
- `meta` (one-shot JSON), `top` (table) and `server` (local HTTP on `127.0.0.1:63490`, `/meta` and `/health`) commands.
- The System Monitor's Processes tab and the About page's hardware summary now use it; it ships with the shell, so no extra package is needed. The `ps` fallback remains for systems without `python3`.
- The installer and `update.sh` link it to `~/.local/bin/material3ui-sysmon`.

## v2.1.0 — 2026-10-18

- Rewrite the installer around a reviewed, seven-step flow modelled on DankMaterialShell's: preflight → choices → dependency review → packages → configuration review → deploy → summary. It logs every run, backs up everything it replaces, supports `--yes`, `--dry-run`, `--terminal`, `--file-manager`, `--browser`, `--hypr full|inject|skip`, `--channel`, `--skip-deps` and `--dir`, and hands over to the cloned script when run through `curl`.
- Default apps are now **Alacritty**, **Nemo** and **LibreWolf**. The choice is written to `hypr/material3ui/apps.lua`, exported as `$TERMINAL` / `$BROWSER`, and registered as the XDG defaults; Nemo's terminal follows the chosen terminal.
- Rewrite the Hyprland Lua config as small modules (`env`, `apps`, `monitors`, `appearance`, `colors`, `input`, `rules`, `autostart`, `keybinds`, `shell`, `user`), loaded with dotted `require`s; `user.lua` is yours and never overwritten.
- Hyprland border colours come from the wallpaper through a new Matugen `colors.lua` template; add an Alacritty theme template and config.
- Add media, mic-mute, `Print`, `SUPER+Escape` (power menu) and `SUPER+B` bindings; panel bindings use the unified `material3ui` IPC target.
- The shell launches `$TERMINAL` (falling back to Alacritty) instead of a hardcoded kitty, and pins Alacritty, Nemo and LibreWolf to the dock by default.
- `update.sh` keeps `apps.lua`, `user.lua`, `user_persistence.lua` and `colors.lua`, honours the recorded Hyprland mode, and only updates Alacritty/Matugen/starship configs that carry the Material3UI marker.
- Add `swappy` to the root dependency list so it matches the shell's copy.

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
