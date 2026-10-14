project('livecaptions', 'c',
          version: '0.1.0',
    meson_version: '>= 0.59.0',
  default_options: [ 'warning_level=2',
                     'werror=false',
                     'c_std=gnu11',
                   ],
)

cmake = import('cmake')
parakeet_options = cmake.subproject_options()
parakeet_options.add_cmake_defines({
  'CMAKE_BUILD_TYPE': 'Release',
  'PARAKEET_BUILD_CLI': false,
  'PARAKEET_BUILD_SERVER': false,
  'PARAKEET_BUILD_TESTS': false,
  'PARAKEET_SHARED': false,
  'PARAKEET_WITH_CED': false,
  'PARAKEET_WITH_VOICEDETECT': false,
  'PARAKEET_GGML_VULKAN': false,
})
parakeet = cmake.subproject('parakeet.cpp', options: parakeet_options)

parakeet_lib = parakeet.dependency('parakeet')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'livecaptions')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
configure_file(
  output: 'livecaptions-config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.project_build_root(),
], language: 'c')


subdir('data')
subdir('src')
