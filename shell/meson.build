project('material3ui', 'cpp', 'c',
  version: '5.0.1',
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
  'src/services/Clipboard.cpp',
  'src/services/Effects.cpp',
  'src/services/Hyprland.cpp',
  'src/services/HyprOptions.cpp',
  'src/services/Idle.cpp',
  'src/services/Keybinds.cpp',
  'src/services/Mpris.cpp',
  'src/services/Network.cpp',
  'src/services/Notifications.cpp',
  'src/services/Packages.cpp',
  'src/services/Polkit.cpp',
  'src/services/Search.cpp',
  'src/services/Screenshot.cpp',
  'src/services/Session.cpp',
  'src/services/SystemStats.cpp',
  'src/services/Tray.cpp',
  'src/services/Wallpaper.cpp',
  'src/services/Weather.cpp',
  'src/shell/Desktop.cpp',
  'src/shell/Dock.cpp',
  'src/shell/Launcher.cpp',
  'src/shell/LockEditor.cpp',
  'src/shell/LockLayout.cpp',
  'src/shell/LockScreen.cpp',
  'src/shell/LockView.cpp',
  'src/shell/Menus.cpp',
  'src/shell/Notify.cpp',
  'src/shell/Osd.cpp',
  'src/shell/PanelKit.cpp',
  'src/shell/Panels.cpp',
  'src/shell/QuickSettings.cpp',
  'src/shell/Rich.cpp',
  'src/shell/CurveEditor.cpp',
  'src/shell/HyprPages.cpp',
  'src/shell/KeyCapture.cpp',
  'src/shell/MonitorLayout.cpp',
  'src/shell/Settings.cpp',
  'src/shell/SettingsKit.cpp',
  'src/shell/Tools.cpp',
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
  'src/wayland/Window.cpp',
  'src/wayland/Surface.cpp',
)

install_data('data/pam/material3ui', install_dir: get_option('sysconfdir') / 'pam.d')
# The Settings app: a launcher entry and its icon
install_data('data/material3ui-settings.desktop', 'data/material3ui-hyprland.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/icons/material3ui-settings.svg', 'data/icons/material3ui-hyprland.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
install_data('data/icons/material3ui-settings.svg', install_dir: get_option('datadir') / 'material3ui/icons')
# Shipped data: emoji list, logo, wallpapers
install_data('data/emojis.txt', install_dir: get_option('datadir') / 'material3ui')
install_data('data/icons/Material3UI.svg', install_dir: get_option('datadir') / 'material3ui/icons')
# The package's icon (no .desktop file: the shell is not an app to launch)
install_data('data/icons/Material3UI.svg', rename: 'material3ui.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
install_data('data/wallpapers/default_wallpaper.png', 'data/wallpapers/midnight-dunes.png',
  'data/wallpapers/bloom-lavender.jpg',
  'data/wallpapers/bloom-rose.jpg',
  'data/wallpapers/pills-mint.jpg',
  'data/wallpapers/pills-sunset.jpg',
  'data/wallpapers/ripples-graphite.jpg',
  'data/wallpapers/ripples-sage.jpg',
  'data/wallpapers/shapes-coral.jpg',
  'data/wallpapers/shapes-ocean.jpg',
  install_dir: get_option('datadir') / 'material3ui/wallpapers')

executable('material3ui', sources, protocol_sources,
  include_directories: include_directories('src'),
  dependencies: [wayland_client, wayland_egl, egl, glesv2, pangocairo, cairo, json, xkbcommon, libsystemd, libpulse, librsvg, gdk_pixbuf, polkit_agent, pam],
  # Function names in crash.log's backtraces
  link_args: ['-rdynamic'],
  install: true)
