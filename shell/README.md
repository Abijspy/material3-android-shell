# material3ui (native C++ shell)

The C++ rewrite of Material3UI: a Wayland shell drawn directly with EGL/OpenGL ES, with no Qt, QML, GTK or Quickshell underneath. It reads the same `~/.config/material3ui/config.json` and Matugen palette as the QML shell, so the two can be swapped while the port is in progress.

> **Status: phase 1 (foundation).** The status bar runs on every output with workspaces, window title, clock and battery. Everything else still lives in the QML shell; see the roadmap below.

## Why native

| Limitation of the QML shell | How the native shell handles it |
| :--- | :--- |
| System access goes through Quickshell's bindings, or shells out to `hyprctl`, `pacman`, Python | Direct Unix sockets, inotify, sysfs; D-Bus, PipeWire and PAM come next |
| JavaScript inside QML is untyped and hard to refactor | C++20, compiled with warnings on |
| Every Quickshell release can break the shell | Depends only on libwayland, EGL/GLES2, Pango/Cairo and nlohmann-json |

## Architecture

```
src/
  core/      EventLoop (poll + timers + deferred calls), FileWatcher (inotify),
             Config (config.json by JSON pointer), Signal (scoped connections)
  wayland/   Display (registry, outputs, seat, pointer, cursor-shape),
             LayerSurface (layer-shell surface hosting a node tree, frame-paced)
  render/    Renderer (EGL context; SDF rounded-rect and alpha-mask programs),
             Text (Pango layout → Cairo raster → cached GL texture)
  ui/        Node scene graph, Flex layout, Label, Icon (Material Symbols
             ligatures), Button (M3 state layer), Tween/ColorTween (M3 easing),
             Theme (Matugen roles → colours, live)
  services/  Hyprland (request socket + event stream)
  ipc/       IpcServer and `material3ui msg` client
  shell/     Shell (surfaces per output, IPC commands), bar/
```

Rules the code follows:

- **One thread, one loop.** Every source (Wayland, sockets, inotify, signals) is an `EventSource` polled by `EventLoop`. Nothing blocks except tiny local socket requests to Hyprland.
- **Setters never paint.** Widgets call `requestLayout()` or `requestRedraw()`. The surface lays out and paints on the next frame, with at most one frame in flight.
- **Colours are roles, not values.** Widgets store `ColorRef("primary")`, resolved at paint time, so a new wallpaper recolours everything without rebuilding.
- **Callbacks can't outlive widgets.** Service signals hand out a `Connection` that disconnects when its owner is destroyed.

The overall approach (a poll loop, direct Wayland, a GLES scene graph, Pango text) follows the design of [Noctalia](https://github.com/noctalia-dev/noctalia). Material3UI's implementation is written from scratch; no Noctalia code is used.

## Building

Dependencies: `meson`, `wayland`, `wayland-protocols`, `wlr-protocols`, `mesa` (EGL/GLES2), `pango`, `cairo`, `nlohmann-json`.

```bash
meson setup build
meson compile -C build
./build/material3ui                 # run (inside a Wayland session)
./build/material3ui msg help        # talk to the running shell
```

No Arch box handy? `tools/dev.sh` builds in an Arch container, and `tools/dev.sh shot` runs the shell under headless Sway and saves a screenshot. Set `M3_FAKE_HYPR=1` to serve a fake Hyprland (`tools/fake-hyprland.py`), and `M3_SCRIPT` to run commands before the shot.

## IPC

```
material3ui msg ping | version | quit | reload
material3ui msg bar [toggle|show|hide]
material3ui msg outputs | workspaces
```

## Roadmap to parity with the QML shell

| Phase | Scope |
| :--- | :--- |
| **1. Foundation** ✅ | Event loop, Wayland and layer shell, GLES renderer, text, scene graph and flex layout, M3 motion, Matugen theme, config, IPC, Hyprland service, status bar (workspaces, window title, clock, battery) |
| 2. Input and popups | Keyboard (xkbcommon), focus grabs, popups anchored to the bar, scrolling lists, text input, fractional scale (wp-fractional-scale + viewporter) |
| 3. System services | D-Bus (sd-bus), PipeWire volume and per-channel balance, NetworkManager, BlueZ, UPower, MPRIS, brightness, StatusNotifier tray |
| 4. Core panels | Quick Settings, Notification daemon and centre, Spotlight launcher (apps, files, calculator, clipboard, emoji), OSDs, power menu |
| 5. Session | Lock screen (ext-session-lock + PAM), polkit agent, greetd greeter, idle (ext-idle-notify) |
| 6. Rich UI | Dynamic island, dashboard, overview, wallpaper picker and transitions, desktop widgets, system monitor |
| 7. Settings | The Settings app, including Packages & Updates and the keybind editor |
| 8. Switch over | Installer and Hyprland config start `material3ui` instead of Quickshell; the QML shell is retired |
