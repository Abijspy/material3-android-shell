# Login Screen (greetd)

The login screen is the shell itself: `material3ui greeter` talks to [greetd](https://sr.ht/~kennylevinsen/greetd/) and draws the lock screen's view, so it has the same clock, widgets, password field and motion. Style both in **Settings → Lock screen**. It adds a user picker, a session picker, and suspend / reboot / power-off. The session picker shows each compositor's logo (Hyprland's drop, niri's) next to its sessions. It remembers the last user and session.

```bash
greeter/material3ui-greeter install --enable   # or pick "login screen" in install.sh / pass --greeter
material3ui-greeter preview                    # try it inside your session (logins disabled)
material3ui-greeter status
material3ui-greeter uninstall                  # restores your previous greetd config
```

It installs as its own pacman package, `material3ui-greeter` (greetd comes with it). Each GitHub release carries the package, built by the *Release packages* workflow; `install` downloads it when your checkout is on a release and builds it from the checkout otherwise (the canary channel, local changes). Updates do the same: `update.sh` and **Settings → Lock screen → Login screen updates** reinstall it whenever the checkout's greeter differs from the installed one. A release package can also be installed directly:

```bash
sudo pacman -U material3ui-greeter-4.4.0-1-any.pkg.tar.zst
material3ui-greeter setup --enable             # point greetd at it
```

To build one by hand: `packaging/build-pkg.sh material3ui-greeter` (writes to `packaging/out/`).

greetd runs a minimal compositor session that starts the greeter and exits once you log in: Hyprland's (`/usr/share/material3ui-greeter/hyprland.lua`), or niri's (`niri.kdl`) when Hyprland isn't installed. The package needs one of the two. The `greeter` user cannot read your home directory, so `material3ui-greeter sync` copies these to `/var/lib/material3ui-greeter`:
- your wallpaper and Material colours (the lock-screen wallpaper's, when you use a separate one);
- the lock screen's clock and widgets, your weather settings and the last forecast (the greeter refreshes it once the network is up);
- your keyboard layout, cursor and monitors: Hyprland's monitor rules for the Hyprland session (`hyprland-local.lua`), and for the niri session (`niri-local.kdl`) the `output` blocks of your niri `config.kdl` and of the shell's niri settings. That file is checked with `niri validate`, and the monitors are left out if niri refuses them.

The shell runs the sync by itself after every recolour and a few seconds after any settings change. Turn that off, or sync by hand, in **Settings → Lock screen → Login screen**.
