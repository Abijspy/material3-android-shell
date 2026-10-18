# Changelog

## Unreleased

- **Native shell phase 7: Settings** (`msg settings [page]`):
  - **Network:** the Wi-Fi picker.
  - **Bluetooth:** power, discovery, and pair/connect/disconnect/forget.
  - **Sound:** volume, balance, channels and devices.
  - **Display:** brightness, night light temperature, monitors.
  - **Appearance:**
    - Wallpaper: thumbnail, picker and transition.
    - Colours: dark theme and Matugen scheme, recoloured live.
    - Bar: style, height and island options.
    - Clock format, fonts, and the desktop clock.
  - **Notifications:** Do Not Disturb and pop-up duration.
  - **Lock & idle:** lock and screen-off timers, lock before sleep.
  - **Keybinds:** a C++ port of the editor, sharing `keybinds.json` and `keybinds_custom.lua` with the QML one.
  - **Packages & Updates:** `checkupdates`, `yay`/`paru` and Flatpak updates, plus search and install for official, AUR (aurweb RPC) and Flatpak.
  - **About:** logo, device and compositor details, credits.
- **Native shell: verified end to end.**
  - **Polkit:** real polkitd authenticates a `wheel` user through the shell's agent: wrong password and retry, then authorized.
  - **Mouse:** pointer-driven tests use a `wlr-virtual-pointer` tool and a `msg debug` layout dump.
  - **JPEG and WebP:** wallpapers decode through gdk-pixbuf's sandboxed loaders. The dev container now relaxes seccomp and SELinux confinement so the sandbox can run.
- **Native shell: hover fix.** New `onHoverWithin` fixes hover over child buttons: the island collapsed as soon as the pointer reached its media buttons, and a toast's timeout restarted while hovering its action buttons.
- **Native C++ shell, phases 4–6** (`shell/`):
  - **Notifications:** a full notification daemon, with toasts and a notification centre.
  - **Quick Settings:** sliders, tiles, now playing, and a Wi-Fi picker with password entry.
  - **Spotlight:** apps, calculator, emoji, clipboard history, files and run, with keyboard navigation.
  - **Indicators and menus:** volume/brightness OSDs, a power menu, and DBusMenu tray menus.
  - **Session:** an ext-session-lock lock screen with PAM, a polkit agent, `material3ui greeter` for greetd, and idle handling (lock, screen off, Caffeine, lock before sleep).
  - **Wallpaper:** a per-output wallpaper layer with fade/wipe/grow transitions, a picker that runs Matugen, and a desktop clock.
  - **Panels:** the dynamic island (media, notifications, workspace switches), a dashboard with a calendar, the system monitor and a workspace overview.
- **Installer:** paru is no longer installed or used. The installer installs packages with yay and bootstraps `yay-bin` when yay is missing, building `yay` from source if the prebuilt binary doesn't run against the system's libalpm. `scripts/install_deps.sh` uses yay too. Packages & Updates still detects paru if you already have it.
- **Installer:** fix `libalpm.so.15: cannot open shared object file` when bootstrapping paru. An AUR helper is used only if it actually starts. When the prebuilt `paru-bin` doesn't run against the system's libalpm, the installer builds `paru` from source, and it runs a full `pacman -Syu` first. The package-updates check also skips a helper that fails to start.
- **License:** Material3UI Shell is now MIT-licensed (previously AGPL-3.0), with the permission of the NAnDoroid-shell authors. See `LICENSE` and `NOTICE`.
- **Native C++ shell, phases 2 and 3** (`shell/`):
  - Input:
    - Keyboard through xkbcommon, with key repeat and focus routing.
    - Fractional scaling (wp-fractional-scale + viewporter).
  - Popups:
    - Anchored under the bar, closed by Esc or a click outside, with Material enter and exit motion.
    - New controls: slider (with a centred balance mode), switch, scroll view and text field.
  - Services:
    - sd-bus on the event loop.
    - Audio through libpulse on PipeWire: volume, left/right balance, per-channel levels and device switching.
    - NetworkManager, BlueZ, UPower (with a sysfs fallback), brightness via logind, MPRIS, and a StatusNotifier tray that is both watcher and host, with theme, PNG, SVG and pixmap icons.
  - Bar:
    - New widgets: now playing, tray, network/Bluetooth/volume icons and UPower battery.
    - New popups: Sound (with balance and channels), Wi-Fi, Bluetooth and Run.
  - IPC: `status`, `popup`, `volume`, `brightness`, `media` and `tray`.
  - Testing: headless tests can start real D-Bus buses, PipeWire with null sinks, and mock system services.
