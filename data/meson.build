service_file = configure_file(
  input: 'net.sapples.LiveCaptions.service.in',
  output: 'net.sapples.LiveCaptions.service',
  configuration: {'bindir': join_paths(get_option('prefix'), get_option('bindir'))},
  install: true,
  install_dir: join_paths(get_option('datadir'), 'dbus-1/services')
)

install_data('../extension/metadata.json', '../extension/extension.js', '../extension/prefs.js',
  install_dir: join_paths(get_option('datadir'), 'gnome-shell/extensions/livecaptions@local'))

install_data('../extension/schemas/org.gnome.shell.extensions.livecaptions.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'gnome-shell/extensions/livecaptions@local/schemas'))

custom_target('livecaptions-extension-schemas',
  input: '../extension/schemas/org.gnome.shell.extensions.livecaptions.gschema.xml',
  output: 'gschemas.compiled',
  command: [find_program('glib-compile-schemas'), '--targetdir', '@OUTDIR@',
            meson.current_source_dir() / '../extension/schemas'],
  install: true,
  install_dir: join_paths(get_option('datadir'), 'gnome-shell/extensions/livecaptions@local/schemas'))
