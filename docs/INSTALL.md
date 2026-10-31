# Material3UI Shell Installation Guide

Welcome! This guide will walk you through getting Material3UI Shell up and running on your system. It is specifically tailored for clean Arch Linux or CachyOS setups running Hyprland or niri.

## Understanding the Scope

Before we dive in, let's clarify what Material3UI Shell actually is. It is a desktop shell for Hyprland and niri (see [Material3UI on niri](NIRI.md)) that replaces your standard panels, notifications, and system controls. However, it is not a complete dotfiles package that overrides your entire system.

**What you get:**
* A universal dynamic island, status bar, and notification center.
* A quick settings panel for Wi-Fi, Bluetooth, and brightness.
* An app launcher, spotlight search, and a visual dashboard.
* A beautifully themed lockscreen and on-screen displays (OSD).
* Automatic Material 3 theme generation based on your wallpaper using Matugen.

**What you need to configure yourself:**
* File pickers and screen sharing dialogs. The dependencies install the required portals (`xdg-desktop-portal-hyprland` or, on niri, `xdg-desktop-portal-gnome`, and `xdg-desktop-portal-gtk`), but they remain your system's responsibility.
* Window management keybinds. You will configure these directly in your `hyprland.lua`, or in niri's `config.kdl`.
* Your choice of terminal emulator and shell. We provide optional aesthetics for kitty, fish, and starship, but they are not strictly required.
* A full desktop environment suite like a file manager or app store.

## Prerequisites

Before running the installer, make sure your system meets these basic requirements:
* You are running Arch Linux, CachyOS, or a similar Arch-based distribution.
* Nothing for the compositor: the installer installs Hyprland, niri or both, whichever you pick. One you have already needs to be Hyprland 0.55 or newer, or niri 26.04 or newer; if it's older, the installer offers to update the system first.
* You have an AUR helper installed. If you do not have one, the installer will set up `yay` for you.
* Standard build tools like `git`, `curl`, and `base-devel` are present on your system.

## Quick Installation

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Abijspy/material3-android-shell/main/install.sh)"
```

When run this way the repository is cloned to `~/.local/src/material3ui` (change it with `--dir`) and the installer continues from the clone. You can also run `./install.sh` from any checkout.

The installer asks everything first, shows one plan, and after you confirm it runs to the end without further questions:

1. **Checks:** an Arch-based system, not root, and Hyprland and niri: installed (and new enough: Hyprland 0.55 for the Lua config, niri 26.04), and what your repositories have.
2. **Your setup:**
   - **The compositor first:** `hyprland`, `niri` or `both` (picked at each login). Each option says where it stands: installed with its version, "installs first" with the version it will get, or installed but too old. The default is what's installed already. Then how to handle Hyprland's config (`full`, `inject` or `skip`) and niri's (`include` or `skip`).
   - **An old compositor:** if the one you picked is installed but older than the shell needs, the installer offers a full system update (`sudo pacman -Syu`) to bring the new one, since Arch doesn't do partial upgrades. If your repositories can't bring a new enough one, it says so.
   - **Apps:** your terminal (Alacritty by default), file manager (Nemo) and browser (LibreWolf), and the update channel.
   - **Extras:** fonts, NetworkManager/Bluetooth, KDE colours, fish + starship, and the login screen.
   - **Re-runs:** when you install again, your previous choices are the defaults.
3. **The plan:** everything in one place.
   - **Packages:** what's missing (found with `pacman -T`), by group in the order they install: the compositor, the shell's own dependencies, tools and theming, your apps, and the extras.
   - **Shell:** the build it will install.
   - **Configs:** every config it will write. `+` is new, and `~` replaces a file, which is backed up first.
   - **Left alone:** configs without the `# Material3UI Configuration` marker, such as your own Matugen or Alacritty setup.
