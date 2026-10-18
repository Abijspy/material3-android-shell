# Login Screen (greetd)

The login screen is the shell itself: `material3ui greeter` talks to [greetd](https://sr.ht/~kennylevinsen/greetd/) and draws the lock screen's view, so it has the same clock, widgets, password field and motion. Style both in **Settings → Lock screen**. It adds a user picker, a session picker, and suspend / reboot / power-off. It remembers the last user and session.

```bash
sudo pacman -S greetd
greeter/material3ui-greeter install --enable   # or pick "login screen" in install.sh / pass --greeter
material3ui-greeter preview                    # try it inside your session (logins disabled)
material3ui-greeter status
material3ui-greeter uninstall                  # restores your previous greetd config
```

greetd runs a minimal Hyprland session (`/usr/share/material3ui-greeter/hyprland.lua`) that starts the greeter and exits once you log in. The `greeter` user cannot read your home directory, so `material3ui-greeter sync` copies these to `/var/lib/material3ui-greeter`:
- your wallpaper and Material colours (the lock-screen wallpaper's, when you use a separate one);
- the lock screen's clock and widgets, your weather settings and the last forecast (the greeter refreshes it once the network is up);
- your keyboard layout and monitor rules.

The shell runs the sync by itself after every recolour and a few seconds after any settings change. Turn that off, or sync by hand, in **Settings → Lock screen → Login screen**.
