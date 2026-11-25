project('material3ui', 'cpp', 'c',
  version: '7.2.0',
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
libcrypto = dependency('libcrypto', version: '>= 3.0') # Quick Share's encryption
libpulse = dependency('libpulse')
librsvg = dependency('librsvg-2.0', version: '>= 2.46')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
polkit_agent = dependency('polkit-agent-1')
libmpv = dependency('mpv') # the video player
libvterm = dependency('vterm') # the terminal
# The PDF viewer and editor: built when poppler-glib is there
poppler_glib = dependency('poppler-glib', required: false)
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
  'wlr-gamma-control-unstable-v1': wlr_dir / 'unstable/wlr-gamma-control-unstable-v1.xml',
}

protocol_sources = []
protocol_headers = []
foreach name, xml : protocols
  header = custom_target(name + '-header',
    input: xml, output: name + '-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += header
  protocol_headers += header
  protocol_sources += custom_target(name + '-code',
    input: xml, output: name + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

conf = configuration_data()
conf.set_quoted('M3_VERSION', meson.project_version())
configure_file(output: 'build_config.h', configuration: conf)

sources = files(
  'src/apps/AppHost.cpp',
  'src/greeter/Greeter.cpp',
  'src/quickshare/Ble.cpp',
  'src/quickshare/Crypto.cpp',
  'src/quickshare/Mdns.cpp',
  'src/quickshare/QuickShare.cpp',
  'src/quickshare/Qr.cpp',
  'src/polkit/listener.c',
  'src/core/Config.cpp',
  'src/core/EventLoop.cpp',
  'src/core/FileWatcher.cpp',
  'src/core/Paths.cpp',
  'src/core/Process.cpp',
  'src/core/Version.cpp',
  'src/dbus/Bus.cpp',
  'src/ipc/IpcServer.cpp',
  'src/render/Images.cpp',
  'src/render/Renderer.cpp',
  'src/render/Text.cpp',
  'src/services/Alarms.cpp',
  'src/services/AlarmsParse.cpp',
  'src/services/Audio.cpp',
  'src/services/Backups.cpp',
  'src/services/BatteryAlerts.cpp',
  'src/services/Calendar.cpp',
  'src/services/Auth.cpp',
  'src/services/Clipboard.cpp',
  'src/services/Compositor.cpp',
  'src/services/Effects.cpp',
  'src/services/Idle.cpp',
  'src/services/Kdl.cpp',
  'src/services/TimeZones.cpp',
  'src/services/Sun.cpp',
  'src/services/Units.cpp',
  'src/services/Currency.cpp',
  'src/services/CurrencyParse.cpp',
  'src/services/RainParse.cpp',
  'src/services/Sounds.cpp',
  'src/services/Pomodoro.cpp',
  'src/services/Hotspot.cpp',
  'src/services/SessionApps.cpp',
  'src/services/DiskSpace.cpp',
  'src/services/Equalizer.cpp',
  'src/services/AutoSaver.cpp',
  'src/services/WorldClock.cpp',
  'src/services/NoiseSuppression.cpp',
  'src/services/LockKeys.cpp',
  'src/services/LauncherEntries.cpp',
  'src/services/Privacy.cpp',
  'src/services/Trash.cpp',
  'src/services/DataUsage.cpp',
  'src/services/Notes.cpp',
  'src/services/KdeConnect.cpp',
  'src/services/DarkSchedule.cpp',
  'src/services/HookEvents.cpp',
  'src/services/Hooks.cpp',
  'src/services/Lyrics.cpp',
  'src/services/FileOps.cpp',
  'src/services/LyricsParse.cpp',
  'src/services/Mpris.cpp',
  'src/services/Niri.cpp',
  'src/services/NiriConfig.cpp',
  'src/services/NiriModel.cpp',
  'src/services/NiriSettings.cpp',
  'src/services/Network.cpp',
  'src/services/NightLight.cpp',
  'src/services/Notifications.cpp',
  'src/services/Firewall.cpp',
  'src/services/Firmware.cpp',
  'src/services/FirmwareParse.cpp',
  'src/services/Packages.cpp',
  'src/services/PacmanConf.cpp',
  'src/services/PacmanLog.cpp',
  'src/services/Printers.cpp',
  'src/services/PrintersParse.cpp',
  'src/services/Polkit.cpp',
  'src/services/Repos.cpp',
  'src/services/Search.cpp',
  'src/services/ThemedIcons.cpp',
  'src/services/IconTheme.cpp',
  'src/services/Screenshot.cpp',
  'src/services/Session.cpp',
  'src/services/SystemStats.cpp',
  'src/services/Tray.cpp',
  'src/services/Wallpaper.cpp',
  'src/services/Wellbeing.cpp',
  'src/services/Vpn.cpp',
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
  'src/shell/ColorPicker.cpp',
  'src/shell/NotesPanel.cpp',
  'src/shell/LyricsPanel.cpp',
  'src/shell/Flyouts.cpp',
  'src/shell/MessagesPanel.cpp',
  'src/shell/LiveWallpaper.cpp',
  'src/shell/ImageEditor.cpp',
  'src/shell/Ringer.cpp',
  'src/shell/PanelKit.cpp',
  'src/shell/Panels.cpp',
  'src/shell/QuickSettings.cpp',
  'src/shell/QuickSharePanel.cpp',
  'src/shell/Rich.cpp',
  'src/shell/ClockFaces.cpp',
  'src/shell/ClockPanel.cpp',
  'src/shell/ServiceApps.cpp',
  'src/shell/CurveEditor.cpp',
  'src/shell/DesktopEditor.cpp',
  'src/shell/EventEditor.cpp',
  'src/shell/FilePicker.cpp',
  'src/shell/DesktopItems.cpp',
  'src/shell/MonitorLayout.cpp',
  'src/shell/TimeZoneMap.cpp',
  'src/shell/NiriPages.cpp',
  'src/shell/Settings.cpp',
  'src/shell/SecurityPages.cpp',
  'src/shell/WellbeingPage.cpp',
  'src/shell/PrintersPage.cpp',
  'src/shell/SettingsKit.cpp',
  'src/shell/SoftwarePages.cpp',
  'src/shell/ShellAppsPage.cpp',
  'src/shell/Tools.cpp',
  'src/shell/Switcher.cpp',
  'src/shell/Welcome.cpp',
  'src/shell/WidgetStyles.cpp',
  'src/shell/Tooltip.cpp',
  'src/shell/Popup.cpp',
  'src/shell/Shell.cpp',
  'src/shell/bar/Bar.cpp',
  'src/shell/bar/Frame.cpp',
  'src/shell/bar/Island.cpp',
  'src/shell/bar/Status.cpp',
  'src/shell/bar/Widgets.cpp',
  'src/ui/Animation.cpp',
  'src/ui/Controls.cpp',
  'src/ui/IconTips.cpp',
  'src/ui/TextArea.cpp',
  'src/ui/Node.cpp',
  'src/ui/Theme.cpp',
  'src/wayland/Display.cpp',
  'src/wayland/LayerSurface.cpp',
  'src/wayland/Window.cpp',
  'src/wayland/Surface.cpp',
)

install_data('data/pam/material3ui', install_dir: get_option('sysconfdir') / 'pam.d', install_tag: 'shell')
# The Settings app: a launcher entry and its icon
install_data('data/material3ui-settings.desktop', 'data/material3ui-niri.desktop', 'data/material3ui-clock.desktop', 'data/material3ui-calendar.desktop', 'data/material3ui-weather.desktop', 'data/material3ui-quickshare.desktop', 'data/material3ui-print.desktop', install_dir: get_option('datadir') / 'applications', install_tag: 'shell')
install_data('data/icons/material3ui-settings.svg', 'data/icons/material3ui-niri.svg', 'data/icons/material3ui-quickshare.svg', 'data/icons/material3ui-clock.svg', 'data/icons/material3ui-calendar.svg', 'data/icons/material3ui-weather.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps', install_tag: 'shell')
install_data('data/icons/material3ui-settings.svg', 'data/icons/quickshare-symbol.svg', install_dir: get_option('datadir') / 'material3ui/icons', install_tag: 'shell')
# Shipped data: emoji list, logo, wallpapers
install_data('data/emojis.txt', install_dir: get_option('datadir') / 'material3ui', install_tag: 'shell')
# Default profile pictures (Settings → Users → Change picture)
install_subdir('data/avatars', install_dir: get_option('datadir') / 'material3ui', install_tag: 'shell')
# What's new, the welcome tour and About → Changelog read it
install_data('../CHANGELOG.md', install_dir: get_option('datadir') / 'material3ui', install_tag: 'shell')
# One password per update: the root helper Settings starts, and its client
install_data('data/update/material3ui-update-root', 'data/update/material3ui-root',
  install_dir: get_option('prefix') / 'lib/material3ui', install_mode: 'rwxr-xr-x', install_tag: 'shell')
install_data('data/update/io.github.abijspy.material3ui.update.policy',
  install_dir: get_option('datadir') / 'polkit-1/actions', install_tag: 'shell')
install_data('data/icons/Material3UI.svg', 'data/icons/niri-logo.svg', install_dir: get_option('datadir') / 'material3ui/icons', install_tag: 'shell')
# The world's land, for Settings → System's time zone map
install_data('data/maps/world.svg', install_dir: get_option('datadir') / 'material3ui/maps', install_tag: 'shell')
# The package's icon (no .desktop file: the shell is not an app to launch)
install_data('data/icons/Material3UI.svg', rename: 'material3ui.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps', install_tag: 'shell')
install_data('data/wallpapers/default_wallpaper.png', 'data/wallpapers/midnight-dunes.png',
  'data/wallpapers/bloom-lavender.jpg',
  'data/wallpapers/bloom-rose.jpg',
  'data/wallpapers/pills-mint.jpg',
  'data/wallpapers/pills-sunset.jpg',
  'data/wallpapers/ripples-graphite.jpg',
  'data/wallpapers/ripples-sage.jpg',
  'data/wallpapers/shapes-coral.jpg',
  'data/wallpapers/shapes-ocean.jpg',
  install_dir: get_option('datadir') / 'material3ui/wallpapers', install_tag: 'shell')

# Everything the shell and its apps share: libmaterial3ui, private to them
# (in /usr/lib/material3ui, found through each program's rpath)
shell_deps = [wayland_client, wayland_egl, egl, glesv2, pangocairo, cairo, json, xkbcommon, libsystemd, libpulse, librsvg, gdk_pixbuf, polkit_agent, pam, libcrypto]
private_libdir = get_option('prefix') / get_option('libdir') / 'material3ui'
libm3 = shared_library('material3ui', sources, protocol_sources,
  include_directories: include_directories('src'),
  dependencies: shell_deps,
  install: true, install_dir: private_libdir, install_tag: 'shell')

executable('material3ui', 'src/main.cpp', protocol_headers,
  include_directories: include_directories('src'),
  dependencies: shell_deps, link_with: libm3,
  # Function names in crash.log's backtraces
  link_args: ['-rdynamic'],
  install_rpath: private_libdir,
  install: true, install_tag: 'shell')

# The apps: each a program, a .desktop file and an icon of its own, packaged
# apart (packaging/material3ui-<app>: meson install --tags <app>)
apps = {
  'files': {'sources': ['src/apps/FilesMain.cpp', 'src/shell/FilesApp.cpp'], 'deps': []},
  'viewer': {'sources': ['src/apps/ViewerMain.cpp', 'src/shell/ImageViewer.cpp'], 'deps': []},
  'player': {'sources': ['src/apps/PlayerMain.cpp', 'src/shell/VideoPlayer.cpp'], 'deps': [libmpv]},
  'tasks': {'sources': ['src/apps/TasksMain.cpp', 'src/shell/TaskManager.cpp'], 'deps': []},
  'terminal': {'sources': ['src/apps/TerminalMain.cpp', 'src/shell/Terminal.cpp'], 'deps': [libvterm]},
  'notes': {'sources': ['src/apps/NotesMain.cpp', 'src/shell/NotesApp.cpp'], 'deps': []},
  'store': {'sources': ['src/apps/StoreMain.cpp', 'src/shell/StoreApp.cpp'], 'deps': [gdk_pixbuf]},
  'imagewriter': {'sources': ['src/apps/ImagewriterMain.cpp', 'src/shell/ImageWriterApp.cpp'], 'deps': []},
  'fonts': {'sources': ['src/apps/FontsMain.cpp', 'src/shell/FontsApp.cpp'], 'deps': [dependency('fontconfig'), dependency('pangoft2')]},
  'calculator': {'sources': ['src/apps/CalculatorMain.cpp', 'src/shell/CalculatorApp.cpp'], 'deps': []},
  'recorder': {'sources': ['src/apps/RecorderMain.cpp', 'src/shell/RecorderApp.cpp'], 'deps': []},
  'backups': {'sources': ['src/apps/BackupsMain.cpp', 'src/shell/BackupsApp.cpp'], 'deps': []},
  'camera': {'sources': ['src/apps/CameraMain.cpp', 'src/shell/CameraApp.cpp'], 'deps': [libmpv]},
  'disks': {'sources': ['src/apps/DisksMain.cpp', 'src/shell/DisksApp.cpp'], 'deps': []},
  'text': {'sources': ['src/apps/TextMain.cpp', 'src/shell/TextEditor.cpp'], 'deps': []},
  'music': {'sources': ['src/apps/MusicMain.cpp', 'src/shell/MusicApp.cpp'], 'deps': [libmpv]},
}
if poppler_glib.found()
  apps += {'pdf': {'sources': ['src/apps/PdfMain.cpp', 'src/shell/PdfApp.cpp'], 'deps': [poppler_glib]}}
else
  warning('poppler-glib not found: the PDF viewer (material3ui-pdf) is left out')
endif
foreach app, def : apps
  executable('material3ui-' + app, def['sources'], protocol_headers,
    include_directories: include_directories('src'),
    dependencies: shell_deps + def['deps'], link_with: libm3,
    install_rpath: private_libdir,
    install: true, install_tag: app)
  install_data('data/material3ui-' + app + '.desktop', install_dir: get_option('datadir') / 'applications', install_tag: app)
  install_data('data/icons/material3ui-' + app + '.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps', install_tag: app)
endforeach

# Unit tests: meson test -C <build dir>
test('pacman-log', executable('pacman_log_test', 'tests/pacman_log_test.cpp', 'src/services/PacmanLog.cpp',
  include_directories: include_directories('src')))
test('printers', executable('printers_test', 'tests/printers_test.cpp', 'src/services/PrintersParse.cpp',
  include_directories: include_directories('src')))
test('lyrics', executable('lyrics_test', 'tests/lyrics_test.cpp', 'src/services/LyricsParse.cpp',
  include_directories: include_directories('src'), dependencies: [json]))
test('alarms', executable('alarms_test', 'tests/alarms_test.cpp', 'src/services/AlarmsParse.cpp',
  include_directories: include_directories('src')))
test('pacman-conf', executable('pacman_conf_test', 'tests/pacman_conf_test.cpp', 'src/services/PacmanConf.cpp',
  include_directories: include_directories('src')))
test('firmware', executable('firmware_test', 'tests/firmware_test.cpp', 'src/services/FirmwareParse.cpp',
  include_directories: include_directories('src'), dependencies: [json]))
test('niri', executable('niri_test', 'tests/niri_test.cpp', 'src/services/NiriModel.cpp',
  include_directories: include_directories('src'), dependencies: [json]))
test('version', executable('version_test', 'tests/version_test.cpp', 'src/core/Version.cpp',
  include_directories: include_directories('src')))
test('niri-settings', executable('niri_settings_test', 'tests/niri_settings_test.cpp', 'src/services/NiriSettings.cpp',
  include_directories: include_directories('src')))
test('kdl', executable('kdl_test', 'tests/kdl_test.cpp', 'src/services/Kdl.cpp',
  include_directories: include_directories('src')))
test('timezones', executable('timezones_test', 'tests/timezones_test.cpp', 'src/services/TimeZones.cpp',
  include_directories: include_directories('src')))
test('sun', executable('sun_test', 'tests/sun_test.cpp', 'src/services/Sun.cpp',
  include_directories: include_directories('src')))
test('units', executable('units_test', 'tests/units_test.cpp', 'src/services/Units.cpp',
  include_directories: include_directories('src')))
test('rain', executable('rain_test', 'tests/rain_test.cpp', 'src/services/RainParse.cpp',
  include_directories: include_directories('src')))
test('currency', executable('currency_test', 'tests/currency_test.cpp', 'src/services/CurrencyParse.cpp',
  include_directories: include_directories('src')))