4. **Packages,** before the shell, with `yay` (set up first when it's missing or broken). sudo is asked for once and kept alive until the end.
   - The system update, when you said yes to one.
   - The compositor on its own: Hyprland with its portal, hyprlock, hypridle, hyprsunset and hyprpicker, or niri with xwayland-satellite and the GNOME portal.
   - Then the shell's dependencies, your apps and the extras, and Google Sans Flex.
   - Then a check that each compositor you picked is installed and new enough.
5. **The shell:** first a check that everything the build needs is installed (even with `--skip-deps`, so a missing one stops here with the command to install it, not with a compiler error). Then it's built from `shell/` with meson and installed to `/usr`. The lock screen's PAM file goes to `/etc/pam.d`.
6. **Configs:** the Matugen templates and theme hooks, Alacritty, and Hyprland or niri. niri's config is checked with `niri validate` afterwards. The v3 Quickshell shell and older helpers are backed up and removed.
7. **Session:** NetworkManager and Bluetooth, other polkit agents turned off (the shell is the agent), default apps, and the login screen.

**Answering:** in a terminal, each list is a menu. ↑/↓ (or j/k) move, Enter picks, a number jumps straight to an option, and Home/End go to the ends. Yes/no questions take `y` or `n` at once, or ←/→ then Enter. Each answer folds into one line. Without a terminal (a pipe, or `--yes`), the installer takes typed answers or the defaults.

**Progress:** a bar in the style of Material 3's wavy progress indicator follows the whole run: a solid line for what's done, a moving wave over the step that's running (its share weighted by how long such a step takes, so the build gets the most), then the percentage, the step and how long it's taken. The full output goes to `~/.cache/material3ui/install-<timestamp>.log`, and if a step fails you see the end of that log.

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
./install.sh --yes --hypr inject --channel release --skip-deps
./install.sh --yes --compositor niri                 # niri only
./install.sh --dry-run                               # show the plan and every step, change nothing
```

### Hyprland modes
* **full** (default): `~/.config/hypr/hyprland.lua` is replaced by the Material3UI entry point, which loads modules from `~/.config/hypr/material3ui/`. Put your own settings in `material3ui/user.lua`.
* **inject**: your `hyprland.lua` is kept; `require("material3ui.shell")`, `require("material3ui.colors")` and `require("material3ui.user_persistence")` are appended.
* **skip**: Hyprland config is not touched.

### niri modes
* **include** (default): `~/.config/niri/material3ui.kdl` (the shell, its keys and your apps) is written, and `include "material3ui.kdl"` is added at the end of your `config.kdl`, or of niri's default config when you have none. Put your own changes in `material3ui-user.kdl`, which is included last and never replaced. → [Material3UI on niri](NIRI.md)
* **skip**: niri's config is not touched.

## Post-Installation Steps

### Starting the Shell
Hyprland starts the shell from `shell.lua`, and niri from `material3ui.kdl`. To start or restart it by hand:

```bash
pkill -x material3ui; material3ui &
material3ui msg ping        # → pong
```

### Verifying Environment Portals
For crucial features like screen sharing and file pickers to work, the desktop portals must be active. They usually start automatically with the compositor, but you can verify their status (on niri, `xdg-desktop-portal-gnome` in place of the Hyprland one):

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

### Packages
The shell installs as the `material3ui` pacman package, and the login screen as `material3ui-greeter`. Every release carries both (`material3ui-<version>-1-x86_64.pkg.tar.zst` and `material3ui-greeter-<version>-1-any.pkg.tar.zst`). On the stable channel, updates download the release's package and install it with pacman, with no compiling. On the latest (git) channel, or for a release without a package for your architecture, the update builds the shell from the checkout and packages that build, so pacman owns the files either way (`pacman -Qi material3ui`). Build one by hand with `packaging/build-pkg.sh material3ui`.

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
| The shell refuses to start | Run `material3ui` from a terminal and check the output for errors. |
| The shell does not build | Run `./install.sh` again without `--skip-deps`, or install the libraries in [DEPENDENCIES.md](DEPENDENCIES.md). |
