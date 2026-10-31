project('WanganArcadeLoader', 'c', 'cpp', 'nasm', version: '1.0.0',
  meson_version: '>=1.3.0',
  default_options: ['cpp_std=c++20', 'buildtype=release', 'warning_level=2',
                    'default_library=static'])

cpp = meson.get_compiler('cpp')

add_project_arguments(
	cpp.get_supported_arguments(
		'-D_WIN32_WINNT=_WIN32_WINNT_WIN10',
	),
	language: 'cpp',
)

add_project_link_arguments('-static', '-static-libgcc', '-static-libstdc++',
                           language: 'cpp')

minhook = subproject('minhook')
tomlc99 = subproject('tomlc99')
sdl2 = subproject('sdl2', default_options: ['default_library=static', 'test=false', 'use_render=disabled'])

shared_library(
	'bngrw',
	dependencies: [
		minhook.get_variable('minhook_dep'),
		tomlc99.get_variable('tomlc99_dep'),
		sdl2.get_variable('sdl2_dep'),
		cpp.find_library('ws2_32'),
	],
	include_directories: 'src',
	sources : [
		'src/dllmain.cpp',
		'src/helpers.cpp',
		'src/logging.cpp',
		'src/startup.cpp',
		'src/graphics.cpp',
		'src/poll.cpp',
		'src/hooks.asm',
	],
	name_prefix: ''
)

executable(
  'runtime-log-smoke',
  sources: ['tests/runtime-log-smoke.cpp', 'src/logging.cpp', 'src/helpers.cpp'],
  include_directories: 'src',
  dependencies: [minhook.get_variable('minhook_dep'),
                 tomlc99.get_variable('tomlc99_dep'), cpp.find_library('ws2_32')],
)
