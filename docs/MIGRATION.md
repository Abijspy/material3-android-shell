# Migrating to Material3UI Shell from KDE or GNOME

Making the jump from a full desktop environment like KDE Plasma or GNOME to a standalone window manager like Hyprland can feel daunting. This guide will help you understand exactly what changes when you switch to Material3UI Shell and how to set up your new environment seamlessly.

## Understanding the Shift

When you move to Material3UI Shell, you are only replacing the "shell" portion of your desktop. This means your panels, notifications, and system controls are swapped out, but much of the underlying infrastructure you rely on remains exactly the same.

### What Material3UI Will Replace

| Your Old Feature | Your New Material3UI Equivalent |
| :--- | :--- |
| Top or bottom panel (taskbar) | The Dynamic Island and Status Bar |
| System tray | Integrated smoothly into the Status Bar |
| Notification center | The Notification Center panel |
| Quick settings or control center | The Quick Settings panel |
| App launcher or menu | The App Launcher paired with Spotlight Search |
| Volume and brightness popups | Custom on screen display (OSD) overlays |
| Lock screen | The shell's own lock screen (ext-session-lock and PAM), with hyprlock as a fallback |
| Display settings | Built-in Display settings for brightness and night light; monitor layout stays in `monitors.lua` |
| Bluetooth and Wi-Fi settings | Built-in controls within the Quick Settings and main Settings panel |
| System monitor | A built-in System Monitor (<kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>Del</kbd>) |
| Dock | A dock with pinned and running apps |

### What Material3UI Does Not Replace

Material3UI relies on standard Linux portals and your own choice of apps for these features:

| Feature | What You Need to Use |
| :--- | :--- |
| **File picker dialogs** | `xdg-desktop-portal-gtk` or your portal of choice |
| **Screen sharing dialogs** | `xdg-desktop-portal-hyprland` |
| **File manager** | Dolphin, Nautilus, Thunar, or any manager you prefer |
| **Text editor or IDE** | Your existing favorite editor |
| **GTK and Qt theming** | Matugen generates GTK CSS, but you might want `nwg-look` or `qt6ct` for fine tuning |
| **Authentication prompts** | Material3UI includes its own Polkit agent |
| **Clipboard manager** | Built in (<kbd>Super</kbd><kbd>V</kbd>), using `cliphist` and `wl-clipboard` |
| **Screenshot tool** | Built in (<kbd>Super</kbd><kbd>G</kbd>), using `grim` and `slurp` |

## Setting Up Your New Environment

### 1. Install Hyprland
If you have not already done so, install and configure Hyprland as your system compositor:

```bash
sudo pacman -S hyprland
```

Your existing `~/.config/hypr/hyprland.lua` can stay: run the installer with `--hypr inject` and it only appends `require("material3ui.shell")` (plus the colour and persistence modules), so your personal shortcuts are never overwritten. The default `full` mode instead backs up your file and installs the modular Material3UI config.

### 2. Configure Desktop Portals
This step is crucial. Without the proper portals, fundamental features like screen sharing and file selection will fail. Install them using:

```bash
sudo pacman -S xdg-desktop-portal-hyprland xdg-desktop-portal-gtk
```

Make sure your Hyprland configuration starts the dbus environment properly:

```lua
hl.exec_cmd("sleep 1 && dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP")
```

After logging in, verify that the services are active:

```bash
systemctl --user status xdg-desktop-portal-hyprland
systemctl --user status xdg-desktop-portal-gtk
```

### 3. Install Material3UI Shell
Run the installation script to get everything set up:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

During the prompts, install the core dependencies and the fonts. The installer builds the shell and sets Hyprland to start it.

### 4. Drive the shell from a terminal
Every panel and action is a `material3ui msg` command:

| What You Want to Do | Command |
| :--- | :--- |
| **Toggle Spotlight** | `material3ui msg launcher` |
| **Toggle the Dashboard** | `material3ui msg popup dashboard` |
| **Open Settings** | `material3ui msg settings` |
| **Set the wallpaper** | `material3ui msg wallpaper set <file>` |
| **Restart the shell** | `pkill -x material3ui; material3ui &` |

