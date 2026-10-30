# Dependencies

The installer reads `data/dependencies.json`, checks what is missing with `pacman -T`, and installs the rest with yay. This is the same list in readable form.

### Compositor

The installer asks which compositor the shell runs on (`--compositor hyprland`, `niri` or `both`) and installs only that one's packages:

| Compositor | Packages | Description |
| :--- | :--- | :--- |
| **Hyprland** | `hyprland` `xdg-desktop-portal-hyprland` `hypridle` `hyprlock` `hyprsunset` `hyprpicker` | Hyprland 0.55 or newer; screen sharing with the shell's picker, the idle daemon and fallback lock screen its config starts, night light and a colour picker. |
| **niri** | `niri` `xwayland-satellite` `xdg-desktop-portal-gnome` | niri 26.04 or newer, X11 apps, and screen sharing. Night light needs nothing more: the shell warms the screens itself. |

### Core Components

| Component | Package | Description |
| :--- | :--- | :--- |
| **Shell** | built from `shell/` | The native C++ shell (`material3ui`), built with `meson` and `ninja` (`base-devel`) by the installer. |
| **Shell libraries** | `wayland` `wayland-protocols` `wlr-protocols` `mesa` `pango` `cairo` `librsvg` `gdk-pixbuf2` `libxkbcommon` `nlohmann-json` `libpulse` `polkit` `pam` | Wayland and layer shell, OpenGL ES, text, icons and wallpapers, keyboard, JSON, audio, the polkit agent and the lock screen. |
| **Theme** | `matugen` | Material 3 theme generation from wallpapers. |
| **GTK Theme** | `adw-gtk-theme` | Libadwaita-like theme for GTK3 applications. |
| **Cursor** | `bibata-cursor-theme-bin` | Bibata Modern Classic, the default cursor (AUR). Another can be picked in Hyprland Settings → Cursor, or Settings → Mouse & touchpad on niri. |
| **Scripting** | `python3` | Used by terminal color scripts and utilities. |
| **JSON** | `jq` | Command-line JSON processor for configs and state. |

### System Services & Protocols

| Service | Command | Description |
| :--- | :--- | :--- |
| **Audio** | `pipewire` `pipewire-pulse` | Volume, devices and balance through the PulseAudio API. |
| **Network** | `networkmanager` | Wi-Fi and Ethernet over D-Bus. |
| **Bluetooth** | `bluez` | Bluetooth over D-Bus. |
| **Notify** | `libnotify` | System notifications via `notify-send`. |
| **Auth** | `polkit` | The shell is the polkit agent. |
| **Session** | `systemd` | Power management and session locking. |
| **Portal (G)** | `xdg-desktop-portal-gtk` | File picker and standard desktop integration (screen sharing comes with the compositor, above). |

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
| **System info** | `fastfetch` | The device's details in Settings → About: model, CPU, GPU, memory, disk, displays, packages, uptime and battery. |
| **Wallpaper Eng.** | `linux-wallpaperengine-git` | Steam Wallpaper Engine support (Optional). |
| **Search** | `fd` | Fast file search functionality in Spotlight. |
| **Updates** | `pacman-contrib` | Safe update checks (`checkupdates`) for Software & updates. |
| **Calculator** | `libqalculate` | Math calculator functionality in Spotlight (`qalc`). |
| **Dialogs** | `zenity` | File and directory selection dialogs. |
| **QR Scan** | `zbar` | QR code scanning functionality (`zbarimg`). |
| **OCR (Opt.)** | `tesseract` | OCR functionality in region tools. |
| **Biometric (Opt.)** | `fprintd` | Fingerprint sensor support on lockscreen. |
| **Flatpak (Opt.)** | `flatpak` | Flatpak updates, search and installs in Software & updates. |
| **Firmware (Opt.)** | `fwupd` | Firmware updates from the LVFS in Settings → Firmware (offered there when missing). |
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
