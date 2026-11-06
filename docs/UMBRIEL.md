# Material3UI on Umbriel

The shell also runs on [Umbriel](https://github.com/noctalia-dev/umbriel), the Noctalia project's Wayland compositor, with scrolling, dwindle, master and stacking layouts. Support is new, like Umbriel itself (0.1). It finds out by itself that it's on Umbriel, the same way it tells Hyprland from niri.

## What works

- **The desktop:** the bar shows each monitor's own workspaces, which Umbriel adds and removes as you use them. The dock, panels, notifications, Spotlight, the clipboard, the window switcher and the lock screen all work, along with the rest of the shell.
- **Windows:** the bar's window title, the dock's running apps, focusing and closing windows from the dock, and screenshots of a window.
- **The overview:** <kbd>Super</kbd><kbd>`</kbd> and the shell's overview button open Umbriel's own overview.
- **Keyboard layouts:** the bar's keyboard widget shows the layout in use and switches to the next one. Each switch shows on screen, and Settings → Keyboard lists your layouts.
- **Blur** behind the bar, dock, panels and notifications, through Umbriel's layer rules (below).
- **Screens off, logging out, reloading the config:** as on the other compositors.

What Umbriel itself sets lives in its config file, which it reloads as you save. Monitors, the mouse and touchpad, keyboard layouts and repeat all go there. Settings → Display, Mouse & touchpad and Keyboard say where, with a button that opens the file. There's no Umbriel Settings app (as Hyprland and niri have).

## Setting it up

The installer doesn't set Umbriel up yet, and Umbriel isn't packaged for Arch. Build and install it as [its guide](https://github.com/noctalia-dev/umbriel/blob/main/docs/user/installation.md) says. It needs `wlroots0.20`. Then:

1. Copy the shell's part next to your config:

   ```bash
   cp dotfiles/.config/umbriel/material3ui.toml ~/.config/umbriel/
   ```

2. In `~/.config/umbriel/config.toml`, include it and start the shell with the session:

   ```toml
   [include.optional]
   files = ["material3ui.toml"]

   [general]
   autostart = ["material3ui"]
   ```

3. Umbriel applies `config.toml` after its includes, so any key bound in both stays as `config.toml` has it. Umbriel's example config binds <kbd>Super</kbd>, the volume and brightness keys and <kbd>Super</kbd><kbd>Escape</kbd> to Noctalia. Remove those lines from `config.toml` to use the shell's.

4. Log out and pick **Umbriel** at the login screen.

## Keys

[`material3ui.toml`](../dotfiles/.config/umbriel/material3ui.toml) binds the shell's keys, as on niri:

- <kbd>Super</kbd><kbd>Space</kbd> Spotlight
- <kbd>Super</kbd><kbd>A</kbd> notifications
- <kbd>Super</kbd><kbd>N</kbd> Quick Settings
- <kbd>Super</kbd><kbd>I</kbd> Settings
- <kbd>Super</kbd><kbd>V</kbd> clipboard
- <kbd>Super</kbd><kbd>Tab</kbd> switch windows
- <kbd>Super</kbd><kbd>`</kbd> overview
- <kbd>Super</kbd><kbd>L</kbd> lock
- <kbd>Super</kbd><kbd>Escape</kbd> power menu
- screenshots, recording and the colour picker
- the media and brightness keys

Umbriel's own keys (moving and resizing windows, workspaces) stay as your `config.toml` has them.

## Blur

Umbriel blurs layer surfaces that a `[[layer_rule]]` names. The shell's file blurs these namespaces:

- `material3ui-popup`, `-toasts`, `-osd`, `-dock` and `-switcher`: the live content behind them, since they open over windows
- `material3ui-bar` and `-frame`: the cheaper cached blur, since they sit over the wallpaper

Fully see-through parts, such as the space around a panel's card, stay clear. Settings → Look & feel doesn't change these rules yet. Edit `material3ui.toml` to change them.

## Not yet

- The installer and updates don't set up or refresh `material3ui.toml`.
- No minimising from the shell. Umbriel minimises only floating windows.
- The shell's screen-sharing picker is Hyprland's. On Umbriel, screen sharing goes through `xdg-desktop-portal-umbriel`.
