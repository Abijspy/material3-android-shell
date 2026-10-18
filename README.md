<div align="center">

<img src="./dotfiles/.config/quickshell/material3ui/assets/icons/Material3UI.svg" alt="Material3UI" width="128"/>

# Material3UI Shell

A Quickshell-based Material 3 desktop shell for Hyprland, adopting Android 16 design elements.

</div>

> **Note**: This shell and its dependencies are designed for **Arch Linux based distributions** (Arch, CachyOS, EndeavourOS, etc.).

**Version:** v2.0.0
**License:** AGPL-3.0 — Material3UI Shell v2 is a rebranded fork of [NAnDoroid-shell](https://github.com/na-ive/nandoroid-shell) by na-ive. See [NOTICE](NOTICE).

## Key Features

- **Universal Dynamic Island:** Media playback, workspace switching, pomodoro timers, and notification popups inside a single central notch.
- **Status bar, dock & overview:** Configurable status bar with tray overflow, taskbar dock, and a workspace overview.
- **Spotlight & launcher:** App search, file search (`fd`), calculator (`qalc`), clipboard history (`cliphist`), emoji picker, quick commands, currency conversion and web search.
- **Quick Settings & Quick Actions:** Editable tiles for Wi-Fi, Bluetooth, VPN/WARP, night light, caffeine, game mode, power profiles, EasyEffects and more.
- **Dashboard:** Calendar, weather, media with lyrics, system info, timers, stopwatch, alarms and reminders.
- **Notification centre** with persistent history and popups.
- **Lock screen** with custom clocks, separate lock wallpaper and optional fingerprint (`fprintd`), plus a built-in **Polkit** agent.
- **Region tools:** Screenshot, OCR (`tesseract`), visual search, QR scanning and screen recording with or without audio.
- **System monitor** (`dgop`) with processes, performance and system pages.
- **Wallpapers:** Local, Wallhaven and online collections, live wallpapers (mpvpaper / Wallpaper Engine), auto-cycling and desktop widgets.
- **Auto-generated colours:** The whole shell, GTK, KDE, terminals and apps are themed from the wallpaper with Matugen Material 3 tokens.
- **Settings app** with onboarding, presets, translations, display, audio, network and Bluetooth dialogs, music recognition (`songrec`), floating lyrics and in-shell OTA updates.

## Installation

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

The interactive installer guides you through dependency installation, config copying, the `material3uictl` CLI, and optional injection into your existing Hyprland/Kitty/Fish setup. Pass `--dry-run` to preview every change.

**Upgrading from Material3UI v1:** the installer backs up and replaces `~/.config/quickshell/material3ui`, swaps the v1 `hyprland.lua` for the modular v2 config (when injection is enabled), and removes the retired `material3ui-system`, `material3ui-polkit` and update-timer helpers. Backups go to `~/.config/material3ui/backups/`.

- **[Installation Guide (Clean Arch/CachyOS)](docs/INSTALL.md)**
- **[Migration Guide (from KDE/GNOME)](docs/MIGRATION.md)**

Update later from Settings → About, or run `~/.local/src/material3ui/update.sh all`.

## Requirements & Dependencies

<details>
<summary>Click to view full dependency list</summary>

### Core Components

| Component | Package | Description |
| :--- | :--- | :--- |
| **Compositor** | `hyprland` | The tiling Wayland compositor hosting the shell. |
| **Framework** | `quickshell` | The engine (0.5.0+) used to build and run the shell. |
| **Monitor** | `dgop` | System monitoring (CPU, RAM, Temp) stats. |
| **Theme** | `matugen` | Material 3 theme generation from wallpapers. |
| **GTK Theme** | `adw-gtk-theme` | Libadwaita-like theme for GTK3 applications. |
| **Scripting** | `python3` | Used by terminal color scripts and utilities. |
| **JSON** | `jq` | Command-line JSON processor for configs and state. |

### System Services & Protocols

| Service | Command | Description |
| :--- | :--- | :--- |
| **Audio** | `pipewire` | Audio management via `wpctl`. |
| **Network** | `networkmanager` | Wi-Fi and Ethernet controls via `nmcli`. |
| **Bluetooth** | `bluez` | Bluetooth management via `bluetoothctl`. |
| **Notify** | `libnotify` | System notifications via `notify-send`. |
| **Auth** | `polkit` | Privileged actions via `pkexec`. |
| **Session** | `systemd` | Power management and session locking. |
| **Portal (H)** | `xdg-desktop-portal-hyprland` | Screen sharing and desktop integration. |
| **Portal (G)** | `xdg-desktop-portal-gtk` | File picker and standard desktop integration. |

### CLI Utilities (Functional)

| Utility | Command | Description |
| :--- | :--- | :--- |
| **Backlight** | `brightnessctl` | Internal screen brightness control. |
| **External Br.** | `ddcutil` | External monitor brightness control. |
| **Media** | `playerctl` | MPRIS media playback controls. |
| **Screenshot** | `grim` | Wayland screenshot utility. |
| **Region** | `slurp` | Region selection for screenshots/recording. |
| **Recorder** | `wf-recorder` | Screen recording functionality. |
| **Image** | `imagemagick` | Color detection and image processing (`magick`). |
| **Sound** | `ffmpeg` | System sounds playback via `ffplay`. |
| **Clipboard** | `wl-clipboard` | Wayland clipboard operations. |
| **Clip. Hist.** | `cliphist` | Clipboard history management in Spotlight. |
| **Recognition** | `songrec` | Shazam-like music recognition feature. |
| **Visualizer** | `cava` | Audio visualization in the shell. |
| **Wallpaper Eng.** | `linux-wallpaperengine-git` | Steam Wallpaper Engine support (Optional). |
| **Effects** | `easyeffects` | Audio effects and equalization management. |
| **Picker** | `hyprpicker` | System-wide color picker tool. |
| **Lock** | `hyprlock` | The lock screen provider. |
| **Night Light** | `hyprsunset` | Blue light filter functionality. |
| **Search** | `fd` | Fast file search functionality in Spotlight. |
| **Calculator** | `libqalculate` | Math calculator functionality in Spotlight (`qalc`). |
| **Dialogs** | `zenity` | File and directory selection dialogs. |
| **QR Scan** | `zbar` | QR code scanning functionality (`zbarimg`). |
| **OCR (Opt.)** | `tesseract` | OCR functionality in region tools. |
| **Biometric (Opt.)** | `fprintd` | Fingerprint sensor support on lockscreen. |
| **VPN (Opt.)** | `warp-cli` | Cloudflare WARP client integration. |

### Fonts

| Font | Package | Source / Purpose |
| :--- | :--- | :--- |
| **UI Font** | `Google Sans Flex` | Primary variable font (from GitHub). |
| **Icons** | `ttf-material-symbols-variable-git` | Material Symbols icon font. |
| **Monospace** | `ttf-jetbrains-mono-nerd` | Default monospace and nerd font. |

### Shell & Terminal (Optional)

| Tool | Package | Purpose |
| :--- | :--- | :--- |
| **Terminal** | `kitty` | Terminal emulator with theme injection support. |
| **Shell** | `fish` | Interactive shell. |
| **Prompt** | `starship` | Cross-shell prompt. |
| **Utils** | `bash`, `awk`, `grep` | Standard Unix utilities used by core scripts. |

</details>

## Configuration

- **`dotfiles/.config/quickshell/material3ui/`**: The shell itself.
- **`dotfiles/.config/hypr/material3ui/`**: Hyprland keybinds and layer rules, required from `hyprland.lua`.
- **`dotfiles/.config/matugen/`**: Template configs for Material 3 theme generation.
- **`dotfiles/.config/starship.toml`**: Prompt configuration (requires starship).
- **`dotfiles/.local/bin/material3uictl`**: Terminal client for the shell's IPC.
- **`extras/`**: Clean base configurations for Hyprland, Hyprlock, Kitty and Fish.

## IPC

Everything is reachable through the unified **`material3ui`** IPC target:

```bash
quickshell -c material3ui ipc call material3ui <function> [args]
# or simply
material3uictl <function> [args]
```

| Function | Action |
| :--- | :--- |
| `launcher`, `spotlight`, `clipboard`, `emoji` | Toggle the app launcher / Spotlight (optionally in clipboard or emoji mode) |
| `controlCenter`, `quickActions`, `notifications` | Toggle Quick Settings, Quick Actions, Notification Centre |
| `dashboard`, `overview`, `power` | Toggle Dashboard, workspace Overview, session (power) menu |
| `settings`, `systemMonitor` | Open Settings / System Monitor |
| `wallpaperPicker`, `wallpaper <path>`, `nextWallpaper` | Open the wallpaper picker, apply an image, or cycle |
| `darkMode` | Toggle light/dark colour scheme |
| `lock` | Lock the session |
| `close` | Close every open panel |

<details>
<summary>Per-panel IPC targets, global shortcuts & panel keys</summary>

Fine-grained control (e.g. `open` / `close` rather than toggle) remains available through the per-panel targets:

```bash
quickshell -c material3ui ipc call <target> <method>
```

### Sidebar & Panels

Manage the visibility of all UI panels.

| Feature                 | Target          | Method   | Terminal Command                                |
| :---------------------- | :-------------- | :------- | :---------------------------------------------- |
| **App Launcher**        | `launcher`      | `toggle` | `qs -c material3ui ipc call launcher toggle`      |
| **Spotlight Search**    | `spotlight`     | `toggle` | `qs -c material3ui ipc call spotlight toggle`     |
| **Notification Center** | `notifications` | `toggle` | `qs -c material3ui ipc call notifications toggle` |
| **Quick Settings**      | `quicksettings` | `toggle` | `qs -c material3ui ipc call quicksettings toggle` |
| **System Monitor**      | `systemmonitor` | `toggle` | `qs -c material3ui ipc call systemmonitor toggle` |
| **Sys. Monitor (Direct)**| `systemmonitor` | `open_direct`| `qs -c material3ui ipc call systemmonitor open_direct`|
| **Overview Panel**      | `overview`      | `toggle` | `qs -c material3ui ipc call overview toggle`      |
| **Session (Power)**     | `session`       | `toggle` | `qs -c material3ui ipc call session toggle`       |
| **Dashboard**           | `dashboard`     | `toggle` | `qs -c material3ui ipc call dashboard toggle`     |
| **Quick Actions**       | `quickactions`  | `toggle` | `qs -c material3ui ipc call quickactions toggle`  |
| **Material3UI Settings**  | `settings`      | `toggle` | `qs -c material3ui ipc call settings toggle`      |
| **Settings (Direct)**   | `settings`      | `open_direct`| `qs -c material3ui ipc call settings open_direct` |

### Region Tools (Screenshots & Recording)

Trigger selection-based actions.

| Action                | Target   | Method            | Terminal Command                                  |
| :-------------------- | :------- | :---------------- | :------------------------------------------------ |
| **Region Screenshot** | `region` | `screenshot`      | `qs -c material3ui ipc call region screenshot`      |
| **Visual Search**     | `region` | `search`          | `qs -c material3ui ipc call region search`          |
| **Text OCR**          | `region` | `ocr`             | `qs -c material3ui ipc call region ocr`             |
| **QR Code Scan**      | `region` | `qrcode`          | `qs -c material3ui ipc call region qrcode`          |
| **Record Region**     | `region` | `record`          | `qs -c material3ui ipc call region record`          |
| **Record w/ Audio**   | `region` | `recordWithSound` | `qs -c material3ui ipc call region recordWithSound` |

### Media & System

Control specific system services.

| Feature              | Target       | Method        | Terminal Command                                 |
| :------------------- | :----------- | :------------ | :----------------------------------------------- |
| **Brightness +**     | `brightness` | `increment`   | `qs -c material3ui ipc call brightness increment`  |
| **Brightness -**     | `brightness` | `decrement`   | `qs -c material3ui ipc call brightness decrement`  |
| **Pomodoro Start**   | `pomodoro`   | `start`       | `qs -c material3ui ipc call pomodoro start`        |
| **Pomodoro Pause**   | `pomodoro`   | `pause`       | `qs -c material3ui ipc call pomodoro pause`        |
| **Pomodoro Stop**    | `pomodoro`   | `stop`        | `qs -c material3ui ipc call pomodoro stop`         |
| **Pomodoro Reset**   | `pomodoro`   | `reset`       | `qs -c material3ui ipc call pomodoro reset`        |
| **Wallpaper (Home)** | `wallpaper`  | `openDesktop` | `qs -c material3ui ipc call wallpaper openDesktop` |
| **Wallpaper (Lock)** | `wallpaper`  | `openLock`    | `qs -c material3ui ipc call wallpaper openLock`    |
| **Wallpaper (Toggle)**| `wallpaper`  | `toggle`      | `qs -c material3ui ipc call wallpaper toggle`      |
| **Change Avatar**    | `spotlight`  | `browse_avatar`| `qs -c material3ui ipc call spotlight browse_avatar`|

### Global Shortcuts (Native Quickshell)

Material3UI uses native Quickshell Global Shortcuts for specialized tool operations. These are triggered using the `global` dispatcher in Hyprland with the format `quickshell:<name>`.

| Shortcut Name           | Description                      | Hyprland Lua Bind Example                                                                  |
| :---------------------- | :------------------------------- | :----------------------------------------------------------------------------------------- |
| `spotlightFiles`        | Open Spotlight in File search    | `hl.bind("SUPER + F", hl.dsp.global("quickshell:spotlightFiles"))`                         |
| `spotlightCommand`      | Open Spotlight in Quick Commands | `hl.bind("SUPER + G", hl.dsp.global("quickshell:spotlightCommand"))`                       |
| `spotlightTools`        | Open Spotlight in Tools mode     | `hl.bind("SUPER + T", hl.dsp.global("quickshell:spotlightTools"))`                         |
| `spotlightClipboard`    | Open Spotlight in Clipboard mode | `hl.bind("SUPER + V", hl.dsp.global("quickshell:spotlightClipboard"))`                     |
| `spotlightEmoji`        | Open Spotlight in Emoji mode     | `hl.bind("SUPER + E", hl.dsp.global("quickshell:spotlightEmoji"))`                         |
| `quickActions`          | Toggle Quick Actions HUD         | `hl.bind("SUPER + X", hl.dsp.global("quickshell:quickActions"))`                           |
| `regionScreenshot`      | Capture selected region          | `hl.bind("SUPER + S", hl.dsp.global("quickshell:regionScreenshot"))`                       |
| `regionOcr`             | Extract text from region         | `hl.bind("SUPER + SHIFT + S", hl.dsp.global("quickshell:regionOcr"))`                      |
| `regionSearch`          | Visual search from region        | `hl.bind("SUPER + Z", hl.dsp.global("quickshell:regionSearch"))`                           |
| `regionQRCode`          | Scan QR code from region         | `hl.bind("SUPER + SHIFT + Z", hl.dsp.global("quickshell:regionQRCode"))`                   |
| `regionRecord`          | Record selected region           | `hl.bind("SUPER + R", hl.dsp.global("quickshell:regionRecord"))`                           |
| `regionRecordWithSound` | Record region with audio         | `hl.bind("SUPER + SHIFT + R", hl.dsp.global("quickshell:regionRecordWithSound"))`          |

### Panel Shortcuts

When any major Material3UI panel is open and focused, you can use built-in keyboard shortcuts to navigate efficiently:

| Key | Action |
| :--- | :--- |
| `/` | Open the Keyboard Shortcuts Cheatsheet |
| `Esc` | Close the current panel |

</details>

## Default Keybinds

`SUPER` (tap) Spotlight · `SUPER+Space` launcher · `SUPER+V` clipboard · `SUPER+Period` emoji · `SUPER+A` notifications · `SUPER+N` quick settings · `SUPER+G` quick actions · `SUPER+D` dashboard · `SUPER+I` settings · `SUPER+Tab` overview · `SUPER+L` lock · `CTRL+ALT+Delete` system monitor · `CTRL+SUPER+T` wallpapers · `SUPER+SHIFT+S` screenshot · `SUPER+SHIFT+R` record · `SUPER+SHIFT+X` OCR · `CTRL+SUPER+R` restart shell. See [`dotfiles/.config/hypr/material3ui/material3ui.lua`](dotfiles/.config/hypr/material3ui/material3ui.lua).

## Credits

- **[NAnDoroid-shell](https://github.com/na-ive/nandoroid-shell)** by **[na-ive](https://github.com/na-ive)** — the AGPL-3.0 codebase Material3UI Shell v2 is built on. Upstream history: [docs/UPSTREAM_CHANGELOG.md](docs/UPSTREAM_CHANGELOG.md).

### Core Framework

- **[Quickshell](https://github.com/outfoxxed)** - The QML-based framework powering Material3UI.

### Design References & Special Thanks

This project is a personal creation inspired by the following open-source developers and their projects:

- **[end-4](https://github.com/end-4)** - Core architecture, project structure, and shell logic inspired by [dots-hyprland](https://github.com/end-4/dots-hyprland).
- **[pC](https://github.com/pctrade)** - UI design, layout, and interaction references from [end4-pC](https://github.com/pctrade/end4-pC).
- **[P3DROVFX](https://github.com/P3DROVFX)** - Design and interaction references from [ii-p3drovfx](https://github.com/P3DROVFX/ii-p3drovfx).
- **[vaguesyntax (Vynx)](https://github.com/vaguesyntax)** - UI design and interaction references from [ii-vynx](https://github.com/vaguesyntax/ii-vynx).
- **[AvengeMedia](https://github.com/AvengeMedia)** - System monitoring implementation inspired by [DankMaterialShell](https://github.com/AvengeMedia/DankMaterialShell) and powered by [dgop](https://github.com/AvengeMedia/dgop).
- **[Axenide](https://github.com/Axenide)** - Spatial UI and Dynamic Island (notch) design references from [Ambxst](https://github.com/Axenide/Ambxst).

### Assets

- **Weather Icons:** Sourced from [mrdarrengriffin/google-weather-icons](https://github.com/mrdarrengriffin/google-weather-icons).

## License

Material3UI Shell is licensed under the [GNU Affero General Public License v3.0](LICENSE), the same licence as NAnDoroid-shell.
