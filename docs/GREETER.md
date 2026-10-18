# Login Screen (greetd)

`greeter/` is a Quickshell greeter for [greetd](https://sr.ht/~kennylevinsen/greetd/) with the same UI and motion as the lock screen: the same centred clock (your lock-screen clock style, fonts and time/date format), the same bottom password pill with Material-shape dots, shake and arrow button, and the same entrance animation. It adds a user chip (avatar + name, with a user picker), a session picker in the pill's leading slot, and suspend / reboot / power-off.

```bash
sudo pacman -S greetd
greeter/material3ui-greeter install --enable   # or pick "login screen" in install.sh / pass --greeter
material3ui-greeter preview                    # try it inside your session
material3ui-greeter status
material3ui-greeter uninstall                  # restores your previous greetd config
```

greetd runs a minimal Hyprland session (`/usr/share/material3ui-greeter/hyprland.lua`) that hosts the greeter and exits once you log in. Because the `greeter` user cannot read your home directory, `material3ui-greeter sync` copies your wallpaper, Material colours (the lock-screen palette when you use a separate lock wallpaper), avatar, clock settings, keyboard layout and monitor rules to `/var/lib/material3ui-greeter`. With **Settings → Wallpaper & Style → Lockscreen → Auto sync login screen** (on by default) the shell does this automatically whenever any of them change; "Sync now" and "Preview" are next to it.
