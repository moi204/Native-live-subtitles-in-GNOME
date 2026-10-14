livecaptions_sources = [
  'main.c',
  'parakeet-audio.c',
  'parakeet-engine.c'
]

cc = meson.get_compiler('c')

livecaptions_deps = [
  dependency('libadwaita-1', version: '>= 1.0'),
  dependency('libpulse-simple'),
  dependency('x11', required: false),
  cc.find_library('m', required: false),
  parakeet_lib,
  parakeet.dependency('ggml'),
  parakeet.dependency('ggml_base'),
  parakeet.dependency('ggml_cpu')
]

executable('livecaptions', livecaptions_sources,
  dependencies: livecaptions_deps,
  install_rpath: join_paths('$ORIGIN', '..', 'lib'),
  install: true,
)
