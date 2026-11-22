# Migrating to Material3UI Shell from KDE or GNOME

Making the jump from a full desktop environment like KDE Plasma or GNOME to a standalone compositor like niri can feel daunting. This guide will help you understand exactly what changes when you switch to Material3UI Shell and how to set up your new environment seamlessly.

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
| Lock screen | The shell's own lock screen (ext-session-lock and PAM) |
| Display settings | Built-in Display settings: brightness, night light, and each monitor's resolution, scale and place |
| Bluetooth and Wi-Fi settings | Built-in controls within the Quick Settings and main Settings panel |
| System monitor | A built-in Task Manager (<kbd>Ctrl</kbd><kbd>Alt</kbd><kbd>Del</kbd>) |
| File manager | **Files** (<kbd>Super</kbd><kbd>E</kbd>): drives, servers, archives, trash, previews |
| Image viewer | **Photos**, with a gallery and an editor |
| Video player | **Videos** (mpv): every format, subtitles |
| Terminal | The shell's own terminal (<kbd>Super</kbd><kbd>Return</kbd>), with tabs |
| PDF viewer | **PDF**: reading, annotating, signing and forms |
| Text editor | **Text Editor**, for .txt files and code |
| Notes app | **Notes**: notes and checklists |
| Icon theme | The Material You icon theme (Settings → Look & feel) |
| Dock | A dock with pinned and running apps |

### What Material3UI Does Not Replace

Material3UI relies on standard Linux portals and your own choice of apps for these features:

