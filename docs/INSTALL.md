# Material3UI Shell Installation Guide

Welcome! This guide will walk you through getting Material3UI Shell up and running on your system. It is specifically tailored for clean Arch Linux or CachyOS setups running Hyprland.

## Understanding the Scope

Before we dive in, let's clarify what Material3UI Shell actually is. It is a desktop shell built for Hyprland that replaces your standard panels, notifications, and system controls. However, it is not a complete dotfiles package that overrides your entire system.

**What you get:**
* A universal dynamic island, status bar, and notification center.
* A quick settings panel for Wi-Fi, Bluetooth, and brightness.
* An app launcher, spotlight search, and a visual dashboard.
* A beautifully themed lockscreen and on-screen displays (OSD).
* Automatic Material 3 theme generation based on your wallpaper using Matugen.

**What you need to configure yourself:**
* File pickers and screen sharing dialogs. The dependencies install the required portals (`xdg-desktop-portal-hyprland` and `xdg-desktop-portal-gtk`), but they remain your system's responsibility.
* Window management keybinds. You will configure these directly in your `hyprland.lua`.
* Your choice of terminal emulator and shell. We provide optional aesthetics for kitty, fish, and starship, but they are not strictly required.
* A full desktop environment suite like a file manager or app store.

## Prerequisites

Before running the installer, make sure your system meets these basic requirements:
* You are running Arch Linux, CachyOS, or a similar Arch-based distribution.
* Hyprland is installed and currently running as your compositor.
* You have an AUR helper installed. If you do not have one, the installer will attempt to set up `paru` for you.
* Standard build tools like `git`, `curl`, and `base-devel` are present on your system.

## Quick Installation

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

When run this way the repository is cloned to `~/.local/src/material3ui` (change it with `--dir`) and the installer continues from the clone. You can also run `./install.sh` from any checkout.

The installer runs seven steps:

1. **Preflight** checks that you are on an Arch-based system, not root, and that Hyprland is 0.55 or newer (needed for the Lua config). Everything is logged to `~/.cache/material3ui/install-<timestamp>.log`.
2. **Choices**: terminal (Alacritty by default), file manager (Nemo), browser (LibreWolf), how to handle Hyprland (`full`, `inject` or `skip`), update channel, and optional components (fonts, NetworkManager/Bluetooth services, KDE theming, live wallpapers, fish + starship, EasyEffects/WARP).
3. **Dependency review** lists what is already installed and what is missing, and asks before installing anything.
4. **Packages** are installed with `paru` or `yay` (paru is bootstrapped when neither exists), followed by Google Sans Flex and optional extras.
5. **Configuration review** shows every file that will be written. Existing files are marked and backed up to `~/.config/material3ui/backups/<timestamp>/`. Configs without the `# Material3UI Configuration` marker (your own Matugen or Alacritty setup) are left alone.
6. **Deploy** installs the shell, Matugen templates, Alacritty config, Hyprland modules and `material3uictl`, sets the XDG default file manager and browser, and removes Material3UI v1 helpers if present.
7. **Summary** offers to reload Hyprland and start the shell when you are already in a Hyprland session.

### Unattended installs

```bash
./install.sh --yes                                   # accept every default
./install.sh --yes --terminal foot --file-manager thunar --browser firefox
./install.sh --yes --hypr inject --channel release --skip-deps
./install.sh --dry-run                               # print every step, change nothing
```

### Hyprland modes
* **full** (default): `~/.config/hypr/hyprland.lua` is replaced by the Material3UI entry point, which loads modules from `~/.config/hypr/material3ui/`. Put your own settings in `material3ui/user.lua`.
* **inject**: your `hyprland.lua` is kept; `require("material3ui.shell")`, `require("material3ui.colors")` and `require("material3ui.user_persistence")` are appended.
* **skip**: Hyprland config is not touched.

## Post-Installation Steps

### Starting the Shell
If you allowed the installer to inject configurations, you can simply restart Hyprland. Otherwise, you can launch the shell manually:

```bash
quickshell -c material3ui
```

### Verifying Environment Portals
For crucial features like screen sharing and file pickers to work, the desktop portals must be active. They usually start automatically with Hyprland, but you can verify their status:

```bash
systemctl --user status xdg-desktop-portal-hyprland
systemctl --user status xdg-desktop-portal-gtk
```

If they are not running, enable and start them manually:

```bash
systemctl --user enable --now xdg-desktop-portal-hyprland
systemctl --user enable --now xdg-desktop-portal-gtk
```

### Generating Your First Theme
When you first launch the shell, it will use a default color palette. To personalize it:
1. Open the Quick Settings or the main Settings panel.
2. Select and apply a new wallpaper.
3. Matugen will automatically extract colors from your wallpaper and generate a fresh Material 3 theme across your entire system.

## Keeping Everything Updated

When a new version is released, you have two easy ways to update your setup:

### Graphical Shell Update (Recommended)
Open the **Material3UI Settings** panel, navigate to the **About** section, and click on **Shell Update**. From there, you can easily choose to update everything or just the shell with a single click.

### Manual Terminal Update
If you prefer the command line, you can run the update script directly:

```bash
# Update everything including the shell, matugen templates, and starship config
~/.local/src/material3ui/update.sh all

# Update only the shell logic, leaving your other configs completely untouched
~/.local/src/material3ui/update.sh shell
```

Whichever method you choose, updating is non-destructive. It safely overlays new files without deleting your existing configurations, so your personal Hyprland keybinds, Fish functions, and Kitty settings are always safe.

## Troubleshooting Common Issues

**First step: Run a Dependency Check!** 
If something feels broken or missing, chances are a required package is not installed. When you first install the shell, the **Onboarding Panel** will automatically open to help you verify these dependencies. If you need to check them again later, simply navigate to **Material3UI Settings > About > Dependency Check**. This built in tool will tell you exactly what you are missing.

If all dependencies are met and you still face issues, check these common solutions:

| Issue | Solution |
| :--- | :--- |
| Icons show as squares or are missing | Install `ttf-material-symbols-variable-git` from the AUR. |
| The font looks wrong or uses a fallback | Install Google Sans Flex via the install script. |
| File picker popups do not appear | Make sure `xdg-desktop-portal-gtk` is running. |
| Screen share dialogs do not appear | Make sure `xdg-desktop-portal-hyprland` is running. |
| Terminal colors are not applying | Verify that `python3` is installed on your system. |
| The terminal context menu does not open | Ensure `kitty` is installed, as it is the default terminal used. |
| Audio effects are not working | Make sure `easyeffects --daemon` is running in the background. |
| Do Not Disturb does not sync with events | Check if the "Focus" toggle is enabled for your event in the Dashboard. |
| The shell refuses to start | Run `quickshell -c material3ui` from a terminal and check the output for errors. |
