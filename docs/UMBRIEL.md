# Material3UI on Umbriel

The shell also runs on [Umbriel](https://github.com/noctalia-dev/umbriel), the Noctalia project's Wayland compositor, with scrolling, dwindle, master and stacking layouts. Support is new, like Umbriel itself (0.1). It finds out by itself that it's on Umbriel, the same way it tells Hyprland from niri.

## What works

- **The desktop:** the bar shows each monitor's own workspaces, which Umbriel adds and removes as you use them. The dock, panels, notifications, Spotlight, the clipboard, the window switcher and the lock screen all work, along with the rest of the shell.
- **Windows:** the bar's window title, the dock's running apps, focusing and closing windows from the dock, and screenshots of a window.
- **The overview:** <kbd>Super</kbd><kbd>`</kbd> and the shell's overview button open Umbriel's own overview.
- **Keyboard layouts:** the bar's keyboard widget shows the layout in use and switches to the next one. Each switch shows on screen, and Settings → Keyboard lists your layouts.
- **Blur** behind the bar, dock, panels and notifications, through Umbriel's layer rules (below).
- **Screens off, logging out, reloading the config:** as on the other compositors.

- **Umbriel Settings:** an app for Umbriel's own options, as Hyprland and niri have theirs (below).

## Umbriel Settings

Open it from the launcher, the dock (the pin that is Hyprland Settings on Hyprland and Niri Settings on niri), the card at the top of Settings' sidebar, the desktop's right-click menu, or `material3ui msg umbrielsettings`. Its pages:

- **Windows & layout:** the layout (scrolling, dwindle, master or floating), gaps, the scrolling strip's centring, the master window's place and width, and workspaces going back and forth.
- **Borders & effects:** borders, corners, Umbriel's title bars, how solid a window stays while dragged, blur (passes, radius, noise, saturation, brightness, contrast) and shadows.
- **Animations:** all of them, or each one: windows opening (pop in, zoom, slide, fade), closing, moving and resizing, dialogs, workspaces (slide or reveal) and the overview.
- **Input devices:** keyboard layouts, variants and options, key repeat, the touchpad (tap to click, natural scrolling, off while typing, speed, right click, scrolling), the mouse and the pointer.
- **Behaviour:** apps bringing themselves forward, dialogs, maximising, the overview's size and blur, the keys cheatsheet and X11 apps.
- **Startup apps:** what Umbriel starts at login, with the shell itself a tap away if it isn't there.

Each option shows what's in effect: your `config.toml`'s value, else what it includes (`from material3ui.toml`), else Umbriel's default. A change goes into `config.toml` itself, since Umbriel applies it after its includes, and Umbriel applies it as the file is saved. The rest of the file stays as you wrote it: comments, order and spacing. A changed option gets a ↺ button that takes it out of `config.toml` again.

The first change in a session copies `config.toml` to `~/.config/material3ui/backups` first. Without a `config.toml` of yours, the first change starts it from Umbriel's packaged one, which is how Umbriel's guide says to begin. After each change, and whenever the file changes, the app runs `umbriel config validate` and shows anything it reports at the top of the page. If the file can't be read, nothing in the app changes it until it's fixed.

Monitors are still set in `config.toml` (Settings → Display says where). So are keys and window rules, for now.

## Setting it up

Run the installer and pick **umbriel**, or several compositors at once:

```bash
./install.sh --compositor umbriel        # or: niri,umbriel · all
```

Umbriel isn't in Arch's repositories or the AUR yet. So the installer builds it, with its portal for screen sharing, screenshots and input capture, from the PKGBUILDs in [`packaging/umbriel`](../packaging/umbriel) and [`packaging/xdg-desktop-portal-umbriel`](../packaging/xdg-desktop-portal-umbriel). That takes a few minutes. Both are pinned to the commits the shell was tested with, because Umbriel changes as it's developed. They install as the pacman packages `umbriel-git` and `xdg-desktop-portal-umbriel-git`, which you can remove like any other. Running the installer again rebuilds them only when the pinned commit moves on.

It then sets up `~/.config/umbriel`:

- **`material3ui.toml`**, the shell's part: its keys (with your terminal, file manager and browser) and its blur. Updates replace this file.
- **`config.toml`**, yours. Without one, it starts as Umbriel's packaged config. The shell's own TOML editor (`material3ui toml`) makes three changes, keeping the rest of the file as you wrote it:
  - It includes `material3ui.toml` (under `[include.optional]`).
  - It adds `material3ui` to `general.autostart`, so the shell starts with Umbriel.
  - It takes out the keys that would hide the shell's, because Umbriel applies `config.toml` after its includes. Only two kinds go: keys that start Noctalia, and Umbriel's stock keys you haven't changed (from its example config: <kbd>Super</kbd><kbd>Space</kbd>, <kbd>Super</kbd><kbd>Tab</kbd>, <kbd>Super</kbd><kbd>L</kbd>, <kbd>Super</kbd><kbd>Escape</kbd>…). Keys you set yourself stay, and the installer lists them.

`umbriel config validate` then checks the result. The old files are backed up, and `./install.sh --restore` puts them back. If your `config.toml` also starts another shell (Noctalia, waybar, mako…), the installer warns you. Pick `--umbriel skip` to leave Umbriel's config alone and set it up by hand (below). Then log out, and choose **Umbriel** at the login screen.

### By hand

With Umbriel installed your own way:

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

3. Any key bound in both files stays as `config.toml` has it. Remove the ones you'd rather the shell had: Umbriel's example config binds <kbd>Super</kbd>, the volume and brightness keys, and more.

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

## When Umbriel changes

Umbriel is young and changes as it's developed (its docs call its defaults "opinions, not stability promises"). The shell is built to keep working when it does:

- **Umbriel Settings shows only what your Umbriel has.** It asks the installed Umbriel which options it knows (`umbriel config schema`), with their ranges, choices and defaults, and hides the rest. An option that's renamed or dropped leaves the page instead of being written wrong.
- **Nothing is saved that your Umbriel would refuse.** Each change goes to a copy beside `config.toml` first, and `umbriel config validate` checks it. If the check finds an error, or Umbriel says it would ignore that setting, `config.toml` stays as it was and the page says why.
- **The bar and dock keep up.** If Umbriel stops sending one of the event families the shell subscribes to, the shell subscribes without the keyboard layout. If it refuses the rest too, the shell asks for the state every 1.5 seconds instead. Fields Umbriel stops sending read as empty, new ones are ignored, and a refused action is only logged.

What can still break is anything Umbriel renames in its IPC (its commands, actions or fields). The Umbriel features would then stop working until the shell catches up, but the shell itself keeps running. This was tested with Umbriel 0.1.0.

## Not yet

- Umbriel Settings has no pages for monitors, keys and window rules yet.
- No minimising from the shell. Umbriel minimises only floating windows.
- The shell's screen-sharing picker is Hyprland's. On Umbriel, screen sharing goes through `xdg-desktop-portal-umbriel`.