| Feature | What You Need to Use |
| :--- | :--- |
| **File picker dialogs** | `xdg-desktop-portal-gtk` or your portal of choice |
| **Screen sharing dialogs** | `xdg-desktop-portal-gnome` (niri's screen sharing goes through it) |
| **Office suite, browser, IDE** | Your own: LibreOffice, a browser, VS Code or any editor you prefer |
| **Other file managers or terminals** | Optional: the shell's own are the defaults, and Settings → Default apps picks Dolphin, Nautilus, kitty or any other |
| **GTK and Qt theming** | Matugen generates GTK CSS, but you might want `nwg-look` or `qt6ct` for fine tuning |
| **Authentication prompts** | Material3UI includes its own Polkit agent |
| **Clipboard manager** | Built in (<kbd>Super</kbd><kbd>V</kbd>), using `cliphist` and `wl-clipboard` |
| **Screenshot tool** | Built in (<kbd>Super</kbd><kbd>G</kbd>), using `grim` and `slurp` |

## Setting Up Your New Environment

### 1. Install niri
The installer installs niri (26.04 or newer) for you. If you already use it, your `~/.config/niri/config.kdl` can stay: the installer adds one `include "material3ui.kdl"` line at its end (backed up first), so your own keys and settings are never overwritten. With `--niri skip` it doesn't touch it at all.

### 2. Desktop Portals
Without the proper portals, screen sharing and file selection fail. The installer installs both: `xdg-desktop-portal-gnome`, through which niri shares the screen, and `xdg-desktop-portal-gtk` for file pickers. After logging into niri, verify that they're active:

```bash
systemctl --user status xdg-desktop-portal-gnome
systemctl --user status xdg-desktop-portal-gtk
```

### 3. Install Material3UI Shell
Run the installation script to get everything set up:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

The full-screen installer walks you through a few pages (niri, your apps, extras such as the fonts), shows one review and then installs. It builds the shell and has niri start it. → [Installation guide](INSTALL.md)

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
You don't need to write any bindings. niri's own keys stay in your `config.kdl`, and the shell's panel shortcuts live in `~/.config/niri/material3ui.kdl`. Add or override keys in **Niri Settings → Keys**, or in `~/.config/niri/material3ui-user.kdl`, which is read last and never overwritten:

```kdl
// material3ui-user.kdl
binds {
    Mod+C { spawn "code"; }
    Mod+M { spawn "material3ui" "msg" "popup" "dashboard"; }
}
```

## Advice for KDE Plasma Migrants

If you are coming directly from KDE Plasma on Wayland:
1. Make sure to remove any KDE autostart entries that might conflict, such as `plasmashell` or `kwin`.
2. Keep `qt6ct` installed if you rely on it for Qt application theming. Matugen will generate color schemes that play nicely alongside it.
3. Dolphin will continue to work perfectly as your main file manager.
4. KDE Connect operates independently and will keep functioning as expected.

## Advice for GNOME Migrants

If you are coming from GNOME:
1. Keep `xdg-desktop-portal-gnome`: niri shares the screen through it. The installer adds `xdg-desktop-portal-gtk` beside it for file pickers.
2. Nautilus (Files) will still work as your primary file manager.
3. Keep in mind that GNOME extensions will no longer apply. Material3UI Shell provides all the equivalent shell features natively.

## Updating Safely

When an update for Material3UI drops, run `update.sh` from your checkout (`update.sh all`, or `update.sh shell` for the shell alone). It pulls, rebuilds and reinstalls the shell (asking for sudo), refreshes the configs and restarts the shell. Your `config.kdl`, `material3ui-user.kdl`, terminal settings and custom scripts are never overwritten.

System packages are updated from **Settings → Software & updates**.

## Troubleshooting Migration Issues

**Tip:** `material3ui msg status` summarises every service the shell talks to (audio, network, Bluetooth, battery, media), so it is the quickest way to see what is missing.

Common migration problems:

| Symptom | The Likely Cause | How to Fix It |
| :--- | :--- | :--- |
| No file picker appears when saving | A missing GTK portal | Install and enable `xdg-desktop-portal-gtk` |
| Screen sharing only shows a black screen | A missing GNOME portal | Install and enable `xdg-desktop-portal-gnome` |
| Your old KDE panel is still showing up | Plasma is still running in the background | Remove `plasmashell` from your autostart entries |
| GTK apps look completely unstyled | No GTK theme has been applied | Matugen should auto-generate your GTK CSS, but you can also use `nwg-look` to force a theme |
| Icons are missing throughout the shell | The correct font is not installed | Install `ttf-material-symbols-variable-git` from the AUR |
| Your clipboard history is empty | `cliphist` is missing | Install `cliphist` and `wl-clipboard`; the shell starts the watchers itself |

## Coming from Material3UI v3

v4 replaces the Quickshell (QML) shell with the native `material3ui` binary:
- `install.sh` and `update.sh` build and install it, then back up and remove `~/.config/quickshell/material3ui`.
- The compositor starts `material3ui`, and every bind goes through `material3ui msg`.
- `material3uictl` and `material3ui-sysmon` are removed. Use `material3ui msg` (see the [IPC reference](IPC.md)); the system monitor reads `/proc` itself.
- Your keybind edits, wallpaper and Matugen colours carry over. Settings pages start from their defaults wherever v3 stored a value under a different key.
- Once the shell runs, you can uninstall `quickshell-git` and the `qt6-*` packages it pulled in, unless other apps need them.

## Coming from Material3UI on Hyprland or Umbriel

From v7.0 the shell runs on niri only:
- Run `./install.sh` again. It sets niri up and leaves your Hyprland or Umbriel config as it was; the shell won't run there any more. `./update.sh` says the same when your last install didn't set niri up.
- Hyprland Settings, Umbriel Settings, the Hyprland keybind editor, the magnifier and colour filters, greyscale at bedtime and the shell's own screen-sharing picker are gone. Niri Settings and Settings → Keyboard take over keys; screen sharing goes through the GNOME portal.
- Your settings, wallpaper and colours carry over. Settings backups made before 7.0 still restore your settings.
- To take the shell out of Hyprland, remove the `require("material3ui.…")` lines from `hyprland.lua`; out of Umbriel, take `material3ui.toml` out of `[include.optional]` and `material3ui` out of `general.autostart` in `config.toml`.