- **Native C++ shell, phase 1** (`shell/`). A Wayland shell drawn directly with EGL/GLES2 and Pango, with no Qt, QML or Quickshell, following Noctalia's architecture with original code:
  - Core: a poll event loop, layer-shell surfaces with frame pacing, an SDF rounded-rect renderer, a scene graph with flex layout, and Material 3 motion.
  - Shared with the QML shell: it reads the same `config.json` and Matugen palette, and recolours live when either changes.
  - Services: a Hyprland socket service and Unix-socket IPC (`material3ui msg`).
  - UI so far: the status bar with animated workspaces, window title, clock and battery.
  - Testing: `shell/tools/dev.sh` builds in an Arch container and screenshots the shell under headless Sway, optionally against a fake Hyprland.
- **Keybind editor** in Settings → System → Keybinds. It lists the shell's Hyprland binds live, grouped and searchable. Click a bind to remap it (modifier chips plus press-a-key capture), disable it or reset it. You can also add custom command binds, and conflicts are flagged. Binds from `user.lua` are listed read-only.
  - The default binds in `shell.lua` and `keybinds.lua` now go through a new `bindings.lua` registry, which applies the edits from the shell-written `keybinds_custom.lua` before binding. Updates never overwrite that file.
- **Channels** in Settings → Audio for the default output (and for stereo microphones):
  - A left/right balance slider that snaps to centre.
  - A volume slider per channel, surround included.
  - A "Centre" reset.
  - Speaker test buttons that play the freedesktop "front left/right" voice on one side only.

## v3.0.0 — 2026-10-18

Material3UI gets its own identity, and a home for system packages.

- New original logo: a Material 3 Expressive eight-lobe "cookie" in an indigo-to-violet tonal gradient, carrying a rounded "M" built from two arches and crowned by a coral Dynamic Island pill. The full-colour and symbolic (tintable) SVGs are generated by `scripts/generate_logo.py` using only SVG Tiny features, so Qt renders them faithfully.
- The NAnDoroid artwork the shell inherited is no longer used as Material3UI's logo; it now appears only on the NAnDoroid card in Settings → About → Credits.
- **Packages & Updates** (Settings → About), with one tab each for Updates, Official, AUR and Flatpak:
  - Updates: official-repo updates via `checkupdates` (pacman-contrib), AUR via `paru`/`yay -Qua`, Flatpak via `flatpak remote-ls --updates`; Check now / Update now (in your terminal), auto-check every 30 min–6 h, AUR and Flatpak toggles, notifications.
  - Official / AUR / Flatpak: search with `pacman -Ss`, the aurweb RPC API and `flatpak search`; installed badges; one-click install in your terminal.
- **Package Updates status bar plugin** for both bar styles (base and M3), listed in the status bar editor and enabled by default for new configs; hidden when up to date unless you choose otherwise. Left click opens the page, right click checks, middle click updates.
- IPC: `packages`, `checkUpdates`, `systemUpdate`, `updateCount` on the `material3ui` target; `material3uictl packages` and `material3uictl updates check|upgrade|count`.
- `pacman-contrib` joins the dependencies; Flatpak is optional.
- Settings → About opens with a logo hero (full-colour logo with a short entrance animation, name and tagline) above the Shell and Distro cards; the Shell card, onboarding, launcher entry and notifications use the new mark.

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
