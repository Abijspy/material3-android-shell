project('material3ui', 'cpp', 'c',
  version: '0.1.0',
  license: 'AGPL-3.0-or-later',
  meson_version: '>= 1.1',
  default_options: ['cpp_std=c++20', 'warning_level=2', 'buildtype=debugoptimized'])

wayland_client = dependency('wayland-client')
wayland_egl = dependency('wayland-egl')
wayland_protocols = dependency('wayland-protocols', version: '>= 1.32')
wlr_protocols = dependency('wlr-protocols')
egl = dependency('egl')
glesv2 = dependency('glesv2')
pangocairo = dependency('pangocairo', version: '>= 1.50')
cairo = dependency('cairo')
json = dependency('nlohmann_json', version: '>= 3.10')

# ── Wayland protocols ──
scanner = find_program(dependency('wayland-scanner', native: true).get_variable('wayland_scanner'))
wl_dir = wayland_protocols.get_variable('pkgdatadir')
wlr_dir = wlr_protocols.get_variable('pkgdatadir')

protocols = {
  'xdg-shell': wl_dir / 'stable/xdg-shell/xdg-shell.xml',
  'tablet-v2': wl_dir / 'stable/tablet/tablet-v2.xml',
  'cursor-shape-v1': wl_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
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
  'src/core/Config.cpp',
  'src/core/EventLoop.cpp',
  'src/core/FileWatcher.cpp',
  'src/core/Paths.cpp',
  'src/ipc/IpcServer.cpp',
  'src/render/Renderer.cpp',
  'src/render/Text.cpp',
  'src/services/Hyprland.cpp',
  'src/shell/Shell.cpp',
  'src/shell/bar/Bar.cpp',
  'src/shell/bar/Widgets.cpp',
  'src/ui/Animation.cpp',
  'src/ui/Node.cpp',
  'src/ui/Theme.cpp',
  'src/wayland/Display.cpp',
  'src/wayland/LayerSurface.cpp',
)

executable('material3ui', sources, protocol_sources,
  include_directories: include_directories('src'),
  dependencies: [wayland_client, wayland_egl, egl, glesv2, pangocairo, cairo, json],
  install: true)
