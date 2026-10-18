# material3ui (native C++ shell)

Material3UI's shell: a Wayland shell drawn directly with EGL/OpenGL ES, with no Qt, QML, GTK or Quickshell underneath. It reads `~/.config/material3ui/config.json` and the Matugen palette.

> **Status: the shell Material3UI ships (v4.0.0 onwards).** It replaced the Quickshell (QML) shell: the installer builds it, Hyprland starts it, and `material3ui msg` is the IPC.

## Why native

| Limitation of the old QML shell | How the native shell handles it |
| :--- | :--- |
| System access goes through Quickshell's bindings, or shells out to `hyprctl`, `pacman`, Python | Direct Unix sockets, inotify, sysfs, D-Bus (sd-bus), PipeWire (libpulse) and PAM |
| JavaScript inside QML is untyped and hard to refactor | C++20, compiled with warnings on |
| Every Quickshell release can break the shell | Depends only on libwayland, EGL/GLES2, Pango/Cairo, xkbcommon, libsystemd, libpulse, librsvg, gdk-pixbuf, PAM, polkit-agent and nlohmann-json |

## Architecture

```
src/
  core/      EventLoop (poll, timers, deferred and cross-thread calls), FileWatcher
             (inotify), Config (config.json by JSON pointer), Signal (scoped
             connections), Process (detached spawn)
  wayland/   Display (registry, outputs, seat, pointer, keyboard with xkbcommon
             and key repeat, cursor-shape, session-lock and idle globals),
             Surface (frame-paced rendering, fractional scale, input regions,
             focus) with LayerSurface and LockSurface roles
  render/    Renderer (EGL; SDF rounded-rect, alpha-mask and image programs,
             clip/opacity/translate stacks), Text (Pango → Cairo → GL texture),
             Images (icon-theme lookup, PNG, SVG via librsvg, ARGB pixmaps)
  ui/        Node scene graph, Flex, Label, Icon (Material Symbols), Button,
             Slider (plain and centred), Switch, ScrollView, TextField,
             Tween/ColorTween (M3 easing), Theme (Matugen roles, live)
  dbus/      Bus: sd-bus on the event loop, async calls, signals, properties,
             D-Bus values as JSON
  services/  Hyprland, Audio (libpulse), Network (NetworkManager, Wi-Fi list and
             connect), Bluetooth, Power (UPower), Brightness, Mpris, Tray (SNI +
             DBusMenu), Notifications (daemon), Search (apps, maths, emoji,
             cliphist, fd), Session (logind), Idle (ext-idle-notify), Auth (PAM),
             Polkit (agent on a GLib thread), Wallpaper, SystemStats
  ipc/       IpcServer and `material3ui msg` client
  shell/     Shell (surfaces per output, popups, IPC), Popup and the panels
             (QuickSettings, Launcher, Menus, Rich), Notify (toasts), Osd,
             LockScreen + LockView, Desktop (wallpaper, desktop clock), bar/
             (incl. the dynamic Island)
  greeter/   `material3ui greeter` for greetd
  polkit/    the PolkitAgentListener GObject (C)
```

Rules the code follows:

- **One thread, one loop.** Every source (Wayland, sockets, inotify, signals) is an `EventSource` polled by `EventLoop`. Nothing blocks except tiny local socket requests to Hyprland.
- **Setters never paint.** Widgets call `requestLayout()` or `requestRedraw()`. The surface lays out and paints on the next frame, with at most one frame in flight.
- **Colours are roles, not values.** Widgets store `ColorRef("primary")`, resolved at paint time, so a new wallpaper recolours everything without rebuilding.
- **Callbacks can't outlive widgets.** Service signals hand out a `Connection` that disconnects when its owner is destroyed.

