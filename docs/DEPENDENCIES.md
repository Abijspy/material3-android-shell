# Dependencies

The installer reads `data/dependencies.json`, checks what's missing with `pacman -T`, and installs the rest with yay. This page is the same list in readable form, plus what each of Material3UI's own pacman packages depends on.

### Compositor

The shell runs on niri, which the installer installs first:

| Compositor | Packages | Description |
| :--- | :--- | :--- |
| **niri** | `niri` `xwayland-satellite` `xdg-desktop-portal-gnome` | niri 26.04 or newer, X11 apps, and screen sharing. Night light needs nothing more: the shell warms the screens itself. |

### Material3UI's packages

The shell and each of its apps are separate pacman packages, built from one compile. The apps share `libmaterial3ui` (in `/usr/lib/material3ui`) with the shell, so each app's version always matches the shell's. Remove any app with `sudo pacman -R material3ui-<app>`; updates leave it out from then on.

| Package | What it is | Needs (besides the shell) | Optional |
| :--- | :--- | :--- | :--- |
| `material3ui` | The shell and `libmaterial3ui` | `niri` `wayland` `libglvnd` `pango` `cairo` `libxkbcommon` `systemd-libs` `libpulse` `librsvg` `gdk-pixbuf2` `polkit` `pam` `fastfetch` | the apps below, and the tools further down |
| `material3ui-files` | Files, the file manager | `udisks2` (drives), `gvfs` (FTP, SFTP, WebDAV), `libarchive` (ZIP and unzip) | `ffmpeg` (video thumbnails), `imagemagick` (PSD, XCF, RAW previews), `wl-clipboard` |
| `material3ui-viewer` | Photos, the image viewer | `gdk-pixbuf2` | `imagemagick`, `libavif`, `libheif`, `libjxl`, `webp-pixbuf-loader` |
| `material3ui-player` | Videos, the video player | `mpv` (libmpv) | `yt-dlp` (web videos) |
| `material3ui-tasks` | The Task Manager | `systemd` | |
| `material3ui-terminal` | The terminal | `libvterm` | `wl-clipboard` |
| `material3ui-notes` | Notes | | |
| `material3ui-pdf` | The PDF viewer and editor | `poppler-glib` | `qpdf` (rotating, moving, deleting and extracting pages), `cups` (printing) |
| `material3ui-text` | The text editor | | `wl-clipboard` |
| `material3ui-store` | Store | pacman, curl | flatpak |
| `material3ui-podcasts` | Podcasts | mpv, curl | mpv-mpris |
| `material3ui-contacts` | Contacts |  | kdeconnect |
| `material3ui-mail` | Mail | curl | gnome-keyring (or another Secret Service) |
| `material3ui-imagewriter` | Image Writer | util-linux, polkit, udisks2 | xz, zstd |
| `material3ui-fonts` | Fonts | fontconfig |  |
| `material3ui-calculator` | Calculator |  |  |
| `material3ui-recorder` | Sound Recorder | `ffmpeg` |  |
| `material3ui-backups` | Backups |  |  |
| `material3ui-camera` | Camera | `mpv`, `ffmpeg` |  |
| `material3ui-disks` | Disks | `udisks2` | `smartmontools`, `exfatprogs`, `dosfstools`, `ntfs-3g` |
| `material3ui-music` | Music | `mpv` (libmpv) | `mpv-mpris` (the bar, lock screen and lyrics follow it), `ffmpeg` (tags and album art) |
| `material3ui-greeter` | The login screen | `greetd` `acl` `niri` | |

### Building

What the build needs (the installer and updates install these before compiling; they're the packages' `makedepends`):

| Component | Packages | Description |
| :--- | :--- | :--- |
| **Toolchain** | `base-devel` `meson` `ninja` | Compiles the shell and its apps (`shell/`). |
| **Shell libraries** | `wayland` `wayland-protocols` `wlr-protocols` `mesa` `pango` `cairo` `librsvg` `gdk-pixbuf2` `libxkbcommon` `nlohmann-json` `libpulse` `polkit` `pam` | Wayland and layer shell, OpenGL ES, text, icons and pictures, keyboard, JSON, audio, the polkit agent and the lock screen. |
| **App libraries** | `mpv` `libvterm` `poppler-glib` | Videos (libmpv), the terminal (libvterm), and the PDF viewer and editor (poppler-glib, which is optional: without it the build leaves the PDF app out). |

### Core Components

| Component | Package | Description |
| :--- | :--- | :--- |
| **Theme** | `matugen` | Material 3 colours from the wallpaper. |
| **GTK Theme** | `adw-gtk-theme` | Libadwaita-like theme for GTK3 applications. |
| **Cursor** | `bibata-cursor-theme-bin` | Bibata Modern Classic, the default cursor (AUR). Another can be picked in Settings → Accessibility. |
| **Scripting** | `python3` | Used by terminal colour scripts and utilities. |
| **JSON** | `jq` | Command-line JSON processor for configs and state. |

### System Services & Protocols

| Service | Command | Description |
| :--- | :--- | :--- |
| **Audio** | `pipewire` `pipewire-pulse` | Volume, devices and balance through the PulseAudio API. |
| **Network** | `networkmanager` | Wi-Fi and Ethernet over D-Bus. |
| **Bluetooth** | `bluez` | Bluetooth over D-Bus. |
| **Notify** | `libnotify` | System notifications via `notify-send`. |
| **Auth** | `polkit` | The shell is the polkit agent. |
| **Session** | `systemd` | Power management, session locking, and the Task Manager's services. |
| **Backlight** | `brightnessctl` | Internal screen brightness control. |
| **Media** | `playerctl` | MPRIS media playback controls. |
| **Portal (G)** | `xdg-desktop-portal-gtk` | File picker and standard desktop integration (screen sharing comes with the compositor, above). |

