project(
  'surfing',
  'c',
  version: '0.1.0',
  license: 'GPL-3.0-only',
  default_options: ['warning_level=1'],
)

wayland_server = dependency('wayland-server')
wlroots = dependency('wlroots-0.20')
xkbcommon = dependency('xkbcommon')

add_global_arguments('-DWLR_USE_UNSTABLE', language: 'c')

executable(
  'surfing',
  'src/surfing.c',
  'src/server.c',
  include_directories: 'include',
  dependencies: [
    wayland_server,
    wlroots,
    xkbcommon,
  ],
  install: true,
)
