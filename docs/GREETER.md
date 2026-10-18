# Login Screen (greetd)

The login screen is the shell itself: `material3ui greeter` talks to [greetd](https://sr.ht/~kennylevinsen/greetd/) and uses the lock screen's view, with the same clock, password field and motion. It adds a user picker, a session picker, and suspend / reboot / power-off. It remembers the last user and session.

```bash
sudo pacman -S greetd
greeter/material3ui-greeter install --enable   # or pick "login screen" in install.sh / pass --greeter
material3ui-greeter preview                    # try it inside your session (logins disabled)
material3ui-greeter status
material3ui-greeter uninstall                  # restores your previous greetd config
```

greetd runs a minimal Hyprland session (`/usr/share/material3ui-greeter/hyprland.lua`) that starts the greeter and exits once you log in. The `greeter` user cannot read your home directory, so `material3ui-greeter sync` copies these to `/var/lib/material3ui-greeter`:
- your wallpaper and Material colours (the lock-screen wallpaper's, when you use a separate one);
- your keyboard layout and monitor rules.

The shell runs the sync by itself after every recolour. Turn that off in **Settings → Lock & idle → Match the login screen**.
