project('material3ui', 'cpp', 'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 1.1',
  default_options: ['cpp_std=c++20', 'warning_level=2', 'buildtype=debugoptimized'])

wayland_client = dependency('wayland-client')
wayland_egl = dependency('wayland-egl')
wayland_protocols = dependency('wayland-protocols', version: '>= 1.45')
wlr_protocols = dependency('wlr-protocols')
egl = dependency('egl')
glesv2 = dependency('glesv2')
pangocairo = dependency('pangocairo', version: '>= 1.50')
cairo = dependency('cairo')
json = dependency('nlohmann_json', version: '>= 3.10')
xkbcommon = dependency('xkbcommon')
libsystemd = dependency('libsystemd', version: '>= 246') # sd-bus
libpulse = dependency('libpulse')
librsvg = dependency('librsvg-2.0', version: '>= 2.46')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
polkit_agent = dependency('polkit-agent-1')
pam = meson.get_compiler('cpp').find_library('pam')

# ── Wayland protocols ──
scanner = find_program(dependency('wayland-scanner', native: true).get_variable('wayland_scanner'))
wl_dir = wayland_protocols.get_variable('pkgdatadir')
wlr_dir = wlr_protocols.get_variable('pkgdatadir')

protocols = {
  'xdg-shell': wl_dir / 'stable/xdg-shell/xdg-shell.xml',
  'tablet-v2': wl_dir / 'stable/tablet/tablet-v2.xml',
  'cursor-shape-v1': wl_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
  'fractional-scale-v1': wl_dir / 'staging/fractional-scale/fractional-scale-v1.xml',
  'viewporter': wl_dir / 'stable/viewporter/viewporter.xml',
  'ext-session-lock-v1': wl_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
  'ext-idle-notify-v1': wl_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml',
  'ext-background-effect-v1': wl_dir / 'staging/ext-background-effect/ext-background-effect-v1.xml',
  'wlr-layer-shell-unstable-v1': wlr_dir / 'unstable/wlr-layer-shell-unstable-v1.xml',
}

protocol_sources = []
foreach name, xml : protocols
  protocol_sources += custom_target(name + '-header',
    input: xml, output: name + '-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += custom_target(name + '-code',
    input: xml, output: name + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

conf = configuration_data()
conf.set_quoted('M3_VERSION', meson.project_version())
configure_file(output: 'build_config.h', configuration: conf)

sources = files(
  'src/main.cpp',
  'src/greeter/Greeter.cpp',
  'src/polkit/listener.c',
  'src/core/Config.cpp',
  'src/core/EventLoop.cpp',
  'src/core/FileWatcher.cpp',
  'src/core/Paths.cpp',
  'src/core/Process.cpp',
  'src/dbus/Bus.cpp',
  'src/ipc/IpcServer.cpp',
  'src/render/Images.cpp',
  'src/render/Renderer.cpp',
  'src/render/Text.cpp',
  'src/services/Audio.cpp',
  'src/services/Auth.cpp',
  'src/services/Effects.cpp',
  'src/services/Hyprland.cpp',
  'src/services/Idle.cpp',
  'src/services/Keybinds.cpp',
  'src/services/Mpris.cpp',
  'src/services/Network.cpp',
  'src/services/Notifications.cpp',
  'src/services/Packages.cpp',
  'src/services/Polkit.cpp',
  'src/services/Search.cpp',
  'src/services/Session.cpp',
  'src/services/SystemStats.cpp',
  'src/services/Tray.cpp',
  'src/services/Wallpaper.cpp',
  'src/shell/Desktop.cpp',
  'src/shell/Launcher.cpp',
  'src/shell/LockScreen.cpp',
  'src/shell/LockView.cpp',
  'src/shell/Menus.cpp',
  'src/shell/Notify.cpp',
  'src/shell/Osd.cpp',
  'src/shell/PanelKit.cpp',
  'src/shell/Panels.cpp',
  'src/shell/QuickSettings.cpp',
  'src/shell/Rich.cpp',
  'src/shell/Settings.cpp',
  'src/shell/Popup.cpp',
  'src/shell/Shell.cpp',
  'src/shell/bar/Bar.cpp',
  'src/shell/bar/Island.cpp',
  'src/shell/bar/Status.cpp',
  'src/shell/bar/Widgets.cpp',
  'src/ui/Animation.cpp',
  'src/ui/Controls.cpp',
  'src/ui/Node.cpp',
  'src/ui/Theme.cpp',
  'src/wayland/Display.cpp',
  'src/wayland/LayerSurface.cpp',
  'src/wayland/Surface.cpp',
)

install_data('data/pam/material3ui', install_dir: get_option('sysconfdir') / 'pam.d')
# Shared with the QML shell; absent when only shell/ is checked out
emojis = '../dotfiles/.config/quickshell/material3ui/data/emojis.txt'
if import('fs').exists(emojis)
  install_data(emojis, install_dir: get_option('datadir') / 'material3ui')
endif

executable('material3ui', sources, protocol_sources,
  include_directories: include_directories('src'),
  dependencies: [wayland_client, wayland_egl, egl, glesv2, pangocairo, cairo, json, xkbcommon, libsystemd, libpulse, librsvg, gdk_pixbuf, polkit_agent, pam],
  install: true)
