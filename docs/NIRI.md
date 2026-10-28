# Material3UI on niri (beta)

From 6.0, the shell also runs on [niri](https://github.com/YaLTeR/niri) 26.04 or newer, the scrollable-tiling compositor, as well as on Hyprland. It finds out which one it's on by itself: in a niri session it talks to niri, and in a Hyprland session to Hyprland.

niri support is in **beta**. The desktop works (the bar, the dock, the panels, notifications, the lock screen, the launcher and the rest), and so do the settings that change niri itself: the touchpad and mouse, keyboard layouts, monitors, the cursor and night light (see [What works](#what-works)). From 6.0-beta-3 the installer sets niri up, and the login screen runs on it when Hyprland isn't installed.

## Setting it up

Run the installer and pick **niri** (or **both**, to choose at each login) when it asks for the compositor:

```bash
./install.sh --compositor niri      # or: --compositor both
```

It installs niri and its companions (xwayland-satellite runs X11 apps, and the GNOME portal handles screen sharing), then sets up `~/.config/niri`:

- **`material3ui.kdl`**, the shell's part: it starts the shell, puts its wallpaper behind niri's overview, and binds its keys (see [Keys](#keys)). Your terminal and browser go in it too, as `$TERMINAL` and `$BROWSER`. Updates replace this file.
- **`config.kdl`**, yours: kept as it is, with `include "material3ui.kdl"` added at its end. Without one, it starts as niri's own default config, minus the waybar it would start. The installer warns you if your config starts something the shell replaces: a bar (waybar), a notification daemon (mako, dunst), a polkit agent, a wallpaper or idle tool. Take those lines out.
- **`material3ui-user.kdl`**, yours: included last, so what you put in it wins over the shell's keys. Updates never touch it.
- **`material3ui-settings.kdl`**, the shell's: what you set in its Settings (touchpad, keyboard, monitors, cursor), written as you change it.

The old files are backed up, and `./install.sh --restore` puts them back. Pick `--niri skip` to leave niri's config alone and include the file yourself (below). Then log out, and choose **niri** at the login screen.

### By hand

With Material3UI already installed (on Hyprland, say), you can add niri without the installer:

1. Install niri and its companions:

   ```bash
   sudo pacman -S niri xwayland-satellite xdg-desktop-portal-gnome
   ```

2. Copy [`material3ui.kdl`](../dotfiles/.config/niri/material3ui.kdl) next to your niri config, and put your terminal and browser in its `environment` block and app keys:

   ```bash
   cp dotfiles/.config/niri/material3ui.kdl ~/.config/niri/
   ```

3. Include it at the **end** of `~/.config/niri/config.kdl`, last so its keys win (niri lets a later bind replace an earlier one):

   ```kdl
   include "material3ui.kdl"
   ```

4. Take out of `config.kdl` what the shell now does itself: the bar, notification daemon, polkit agent and wallpaper tool.

## Keys

`material3ui.kdl` binds <kbd>Super</kbd><kbd>Space</kbd> for Spotlight (niri can't bind <kbd>Super</kbd> on its own), <kbd>Super</kbd><kbd>N</kbd> for Quick Settings, <kbd>Super</kbd><kbd>A</kbd> for notifications, <kbd>Super</kbd><kbd>D</kbd> for the dashboard, <kbd>Super</kbd><kbd>I</kbd> for Settings, <kbd>Super</kbd><kbd>Tab</kbd> to switch windows, <kbd>Super</kbd><kbd>L</kbd> to lock, <kbd>Super</kbd><kbd>Return</kbd> (or <kbd>Super</kbd><kbd>T</kbd>) for your terminal, <kbd>Super</kbd><kbd>E</kbd> for files and <kbd>Super</kbd><kbd>B</kbd> for the browser, and the rest as on Hyprland. That takes over a few of niri's own default keys (<kbd>Super</kbd><kbd>D</kbd>, <kbd>Super</kbd><kbd>T</kbd>, <kbd>Super</kbd><kbd>V</kbd>…). To keep one, bind it again in `material3ui-user.kdl`, or add your own shortcuts in Settings → Keyboard.

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
- **The login screen**, from 6.0-beta-3: without Hyprland installed, it runs in a minimal niri session, with your keyboard layout, cursor and monitors synced from your niri config. With both installed it stays on Hyprland. → [Login screen](GREETER.md)

### Settings that change niri

From 6.0-beta-2, these Settings pages work on niri. What you set is written to `~/.config/niri/material3ui-settings.kdl`, and niri picks it up at once:

- **Mouse & touchpad:** tap to click, natural scrolling (the touchpad's and a mouse's), ignore while typing, click with fingers or the pad's corners, tap and drag, drag lock, scrolling speed, pointer speed and acceleration.
- **Keyboard:** your layouts (with **Next layout** to switch), the key that switches them, repeat delay and rate, and your own shortcuts: a key combination and the command it runs.
- **Display:** each monitor's resolution, scale, rotation, variable refresh rate, and on or off. They're applied at once and kept for the next login. If niri's own `config.kdl` sets a monitor, niri's block wins when it reloads; remove that block to keep the shell's.
- **Night light:** the shell warms the screens itself, through the compositor's gamma, with nothing else to install.
- **The cursor:** its theme (Bibata by default) and size, for niri and the apps it starts.
- **The focus ring:** drawn in the wallpaper's colours.

If niri can't read the shell's settings (`material3ui.kdl` isn't included, or niri refused the file), those pages say so at the top.

### Niri Settings

On niri, the window that is Hyprland Settings on Hyprland is **Niri Settings**: in the launcher, from the card at the top of Settings' sidebar, or with `material3ui msg nirisettings`. Its changes apply as you make them:

- **Windows & layout:** gaps, centring the focused column (never, when it won't fit, always), centring a lone column, how wide new windows open, and space kept free at each edge.
- **Borders & shadows:** the focus ring and the border (on or off, their widths, in the wallpaper's colours), shadows (softness, spread, drop) and window corners.
- **Animations:** on or off, and their speed.
- **Behaviour:** focus following the mouse, the pointer following the focus, workspace back and forth, asking apps not to draw their own title bars, the hot corner, the overview's size, and niri's list of keys at login.

Each option starts from what your `config.kdl` says, or niri's default. Only what you change here is written, to `material3ui-settings.kdl`, so the rest of your config stays in charge: niri merges these sections field by field. **Back to config.kdl**, at the foot of each page, forgets that page's changes. niri can't switch a few options off once your config switches them on (focus following the mouse, the pointer following the focus) or back on once it switches them off (the hot corner); for those, the page says so.

## Not on niri yet

These are coming in the next betas. Until then, set them in niri's own `config.kdl`:

- **Window rules, each animation's curve, and blur.** Hyprland Settings has them on Hyprland. niri has no blur.
- **Arranging monitors** side by side (their positions): set `position` in niri's config.
- **Minimising windows**: niri has no minimised state.
- **Picking a single window to record:** you pick a region instead, since niri's windows scroll.
- **The screen-sharing picker:** apps sharing the screen get niri's own picker, through the GNOME portal.
- **The magnifier and colour filters** (Accessibility).

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