`material3ui msg help` lists every command; see the [IPC reference](IPC.md).

### 5. Keybinds
With the default `full` Hyprland mode you do not need to write any bindings: they live in `~/.config/hypr/material3ui/shell.lua` (shell panels) and `keybinds.lua` (apps, windows, media). Override or add your own in `~/.config/hypr/material3ui/user.lua`:

```lua
-- user.lua: loaded last, never overwritten
hl.bind("SUPER + C", hl.dsp.exec_cmd("code"))
hl.bind("SUPER + M", hl.dsp.exec_cmd("material3ui msg popup dashboard"))
```

If you keep your own `hyprland.lua` (`--hypr inject`), adding `require("material3ui.shell")` gives you the shell, its panel shortcuts and region tools without changing anything else.

## Advice for KDE Plasma Migrants

If you are coming directly from KDE Plasma on Wayland:
1. Make sure to remove any KDE autostart entries that might conflict, such as `plasmashell` or `kwin`.
2. Keep `qt6ct` installed if you rely on it for Qt application theming. Matugen will generate color schemes that play nicely alongside it.
3. Dolphin will continue to work perfectly as your main file manager.
4. KDE Connect operates independently and will keep functioning as expected.

## Advice for GNOME Migrants

If you are coming from GNOME:
1. You must switch your portal setup. Remove `xdg-desktop-portal-gnome` and replace it with `xdg-desktop-portal-gtk`. The GTK portal plays very nicely with Hyprland, while the GNOME one generally causes issues.
2. Nautilus (Files) will still work as your primary file manager.
3. Keep in mind that GNOME extensions will no longer apply. Material3UI Shell provides all the equivalent shell features natively.

## Updating Safely

When an update for Material3UI drops, run `update.sh` from your checkout (`update.sh all`, or `update.sh shell` for the shell alone). It pulls, rebuilds and reinstalls the shell (asking for sudo), refreshes the configs and restarts the shell. Your personal Hyprland keybinds, terminal settings and custom scripts are never overwritten.

System packages are updated from **Settings → Packages & Updates**.

## Troubleshooting Migration Issues

**Tip:** `material3ui msg status` summarises every service the shell talks to (audio, network, Bluetooth, battery, media), so it is the quickest way to see what is missing.

Common migration problems:

| Symptom | The Likely Cause | How to Fix It |
| :--- | :--- | :--- |
| No file picker appears when saving | A missing GTK portal | Install and enable `xdg-desktop-portal-gtk` |
| Screen sharing only shows a black screen | A missing Hyprland portal | Install and enable `xdg-desktop-portal-hyprland` |
| Your old KDE panel is still showing up | Plasma is still running in the background | Remove `plasmashell` from your autostart entries |
| GTK apps look completely unstyled | No GTK theme has been applied | Matugen should auto-generate your GTK CSS, but you can also use `nwg-look` to force a theme |
| Icons are missing throughout the shell | The correct font is not installed | Install `ttf-material-symbols-variable-git` from the AUR |
| Your clipboard history is empty | `cliphist` is missing | Install `cliphist` and `wl-clipboard`; the shell starts the watchers itself |

## Coming from Material3UI v3

v4 replaces the Quickshell (QML) shell with the native `material3ui` binary:
- `install.sh` and `update.sh` build and install it, then back up and remove `~/.config/quickshell/material3ui`.
- Hyprland now starts `material3ui`, and every bind goes through `material3ui msg`.
- `material3uictl` and `material3ui-sysmon` are removed. Use `material3ui msg` (see the [IPC reference](IPC.md)); the system monitor reads `/proc` itself.
- Your keybind edits, wallpaper and Matugen colours carry over. Settings pages start from their defaults wherever v3 stored a value under a different key.
- Once the shell runs, you can uninstall `quickshell-git` and the `qt6-*` packages it pulled in, unless other apps need them.
