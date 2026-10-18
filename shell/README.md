# material3ui (native C++ shell)

The C++ rewrite of Material3UI: a Wayland shell drawn directly with EGL/OpenGL ES, with no Qt, QML, GTK or Quickshell underneath. It reads the same `~/.config/material3ui/config.json` and Matugen palette as the QML shell, so the two can be swapped while the port is in progress.

> **Status: phases 1–3 done.** The status bar runs on every output: workspaces, window title, now playing, clock, tray, network/Bluetooth/volume icons and battery. Sound, Wi-Fi and Bluetooth popups and a run prompt work with mouse and keyboard. The bigger panels (Quick Settings, notifications, Spotlight, lock screen…) still live in the QML shell; see the roadmap below.

## Why native

| Limitation of the QML shell | How the native shell handles it |
| :--- | :--- |
| System access goes through Quickshell's bindings, or shells out to `hyprctl`, `pacman`, Python | Direct Unix sockets, inotify, sysfs, D-Bus (sd-bus) and PipeWire (libpulse); PAM comes next |
| JavaScript inside QML is untyped and hard to refactor | C++20, compiled with warnings on |
| Every Quickshell release can break the shell | Depends only on libwayland, EGL/GLES2, Pango/Cairo, xkbcommon, libsystemd, libpulse, librsvg and nlohmann-json |

## Architecture

```
src/
  core/      EventLoop (poll, timers, deferred and cross-thread calls), FileWatcher
             (inotify), Config (config.json by JSON pointer), Signal (scoped
             connections), Process (detached spawn)
  wayland/   Display (registry, outputs, seat, pointer, keyboard with xkbcommon
             and key repeat, cursor-shape), LayerSurface (frame-paced layer
             surface, fractional scale via wp-fractional-scale + viewporter,
             keyboard focus routing)
  render/    Renderer (EGL; SDF rounded-rect, alpha-mask and image programs,
             clip/opacity/translate stacks), Text (Pango → Cairo → GL texture),
             Images (icon-theme lookup, PNG, SVG via librsvg, ARGB pixmaps)
  ui/        Node scene graph, Flex, Label, Icon (Material Symbols), Button,
             Slider (plain and centred), Switch, ScrollView, TextField,
             Tween/ColorTween (M3 easing), Theme (Matugen roles, live)
  dbus/      Bus: sd-bus on the event loop, async calls, signals, properties,
             D-Bus values as JSON
  services/  Hyprland, Audio (libpulse: volume, balance, per channel, devices),
             Network (NetworkManager), Bluetooth (BlueZ), Power (UPower, sysfs
             fallback), Brightness (sysfs + logind), Mpris, Tray
             (StatusNotifierWatcher/host)
  ipc/       IpcServer and `material3ui msg` client
  shell/     Shell (surfaces per output, popups, IPC), Popup, Panels, bar/
```

Rules the code follows:

- **One thread, one loop.** Every source (Wayland, sockets, inotify, signals) is an `EventSource` polled by `EventLoop`. Nothing blocks except tiny local socket requests to Hyprland.
- **Setters never paint.** Widgets call `requestLayout()` or `requestRedraw()`. The surface lays out and paints on the next frame, with at most one frame in flight.
- **Colours are roles, not values.** Widgets store `ColorRef("primary")`, resolved at paint time, so a new wallpaper recolours everything without rebuilding.
- **Callbacks can't outlive widgets.** Service signals hand out a `Connection` that disconnects when its owner is destroyed.

The overall approach (a poll loop, direct Wayland, a GLES scene graph, Pango text) follows the design of [Noctalia](https://github.com/noctalia-dev/noctalia). Material3UI's implementation is written from scratch; no Noctalia code is used.

## Building

Dependencies: `meson`, `wayland`, `wayland-protocols`, `wlr-protocols`, `mesa` (EGL/GLES2), `pango`, `cairo`, `libxkbcommon`, `systemd-libs` (sd-bus), `libpulse` (talks to `pipewire-pulse`), `librsvg`, `nlohmann-json`.

```bash
meson setup build
meson compile -C build
./build/material3ui                 # run (inside a Wayland session)
./build/material3ui msg help        # talk to the running shell
```

No Arch box handy? `tools/dev.sh` builds in an Arch container, and `tools/dev.sh shot` runs the shell under headless Sway and saves a screenshot. Options (environment variables):

| Variable | Effect |
| :--- | :--- |
| `M3_FAKE_HYPR=1` | Serve a fake Hyprland (`tools/fake-hyprland.py`) |
| `M3_SERVICES=1` | Start a session bus, a stand-in system bus, PipeWire with two stereo null sinks, and mock NetworkManager, UPower, BlueZ, an MPRIS player and tray items (`tools/mock-services.py`) |
| `M3_SCRIPT="…"` | Commands to run before the screenshot (`material3ui msg …`, `wtype` for typing, extra `grim` shots) |
| `M3_SCALE=1.5` | Output scale, to check fractional scaling |
| `M3_DEBUG_FRAMES=1`, `M3_DEBUG_FOCUS=1` | Log frames, or keyboard focus and key events |

## IPC

```
material3ui msg ping | version | quit | reload | status
material3ui msg bar [toggle|show|hide]
material3ui msg popup audio|network|bluetooth|run
material3ui msg volume up|down|mute|mic-mute|set <0-150>|balance <-100..100>
material3ui msg brightness up|down|set <0-100>
material3ui msg media play-pause|next|previous|status
material3ui msg tray | outputs | workspaces
```

## Roadmap to parity with the QML shell

| Phase | Scope |
| :--- | :--- |
| **1. Foundation** ✅ | Event loop, Wayland and layer shell, GLES renderer, text, scene graph and flex layout, M3 motion, Matugen theme, config, IPC, Hyprland service, status bar (workspaces, window title, clock, battery) |
| **2. Input and popups** ✅ | Keyboard (xkbcommon, repeat), popups anchored to the bar (Esc or click outside closes, M3 enter/exit motion), slider, switch, scroll view, text field, fractional scale |
| **3. System services** ✅ | D-Bus (sd-bus), audio volume/balance/per-channel/devices (libpulse on PipeWire), NetworkManager, BlueZ, UPower, MPRIS, brightness, StatusNotifier tray; bar widgets and Sound/Wi-Fi/Bluetooth/Run popups |
| 4. Core panels | Quick Settings, Notification daemon and centre, Spotlight launcher (apps, files, calculator, clipboard, emoji), OSDs, power menu, tray menus (DBusMenu), Wi-Fi network list |
| 5. Session | Lock screen (ext-session-lock + PAM), polkit agent, greetd greeter, idle (ext-idle-notify) |
| 6. Rich UI | Dynamic island, dashboard, overview, wallpaper picker and transitions, desktop widgets, system monitor |
| 7. Settings | The Settings app, including Packages & Updates and the keybind editor |
| 8. Switch over | Installer and Hyprland config start `material3ui` instead of Quickshell; the QML shell is retired |
