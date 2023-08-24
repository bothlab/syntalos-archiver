project('Syntalos Archiver', 'c', 'cpp',
  meson_version: '>=0.64',
  default_options: ['c_std=gnu17',
                    'cpp_std=gnu++17',
                    'b_lto=true'],

  license: 'LGPL-3.0+',
  version: '0.0.1',
)

cpp_compiler = meson.get_compiler('cpp')
source_root = meson.project_source_root()

sya_version = meson.project_version()
varr = sya_version.split('.')
sya_major_version = varr[0]
sya_minor_version = varr[1]
sya_micro_version = varr[2]

root_include_dir = include_directories('.')

#
# Modules
#
fs     = import('fs')
qt     = import('qt5')

#
# Configure files
#
conf = configuration_data()
conf.set('SYA_MAJOR_VERSION_CONF', sya_major_version)
conf.set('SYA_MINOR_VERSION_CONF', sya_minor_version)
conf.set('SYA_MICRO_VERSION_CONF', sya_micro_version)
conf.set_quoted('PROJECT_VERSION', sya_version)
conf.set_quoted('LIBEXECDIR',
                join_paths(get_option('prefix'),
                           get_option('libexecdir')))
conf.set_quoted('DATADIR',
                join_paths(get_option('prefix'),
                           get_option('datadir')))
conf.set_quoted('LIBDIR',
                join_paths(get_option('prefix'),
                           get_option('libdir')))
conf.set_quoted('BINDIR',
                join_paths(get_option('prefix'),
                           get_option('bindir')))
conf.set_quoted('SYSCONFDIR',
                join_paths(get_option('prefix'),
                           get_option('sysconfdir')))
conf.set_quoted('SYA_VCS_TAG', '@VCS_TAG@')
config_h = configure_file(output: 'config.h.in', configuration: conf)
config_h = vcs_tag(input: config_h, output: 'config.h')

#
# Custom compile flags
#
if get_option('maintainer')
    maintainer_c_args = [
         '-Werror',
         '-Wall',
         '-Wextra',
         '-Wcast-align',
         '-Wno-uninitialized',
         '-Wempty-body',
         '-Wformat-security',
         '-Winit-self',
         '-Wnull-dereference',
         '-Winline'
    ]
    maintainer_cpp_args = [
        '-Wsuggest-final-methods'
    ]

    add_project_arguments(maintainer_c_args, language: 'c')
    add_project_arguments([maintainer_c_args, maintainer_cpp_args], language: 'cpp')
endif

# a few compiler warning/error flags we always want enabled
add_project_arguments(
  '-Werror=shadow',
  '-Werror=empty-body',
  '-Werror=missing-prototypes',
  '-Werror=implicit-function-declaration',
  '-Werror=missing-declarations',
  '-Werror=return-type',
  '-Werror=int-conversion',
  '-Werror=incompatible-pointer-types',
  '-Werror=misleading-indentation',
  '-Werror=format-security',

  '-Wno-missing-field-initializers',
  '-Wno-error=missing-field-initializers',
  '-Wno-unused-parameter',
  '-Wno-error=unused-parameter',
  language: 'c'
)
add_project_arguments(
  cpp_compiler.get_supported_arguments([
    '-Werror=empty-body',
    '-Werror=pointer-arith',
    '-Werror=missing-declarations',
    '-Werror=return-type',
    '-Werror=misleading-indentation',
    '-Werror=format-security',
    '-Werror=suggest-override',

    '-Wno-missing-field-initializers',
    '-Wno-error=missing-field-initializers',
    '-Wno-unused-parameter',
    '-Wno-error=unused-parameter',
  ]),
  language: 'cpp'
)

#
# Dependencies
#
qt_core_dep = dependency('qt5', modules: ['Core'], version: '>= 5.15', include_type: 'system')
qt_gui_dep = dependency('qt5', modules: ['Gui', 'Widgets'])
qt_test_dep = dependency('qt5', modules: ['Test'])
qt_svg_dep = dependency('qt5', modules: ['Svg'])
qt_concurrent_dep = dependency('qt5', modules: ['Concurrent'])
sqfs_dep = dependency('libsquashfs1', version: '>= 1.2')

#
# Subdirectories
#
subdir('src')
