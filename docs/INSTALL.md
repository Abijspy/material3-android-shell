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

The easiest way to install Material3UI Shell is by using the interactive installation script. Run the following command in your terminal:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

The script will guide you through the process step by step. Here is what to expect during the installation:

### 1. Choosing the Install Location
By default, the shell will be installed in `~/.local/src/material3ui`. You can choose a different location if you prefer.

### 2. Installing Dependencies
The installer categorizes dependencies to give you control over what gets installed:
* **Core Dependencies (Required):** Includes Hyprland, Quickshell, Pipewire, NetworkManager, Matugen, and essential CLI tools like Python3. Python3 is specifically required to apply dynamic colors to your terminal.
* **Fonts (Recommended):** Sets up Google Sans Flex, Material Symbols Rounded, and JetBrains Mono Nerd Font for the intended visual experience.
* **Terminal Setup (Optional):** Installs Kitty, Fish, and Starship if you want your terminal to match the shell's aesthetics.
* **CLI Tool (Optional):** Installs the Material3UI CLI for unified command line control.

### 3. Copying Configuration Files
The script will safely copy necessary configuration files into your `~/.config/` directory:
* `quickshell/material3ui/` contains the actual shell.
* `matugen/` contains template configurations for theme generation.
* `starship.toml` provides the custom prompt layout if you use Starship.

### 4. Setting Up the Material3UI CLI (Optional)
The repository ships `material3uictl`, a small client for the shell's `material3ui` IPC target. The installer offers to copy it to `~/.local/bin/`; to do it by hand:

```bash
install -Dm755 dotfiles/.local/bin/material3uictl ~/.local/bin/material3uictl
material3uictl help
```

Make sure `~/.local/bin` is on your `$PATH`.

### 5. Configuration Injection (Optional)
The installer can safely append source lines to your existing configurations without deleting your personal settings:
* **Kitty:** Injects `include current-theme.conf` for dynamic color support.
* **Fish:** Injects the Starship initialization command.
* **Hyprland:** Creates a dedicated `material3ui.lua` file and sources it in your main configuration.

### 6. Selecting an Update Channel
You can choose how you want to receive updates:
* **Stable:** Follows official release tags.
* **Canary:** Follows the absolute latest commits on the main branch for cutting edge features.

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
