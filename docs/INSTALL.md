# Material3UI Shell Installation Guide

Welcome! This guide will walk you through getting Material3UI Shell up and running on your system. It is tailored for clean Arch Linux or CachyOS setups running [niri](https://github.com/niri-wm/niri).

## Understanding the Scope

Before we dive in, let's clarify what Material3UI Shell actually is. It is a desktop shell for niri (see [Material3UI on niri](NIRI.md)) that replaces your standard panels, notifications, and system controls. However, it is not a complete dotfiles package that overrides your entire system.

**What you get:**
* A universal dynamic island, status bar, and notification center.
* A quick settings panel for Wi-Fi, Bluetooth, and brightness.
* An app launcher, spotlight search, and a visual dashboard.
* A beautifully themed lockscreen and on-screen displays (OSD).
* Automatic Material 3 theme generation based on your wallpaper using Matugen.

**What you need to configure yourself:**
* File pickers and screen sharing dialogs. The dependencies install the required portals (`xdg-desktop-portal-gnome` for screen sharing on niri, and `xdg-desktop-portal-gtk`), but they remain your system's responsibility.
* Window management keybinds. niri's own live in your `config.kdl`; the shell's in `material3ui.kdl`.
* Your choice of terminal emulator and shell. We provide optional aesthetics for kitty, fish, and starship, but they are not strictly required.
* A full desktop environment suite like a file manager or app store.

## Prerequisites

Before running the installer, make sure your system meets these basic requirements:
* You are running Arch Linux, CachyOS, or a similar Arch-based distribution.
* Nothing for the compositor: the installer installs niri. One you have already needs to be niri 26.04 or newer; if it's older, the installer offers to update the system first.
* You have an AUR helper installed. If you do not have one, the installer will set up `yay` for you.
* Standard build tools like `git`, `curl`, and `base-devel` are present on your system.

## Quick Installation

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

When run this way the repository is cloned to `~/.local/src/material3ui` (change it with `--dir`) and the installer continues from the clone. You can also run `./install.sh` from any checkout.

### The full-screen installer

The installer fills the terminal, in the shell's own Material 3 colours (the dark baseline: lavender primary, rose tertiary). It's plain bash, so it runs on a fresh install, and on the Linux console it switches the console's palette to the same colours and uses symbols the console font can draw, putting the old palette back when it exits. A step tracker under the header shows where you are:

1. **Welcome:** your Material3UI version, where niri stands (installed with its version, "installs first" with the version it will get, or too old), and what it found: your distribution, your login manager, and a Hyprland or Umbriel setup from before v7.0, which it leaves as it is. When you've installed before, it offers **Install again** with last time's choices, straight to the review, or **Change my choices**.
2. **niri:**
   - **Update the system first?** Only when your niri is older than 26.04: the new one comes with a full system update (`sudo pacman -Syu`), since Arch doesn't do partial upgrades.
   - **Your niri config:** include the shell in `config.kdl`, or leave it alone (see [niri modes](#niri-modes)). The page says what it will do with the `config.kdl` you have.
3. **Apps:** your terminal (Alacritty by default), file manager (the shell's own **Files** by default: built in, nothing to install; or Nemo, Thunar, Nautilus or Dolphin) and browser (LibreWolf), one page each. Pictures open in the shell's own image viewer. The shell's keys open them, and **Settings → Default apps** changes them later.
4. **Extras:** tick fonts, NetworkManager and Bluetooth, Material You colours for KDE/Qt apps, fish with starship, and the login screen. If iwd or systemd-networkd also manages your network, it asks whether to turn them off (they fight NetworkManager for the Wi-Fi). Then the update channel.
5. **Review:** everything on one card: niri and its config, your apps, the extras, the channel, the system update, how many packages are missing (found with `pacman -T`, by group), the shell build, and how many configs it will write and back up. It warns when your `config.kdl` also starts something the shell replaces (a bar, a notification daemon, a wallpaper tool). **See every package and file** opens the full plan; **Install now** starts. Nothing changes before that.
6. **Install:** it asks for your password once, in the open, before it starts (nothing can ask behind the full screen), and keeps sudo alive to the end. Each step shows as it runs, with Material 3 Expressive's wavy progress bar, the time taken and the latest log lines. Keys do nothing meanwhile, and <kbd>Ctrl</kbd><kbd>C</kbd> can't stop pacman halfway.
7. **Done:** the main keys, notes from the run (anything it warned about), and, in a niri session, **Start the shell now**; with the login screen, **Reboot now**. If a step fails, the screen shows the errors from that step and where the full log is.

| Key | Does |
| :--- | :--- |
| <kbd>↑</kbd> <kbd>↓</kbd> | Move (<kbd>Home</kbd> / <kbd>End</kbd> jump to the first / last) |
| <kbd>Enter</kbd> | Go on with the highlighted choice |
| <kbd>Space</kbd> | Tick or untick an extra |
| <kbd>Backspace</kbd> or <kbd>←</kbd> | Back to the previous page |
| <kbd>Esc</kbd> | Quit; nothing changes before **Install now** |

It needs a terminal of at least 76×24. In a smaller one, with `--plain`, or with `--yes`, it asks the same questions as plain prompts: each list a menu (↑/↓ or j/k, Enter, or a number), yes/no questions taking `y` or `n` at once, then one plan to confirm. With `--plain`, package installs run in your terminal, where you can answer pacman's prompts.

### What it does, in order

1. **Packages,** before the shell, with `yay` (set up first when it's missing or broken):
   - the system update, when you said yes to one;
   - niri on its own, with xwayland-satellite and the GNOME portal;
   - then the shell's dependencies, your apps and the extras, and Google Sans Flex;
   - then a check that niri is installed and new enough.
2. **The shell:** first a check that everything the build needs is installed (even with `--skip-deps`, so a missing one stops here with the command to install it, not with a compiler error). Then it's built from `shell/` with meson and installed to `/usr` as the `material3ui` pacman package. The lock screen's PAM file goes to `/etc/pam.d`.
3. **Configs:** the Matugen templates and theme hooks, Alacritty, and niri's `material3ui.kdl`. niri's config is checked with `niri validate` afterwards. The v3 Quickshell shell and older helpers are backed up and removed. Configs without the `# Material3UI Configuration` marker, such as your own Matugen or Alacritty setup, are left alone.
4. **Session:** NetworkManager and Bluetooth, other polkit agents turned off (the shell is the agent), default apps, and the login screen.

The full output goes to `~/.cache/material3ui/install-<timestamp>.log`.

### Undo and uninstall

Every replaced file is copied to `~/.config/material3ui/backups/<timestamp>/`, and a `restore.sh` there records each change.

```bash
./install.sh --restore              # put back what the last install changed
./install.sh --restore 20261018-154507   # or an older one
./install.sh --uninstall            # remove the shell from /usr (configs stay)
```

### Unattended installs

```bash
./install.sh --yes                                   # accept every default
./install.sh --yes --terminal foot --file-manager thunar --browser firefox
./install.sh --yes --niri skip --channel release --skip-deps
./install.sh --plain                                 # the questions as plain prompts
./install.sh --dry-run                               # show the plan and every step, change nothing
```

### niri modes
* **include** (default): `~/.config/niri/material3ui.kdl` (the shell, its keys and your apps) is written, and `include "material3ui.kdl"` is added at the end of your `config.kdl`, or of niri's default config when you have none. Put your own changes in `material3ui-user.kdl`, which is included last and never replaced. → [Material3UI on niri](NIRI.md)
* **skip**: niri's config is not touched.

## Post-Installation Steps

### Starting the Shell
niri starts the shell from `material3ui.kdl`. To start or restart it by hand:

```bash
pkill -x material3ui; material3ui &
material3ui msg ping        # → pong
```

### Verifying Environment Portals
For crucial features like screen sharing and file pickers to work, the desktop portals must be active. They usually start automatically with niri, but you can verify their status:

```bash
systemctl --user status xdg-desktop-portal-gnome
systemctl --user status xdg-desktop-portal-gtk
```

If they are not running, enable and start them manually:

```bash
systemctl --user enable --now xdg-desktop-portal-gnome
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

### Packages
The shell installs as the `material3ui` pacman package, and the login screen as `material3ui-greeter`. Every release carries both (`material3ui-<version>-1-x86_64.pkg.tar.zst` and `material3ui-greeter-<version>-1-any.pkg.tar.zst`). On the stable channel, updates download the release's package and install it with pacman, with no compiling. On the latest (git) channel, or for a release without a package for your architecture, the update builds the shell from the checkout and packages that build, so pacman owns the files either way (`pacman -Qi material3ui`). Build one by hand with `packaging/build-pkg.sh material3ui`.

Whichever method you choose, updating is non-destructive. Your `config.kdl` and `material3ui-user.kdl` are never replaced, `material3ui.kdl` keeps the apps you picked in Settings → Default apps, and your Fish functions and Kitty settings are always safe.

## Troubleshooting Common Issues

**First step: Run a Dependency Check!**
If something feels broken or missing, chances are a required package is not installed. When you first install the shell, the **Onboarding Panel** will automatically open to help you verify these dependencies. If you need to check them again later, simply navigate to **Material3UI Settings > About > Dependency Check**. This built in tool will tell you exactly what you are missing.

If all dependencies are met and you still face issues, check these common solutions:

| Issue | Solution |
| :--- | :--- |
| Icons show as squares or are missing | Install `ttf-material-symbols-variable-git` from the AUR. |
| The font looks wrong or uses a fallback | Install Google Sans Flex via the install script. |
| File picker popups do not appear | Make sure `xdg-desktop-portal-gtk` is running. |
| Screen share dialogs do not appear | Make sure `xdg-desktop-portal-gnome` is running. |
| Terminal colors are not applying | Verify that `python3` is installed on your system. |
| The shell refuses to start | Run `material3ui` from a terminal and check the output for errors. It needs a niri session. |
| The shell does not build | Run `./install.sh` again without `--skip-deps`, or install the libraries in [DEPENDENCIES.md](DEPENDENCIES.md). |
| The installer shows plain prompts | The terminal is smaller than 76×24: make it bigger, or answer the prompts. |
