<div align="center">

<img src="./dotfiles/.config/quickshell/material3ui/assets/icons/Material3UI.svg" alt="Material3UI" width="128"/>

# Material3UI Shell

[![Last Commit](https://img.shields.io/github/last-commit/Abijspy/material3-android-shell?style=for-the-badge&logo=git&color=8bd5ca&logoColor=D9E0EE&labelColor=303446)](https://github.com/Abijspy/material3-android-shell/commits/main)
[![Release](https://img.shields.io/github/v/release/Abijspy/material3-android-shell?style=for-the-badge&logo=github&color=c6a0f6&logoColor=D9E0EE&labelColor=303446)](https://github.com/Abijspy/material3-android-shell/releases/latest)
[![Stars](https://img.shields.io/github/stars/Abijspy/material3-android-shell?style=for-the-badge&logo=github&color=f5bde6&logoColor=D9E0EE&labelColor=303446)](https://github.com/Abijspy/material3-android-shell/stargazers)
[![Repo Size](https://img.shields.io/github/repo-size/Abijspy/material3-android-shell?style=for-the-badge&logo=folder&color=8caaee&logoColor=D9E0EE&labelColor=303446)](https://github.com/Abijspy/material3-android-shell)
[![License](https://img.shields.io/github/license/Abijspy/material3-android-shell?style=for-the-badge&logo=gnu&color=a6da95&logoColor=D9E0EE&labelColor=303446)](LICENSE)

A Quickshell-based Material 3 desktop shell for Hyprland, adopting Android 16 design elements.

</div>

> **Note**: This shell and its dependencies are designed strictly for **Arch Linux based distributions** (Arch, CachyOS, EndeavourOS, etc.).
>
> **Built on NAnDoroid**: Material3UI started as a fork of [NAnDoroid-shell](https://github.com/na-ive/nandoroid-shell) by na-ive and has grown its own identity, installer, Hyprland setup, login screen and package tools since. See [NOTICE](NOTICE).

**Version:** v3.0.0
**License:** AGPL-3.0

## Key Features

- **Universal Dynamic Island:** Media playback, workspace switching, pomodoro timers and notification popups inside a single central notch.
- **Matching Lock & Login Screens:** A greetd login screen with the same clock, password pill and motion as the lock screen, kept in sync with your wallpaper, colours and avatar automatically.
- **Packages & Updates:** Arch, AUR and Flatpak updates in one place, with tabs to search and install packages and a status bar plugin that shows what is pending.
- **Auto-generated Colors:** The entire shell, GTK, KDE, Alacritty and Hyprland borders are themed from your wallpaper using Material 3 design tokens (via Matugen).
- **Deep Customizability:** Clocks, status bar modules, lock screen visuals, UI sizing and more, all from the built-in Settings app.
- **Built-in Tools:** Polkit agent, region screenshots, OCR, QR scanning and recording, Spotlight with clipboard, emoji and calculator, and a system monitor powered by the bundled `material3ui-sysmon`.
- **...and So Much More:** Online wallpaper collections, live wallpapers, lyrics, music recognition, dashboard widgets and countless micro-interactions.

## Screenshots

<div align="center">

Screenshots of v3 are on the way. Until then, try the shell live with `quickshell -c material3ui`, or preview the login screen with `material3ui-greeter preview`.

</div>

## Installation

> **Material3UI ships a shell _and_ a Hyprland setup.** It replaces your desktop panels, notifications, lock and login screens and system controls, and can manage your `hyprland.lua` (backed up first). It does not provide file pickers or screen sharing dialogs; install the XDG portals for those.

### Quick Install

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

The interactive installer runs a preflight check, lets you pick your apps (Alacritty, Nemo and LibreWolf by default) and how to handle Hyprland, reviews dependencies and every file it will write, then deploys with backups. Use `./install.sh --dry-run` to preview, `--yes` for unattended installs and `--greeter` for the login screen.

- **[Installation Guide (Clean Arch/CachyOS)](docs/INSTALL.md)**: Step-by-step from a fresh system, installer options and updating
- **[Migration Guide (from KDE/GNOME)](docs/MIGRATION.md)**: What gets replaced, portal setup, keybind migration
- **[Hyprland Configuration](docs/HYPRLAND.md)**: The modular Lua config and where your own settings go
- **[Packages & Updates](docs/PACKAGES.md)** · **[Login Screen](docs/GREETER.md)** · **[System Monitor Backend](docs/SYSMON.md)**

## Requirements & Dependencies

<details>
<summary>Click to view full dependency list</summary>

### Core Components

| Component | Package | Description |
| :--- | :--- | :--- |
| **Compositor** | `hyprland` | The tiling Wayland compositor hosting the shell. |
| **Framework** | `quickshell` | The engine (0.5.0+) used to build and run the shell. |
| **Monitor** | bundled | `material3ui-sysmon`: CPU, RAM, temperature, disks, GPU and processes from `/proc` and `/sys` (needs only `python3`). |
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
| **Updates** | `pacman-contrib` | Safe update checks (`checkupdates`) for Packages & Updates. |
| **Calculator** | `libqalculate` | Math calculator functionality in Spotlight (`qalc`). |
| **Dialogs** | `zenity` | File and directory selection dialogs. |
| **QR Scan** | `zbar` | QR code scanning functionality (`zbarimg`). |
| **OCR (Opt.)** | `tesseract` | OCR functionality in region tools. |
| **Biometric (Opt.)** | `fprintd` | Fingerprint sensor support on lockscreen. |
| **VPN (Opt.)** | `warp-cli` | Cloudflare WARP client integration. |
| **Flatpak (Opt.)** | `flatpak` | Flatpak updates, search and installs in Packages & Updates. |
| **Login (Opt.)** | `greetd` | Hosts the Material3UI login screen. |

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

The `dotfiles/` directory distributed with this repository contains:

- **`.config/quickshell/material3ui/`**: The shell itself.
- **`.config/hypr/`**: Modular Hyprland Lua config ([details](docs/HYPRLAND.md)).
- **`.config/matugen/`**: Material 3 templates for GTK, KDE, Alacritty, kitty, Hyprland borders, hyprlock and more.
- **`.config/alacritty/`**: Alacritty config that imports the generated theme.
- **`.config/starship.toml`**: Prompt configuration (requires starship).
- **`.local/bin/material3uictl`**: Command-line client for the shell's IPC.
- **`greeter/`**: The greetd login screen and its `material3ui-greeter` tool ([details](docs/GREETER.md)).
- **`extras/`**: hypridle, hyprlock, kitty and fish configs.

## IPC Commands

<details>
<summary>Click to view IPC commands & Keybinds</summary>

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

### Per-panel Targets

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

### Default Keybinds

| Keys | Action | Keys | Action |
| :--- | :--- | :--- | :--- |
| `SUPER` (tap) | Spotlight | `SUPER+Space` | App launcher |
| `SUPER+Return` / `SUPER+T` | Terminal (Alacritty) | `SUPER+E` | File manager (Nemo) |
| `SUPER+B` / `SUPER+W` | Browser (LibreWolf) | `SUPER+Q` | Close window |
| `SUPER+V` | Clipboard history | `SUPER+Period` | Emoji picker |
| `SUPER+A` | Notifications | `SUPER+N` | Quick settings |
| `SUPER+G` | Quick actions | `SUPER+D` | Dashboard |
| `SUPER+I` | Settings | `SUPER+Tab` | Overview |
| `SUPER+Escape` | Power menu | `SUPER+L` | Lock |
| `SUPER+SHIFT+S` / `Print` | Region screenshot | `SUPER+SHIFT+X` | OCR |
| `SUPER+SHIFT+R` | Record region | `CTRL+SUPER+T` | Wallpapers |
| `CTRL+ALT+Delete` | System monitor | `CTRL+SUPER+R` | Restart shell |
| `SUPER+F` / `S` / `P` / `J` | Fullscreen / float / pseudo / split | `SUPER+1…0` (`+SHIFT`) | Go to / move to workspace |

Defined in [`shell.lua`](dotfiles/.config/hypr/material3ui/shell.lua) and [`keybinds.lua`](dotfiles/.config/hypr/material3ui/keybinds.lua); override them in `user.lua`.

</details>

## Contributing

Pull requests are welcome! If you find bugs, want to fix issues or have improvements, feel free to contribute. For larger changes, please open an issue first so we can discuss the direction.

If you want to take the shell somewhere else entirely, you are encouraged to **fork** it and make it your own, as Material3UI itself did with NAnDoroid.

## Credits

Material3UI builds on the work of several open-source Quickshell projects. Their ideas, architecture and design references shaped this project.

### Upstream

- **[NAnDoroid-shell](https://github.com/na-ive/nandoroid-shell)** by **[na-ive](https://github.com/na-ive)** - The AGPL-3.0 codebase Material3UI is built on. Upstream history: [docs/UPSTREAM_CHANGELOG.md](docs/UPSTREAM_CHANGELOG.md).

### Core Framework

- **[Quickshell](https://github.com/outfoxxed)** - The QML-based framework powering Material3UI.

### Design References & Special Thanks

This project is a personal creation inspired by the following open-source developers and their projects:

- **[end-4](https://github.com/end-4)** - Core architecture, project structure, and shell logic inspired by [dots-hyprland](https://github.com/end-4/dots-hyprland).
- **[pC](https://github.com/pctrade)** - UI design, layout, and interaction references from [end4-pC](https://github.com/pctrade/end4-pC).
- **[P3DROVFX](https://github.com/P3DROVFX)** - Design and interaction references from [ii-p3drovfx](https://github.com/P3DROVFX/ii-p3drovfx).
- **[vaguesyntax (Vynx)](https://github.com/vaguesyntax)** - UI design and interaction references from [ii-vynx](https://github.com/vaguesyntax/ii-vynx).
- **[AvengeMedia](https://github.com/AvengeMedia)** - System Monitor design inspired by, and the installer flow and Lua config layout following, [DankMaterialShell](https://github.com/AvengeMedia/DankMaterialShell); `material3ui-sysmon` replaces its [dgop](https://github.com/AvengeMedia/dgop) monitor with a compatible-shaped JSON API.
- **[Axenide](https://github.com/Axenide)** - Spatial UI and Dynamic Island (notch) design references from [Ambxst](https://github.com/Axenide/Ambxst).

### Assets

- **Weather Icons:** Sourced from [mrdarrengriffin/google-weather-icons](https://github.com/mrdarrengriffin/google-weather-icons).