The overall approach (a poll loop, direct Wayland, a GLES scene graph, Pango text) follows the design of [Noctalia](https://github.com/noctalia-dev/noctalia). Material3UI's implementation is written from scratch; no Noctalia code is used.

## Building

Dependencies: `meson`, `wayland`, `wayland-protocols`, `wlr-protocols`, `mesa` (EGL/GLES2), `pango`, `cairo`, `libxkbcommon`, `systemd-libs` (sd-bus), `libpulse` (talks to `pipewire-pulse`), `librsvg`, `gdk-pixbuf2`, `pam`, `polkit`, `nlohmann-json`. At runtime, optionally: `matugen` (palette), `cliphist` + `wl-clipboard` (clipboard history), `fd` (file search), `hyprsunset` (night light).

`meson install` also installs `/etc/pam.d/material3ui` (the lock screen's PAM service; it includes `login`).

```bash
meson setup build
meson compile -C build
./build/material3ui                 # run (inside a Wayland session)
./build/material3ui msg help        # talk to the running shell
```

No Arch box handy? `tools/dev.sh` builds in an Arch container, and `tools/dev.sh shot` runs the shell under headless Sway and saves a screenshot. Options (environment variables):

| Variable | Effect |
| :--- | :--- |
| `M3_FAKE_HYPR=1` | Serve a fake Hyprland with workspaces and windows (`tools/fake-hyprland.py`) |
| `M3_SERVICES=1` | Start a session bus, a stand-in system bus, PipeWire with two stereo null sinks, and mock NetworkManager, UPower, BlueZ, an MPRIS player and tray items (`tools/mock-services.py`) |
| `M3_SCRIPT="…"` | Commands to run before the screenshot (`material3ui msg …`, `wtype` for typing, extra `grim` shots) |
| `M3_SCALE=1.5` | Output scale, to check fractional scaling |
| `M3_SETUP="…"` | Commands run before the shell starts (users, config, `tools/make-wallpapers.py`, `tools/fake-greetd.py`) |
| `M3_ARGS=greeter` | Arguments for `material3ui` (test the greeter) |
| `M3_USER=alice` | Run the shell as that (existing) user instead of root |

Pointer tests drive a `wlr-virtual-pointer` device (`build/vpointer move X Y click left …`, built on first use) and find elements with `tools/ui.py find TEXT` / `ui.py at TYPE`, which read `material3ui msg debug`. Ready-made suites:

```bash
# Hover, clicks, drags, Wi-Fi taps, tray menus (mock services)
M3_SERVICES=1 M3_FAKE_HYPR=1 M3_WAIT=4 M3_SCRIPT='bash tools/test-mouse.sh' tools/dev.sh shot
# Settings: keybind remap, a config chip, live AUR search
M3_SERVICES=1 M3_FAKE_HYPR=1 M3_WAIT=4 M3_SCRIPT='bash tools/test-settings.sh' tools/dev.sh shot
# polkit end to end: real polkitd authenticates "alice" through the shell's agent
M3_USER=alice M3_SETUP='source tools/polkit-setup.sh' M3_WAIT=3 M3_SCRIPT='bash tools/test-polkit.sh' tools/dev.sh shot
```
| `M3_DEBUG_FRAMES=1`, `M3_DEBUG_FOCUS=1` | Log frames, or keyboard focus and key events |

## IPC

```
material3ui msg ping | version | quit | reload | status
material3ui msg bar [toggle|show|hide]
material3ui msg popup quicksettings [wifi] | notifications | audio | network | bluetooth
                      | dashboard | sysmon | overview | power | run
material3ui msg launcher [text]       # "=" maths, ":" emoji, "#" clipboard, "@" files, ">" run
material3ui msg lock | idle [inhibit|allow]
material3ui msg volume up|down|mute|mic-mute|set <0-150>|balance <-100..100>
material3ui msg brightness up|down|set <0-100>
material3ui msg media play-pause|next|previous|status
material3ui msg notifications [list|clear|dnd on|dnd off]
material3ui msg wallpaper [next|prev|set <path>|pick]
material3ui msg settings [network|bluetooth|sound|display|appearance|style|bar|dock|tools|weather
                         |notifications|lockscreen|lock|keybinds|packages|about]
material3ui msg clipboard             # clipboard history (cliphist)
material3ui msg screenshot [region|window|screen [delay s]|ocr]   # no argument: the tool
material3ui msg record [region|screen]   # start, or stop, a screen recording
material3ui msg tray | outputs | workspaces

material3ui greeter                   # the greetd login screen
```

## How it got here

| Phase | Scope |
| :--- | :--- |
| **1. Foundation** ✅ | Event loop, Wayland and layer shell, GLES renderer, text, scene graph and flex layout, M3 motion, Matugen theme, config, IPC, Hyprland service, status bar (workspaces, window title, clock, battery) |
| **2. Input and popups** ✅ | Keyboard (xkbcommon, repeat), popups anchored to the bar (Esc or click outside closes, M3 enter/exit motion), slider, switch, scroll view, text field, fractional scale |
| **3. System services** ✅ | D-Bus (sd-bus), audio volume/balance/per-channel/devices (libpulse on PipeWire), NetworkManager, BlueZ, UPower, MPRIS, brightness, StatusNotifier tray; bar widgets and Sound/Wi-Fi/Bluetooth/Run popups |
| **4. Core panels** ✅ | Quick Settings with a Wi-Fi picker, notification daemon/toasts/centre, Spotlight (apps, maths, emoji, clipboard, files, run), OSDs, power menu, tray menus (DBusMenu) |
| **5. Session** ✅ | Lock screen (ext-session-lock + PAM), polkit agent, greetd greeter, idle (lock, DPMS, Caffeine), lock before sleep, loginctl lock-session |
| **6. Rich UI** ✅ | Dynamic island, dashboard, overview, wallpapers with transitions and a picker (Matugen), desktop clock, system monitor |
| **7. Settings** ✅ | The Settings app: Network, Bluetooth (pair, connect, forget, discovery), Sound, Display (brightness, night light, monitors), Appearance (wallpaper, Matugen scheme, dark mode, bar, fonts, clock), Notifications, Lock & idle, Keybinds (same files as the QML editor), Packages & Updates (updates and search/install for official, AUR, Flatpak), About |
| **7½. Look and tools** ✅ | Blur (Hyprland layer rules, ext-background-effect elsewhere), bar/panel opacity, roundness, animation speed, accent seed; M3 Expressive Settings (segmented lists, connected button groups, shape morph, scrollbars, close button, app icon); bar layout editor with new modules; dock; clipboard history; screenshot tool; Wi-Fi details and live speeds |
| **8. Switch over** ✅ | The installer builds and installs `material3ui`, Hyprland starts it, binds use `material3ui msg`, the greeter runs `material3ui greeter`, and the QML shell is gone (v4.0.0) |