### CLI Utilities (Functional)

| Utility | Command | Description |
| :--- | :--- | :--- |
| **External Br.** | `ddcutil` | External monitor brightness control. |
| **Screenshot** | `grim` | Wayland screenshot utility. |
| **Region** | `slurp` | Region selection for screenshots and recording. |
| **Annotate** | `swappy` | Screenshot annotation (the shell's own image editor does it too). |
| **Recorder** | `wf-recorder` | Screen recording. |
| **Image** | `imagemagick` | Colour detection and image conversion (`magick`); opens PSD, XCF, camera RAW and JPEG 2000 in Photos and the image editor. |
| **Media** | `ffmpeg` | System sounds (`ffplay`), GIF recordings and video thumbnails in Files. |
| **Web video** | `yt-dlp` | Web videos in Videos (`material3ui msg play <URL>`). |
| **PDF pages** | `qpdf` | Rotating, moving, deleting and extracting pages in the PDF editor. |
| **Drives** | `udisks2` | Opening and ejecting USB drives and cards in Files (`udisksctl`). |
| **Servers** | `gvfs` | FTP, SFTP and WebDAV servers in Files (`gio mount`). |
| **Archives** | `libarchive` | ZIP and unzip in Files (`bsdtar`: ZIP, tar, 7z, rar, ISO and more). |
| **Clipboard** | `wl-clipboard` | Wayland clipboard: copy and paste in Files, the terminal and the editors. |
| **Clip. Hist.** | `cliphist` | Clipboard history in Spotlight. |
| **Recognition** | `songrec` | Recognises the song playing (Shazam), so its lyrics can be found when the player doesn't say what it is. The sound is captured with `parec` (libpulse). |
| **Visualizer** | `cava` | Audio visualisation in the shell. |
| **Translate** | `translate-shell` | Translation in Spotlight (`trans`). |
| **System info** | `fastfetch` | The device's details in Settings → About: model, CPU, GPU, memory, disk, displays, packages, uptime and battery. |
| **Search** | `fd` | Fast file search in Spotlight. |
| **Updates** | `pacman-contrib` | Safe update checks (`checkupdates`) for Software & updates. |
| **Calculator** | `libqalculate` | The calculator in Spotlight (`qalc`). |
| **Dialogs** | `zenity` | File and directory selection dialogs. |
| **QR Scan** | `zbar` | QR code scanning (`zbarimg`). |
| **OCR** | `tesseract` `tesseract-data-eng` | Copy text from the screen (region tools). |
| **Biometric (Opt.)** | `fprintd` | Fingerprint sensor support on the lock screen. |
| **Flatpak (Opt.)** | `flatpak` | Flatpak updates, search and installs in Software & updates. |
| **Firmware (Opt.)** | `fwupd` | Firmware updates from the LVFS in Settings → Firmware (offered there when missing). |
| **Printing (Opt.)** | `cups` | Printing from the PDF viewer (`lp`) and Settings → Printers. |
| **Login (Opt.)** | `greetd` | Hosts the Material3UI login screen. |

### Theming

| Tool | Package | Purpose |
| :--- | :--- | :--- |
| **GTK settings** | `nwg-look` | GTK theme, icons and fonts outside the shell. |
| **Qt** | `qt5ct` `qt6ct` | Qt apps in the theme's colours. |
| **Icons** | `breeze` `breeze-icons` | Fallback icons. The shell's own Material You icon theme (Settings → Look & feel) needs nothing more. |

### Live wallpapers

| Tool | Package | Purpose |
| :--- | :--- | :--- |
| **Video** | `mpvpaper` `socat` | Video wallpapers, controlled through mpv's IPC. |
| **Wallpaper Engine** | `linux-wallpaperengine-git` | Steam Wallpaper Engine support (deprecated: mpvpaper replaces it). |

### Fonts

| Font | Package | Source / Purpose |
| :--- | :--- | :--- |
| **UI Font** | `Google Sans Flex` | Primary variable font (from GitHub). |
| **Icons** | `ttf-material-symbols-variable-git` | Material Symbols: the shell's icons and its icon theme. |
| **Monospace** | `ttf-jetbrains-mono-nerd` | Default monospace and nerd font (the terminal and the text editor). |
| **Scripts** | `noto-fonts` `noto-fonts-cjk` `noto-fonts-emoji` | Indic, Chinese, Japanese and Korean text, and emoji (lyrics, files, the terminal). |

### Apps & Shell (Optional)

The shell brings its own terminal, file manager, image viewer, video player, PDF viewer, text editor, notes and task manager. The installer offers these as defaults or alternatives:

| Tool | Package | Purpose |
| :--- | :--- | :--- |
| **Terminal** | `material3ui-terminal`, or `alacritty` `kitty` `foot` `ghostty` | The shell's own is offered first; alacritty and kitty get the wallpaper's colours too. |
| **File manager** | `material3ui-files`, or `nemo` `nautilus` `dolphin` `thunar` | The shell's Files is the default. |
| **Browser** | `librewolf`, `firefox`, `chromium` or `zen-browser-bin` | The default web browser. |
| **Shell** | `fish` | Interactive shell. |
| **Prompt** | `starship` | Cross-shell prompt. |
| **Utils** | `bash`, `awk`, `grep` | Standard Unix utilities used by core scripts. |
