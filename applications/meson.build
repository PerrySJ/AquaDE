# Shell Settings' entry beside the build, for a DEsk run from the build
# directory: it looks in applications/ next to its own binary last (apps.c).
configure_file(
  input: '../session/aquade-settings.desktop.in',
  output: 'aquade-settings.desktop',
  configuration: { 'EXEC': meson.project_build_root() / 'aquade-settings',
    'ICON': meson.project_source_root() / 'session/aquade-settings.png' },
)
configure_file(
  input: '../session/aquade-index.desktop.in',
  output: 'aquade-index.desktop',
  configuration: { 'EXEC': meson.project_build_root() / 'aquade-index',
    'ICON': meson.project_source_root() / 'session/aquade-settings.png' },
)
