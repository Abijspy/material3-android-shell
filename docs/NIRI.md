# Material3UI on niri (beta)

From 6.0, the shell also runs on [niri](https://github.com/YaLTeR/niri) 26.04 or newer, the scrollable-tiling compositor, as well as on Hyprland. It finds out which one it's on by itself: in a niri session it talks to niri, and in a Hyprland session to Hyprland.

niri support is in **beta**. The desktop itself works (the bar, the dock, the panels, notifications, the lock screen, the launcher and the rest), but the settings that change the compositor are still Hyprland's (see [Not on niri yet](#not-on-niri-yet)). The installer doesn't set niri up yet, so it's done by hand, below.

## Setting it up

1. Install niri and its companions, if you haven't already. xwayland-satellite runs X11 apps, and the GNOME portal handles screen sharing:

   ```bash
   sudo pacman -S niri xwayland-satellite xdg-desktop-portal-gnome
   ```

   Install Material3UI as usual (see the [README](../README.md#install-in-one-line)); its Hyprland setup can stay, for when you log into Hyprland.

2. Copy the shell's niri settings next to your niri config:

   ```bash
   cp dotfiles/.config/niri/material3ui.kdl ~/.config/niri/
   ```

   [`material3ui.kdl`](../dotfiles/.config/niri/material3ui.kdl) starts the shell with niri, puts its wallpaper behind niri's overview, and binds its keys: <kbd>Super</kbd><kbd>Space</kbd> for Spotlight, <kbd>Super</kbd><kbd>N</kbd> for Quick Settings, <kbd>Super</kbd><kbd>A</kbd> for notifications, <kbd>Super</kbd><kbd>D</kbd> for the dashboard, <kbd>Super</kbd><kbd>Tab</kbd> to switch windows, <kbd>Super</kbd><kbd>L</kbd> to lock, and the rest as on Hyprland.

3. Include it at the **end** of `~/.config/niri/config.kdl`:

   ```kdl
   include "material3ui.kdl"
   ```

   Last, so its keys win: niri lets a later bind replace an earlier one. That takes over a few of niri's own default keys (<kbd>Super</kbd><kbd>D</kbd>, <kbd>Super</kbd><kbd>L</kbd>, <kbd>Super</kbd><kbd>I</kbd>, <kbd>Super</kbd><kbd>V</kbd>…); move the ones you want to other keys in your config.

4. Take out of your niri config what the shell now does itself: a bar (`spawn-at-startup "waybar"`), a notification daemon (mako, dunst), a polkit agent and a wallpaper tool.

5. Log out, and choose **niri** at the login screen.

## What works

- **The bar:** each monitor's own workspaces, exactly as many as niri has (it adds one as you fill the last), with the active one lit. A click goes to that monitor's workspace, and scrolling moves up and down. The window title follows the focused window.
- **The dock:** your pinned apps and open windows; a click focuses one, and the menu closes them.
- **The island and hooks:** workspace switches show on the island, and `workspace_changed` and the other hooks fire.
- **Panels:** Quick Settings, notifications, the dashboard, Spotlight, the clipboard, Settings, the power menu: all of them.
- **The overview:** <kbd>Super</kbd><kbd>`</kbd> (or `material3ui msg popup overview`) opens niri's own overview.
- **Switching windows:** <kbd>Super</kbd><kbd>Tab</kbd>, most recently used first.
- **Screenshots and recording:** a region, the screen, or the focused window (captured by niri, then copied or opened in the editor as Settings says); screen recordings with wf-recorder.
- **The lock screen and idle:** locking, the screens going off when you're away, and lock before sleep.
- **Logging out**, restarting and shutting down from the power menu.
- **Digital Wellbeing:** screen time per app, app timers and Focus mode.

## Not on niri yet

These are coming in the next betas. Until then, set them in niri's own `config.kdl`:

- **Settings that change the compositor:** mouse and touchpad, keyboard layouts, the cursor theme and size, displays, keybinds, night light, and how windows look (gaps, borders, corners, blur strength). They still write Hyprland's files.
- **Hyprland Settings** is for Hyprland only.
- **Minimising windows**: niri has no minimised state.
- **Picking a single window to record:** you pick a region instead, since niri's windows scroll.
- **The screen-sharing picker:** apps sharing the screen get niri's own picker, through the GNOME portal.
- **The magnifier and colour filters** (Accessibility).
- **The login screen** runs on Hyprland, so it still needs Hyprland installed.

## Trying it without logging out

The shell can run a second time, for testing, inside a niri started in a window of your current session:

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE niri -c ~/.config/niri/config.kdl &
# niri's log names its sockets: "listening on Wayland socket: wayland-2", "IPC listening on: …"
env -u HYPRLAND_INSTANCE_SIGNATURE WAYLAND_DISPLAY=wayland-2 NIRI_SOCKET=/run/user/1000/niri.wayland-2.<pid>.sock \
    M3_INSTANCE=niri material3ui &
M3_INSTANCE=niri material3ui msg workspaces
```

`M3_INSTANCE` gives that shell its own socket, so `material3ui msg` reaches it only with the same variable (see [IPC](IPC.md)).
