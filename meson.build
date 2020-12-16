# SPDX-License-Identifier: MIT
project('yttui', ['cpp'], default_options: ['buildtype=release', 'cpp_std=c++17'], version: '0.1.0')

add_project_arguments('-Werror=return-type', language: 'cpp')

termpaint_dep = dependency('termpaint', static: true, version: '>=0.3.0')
sqlite3_dep = dependency('sqlite3')
curl_dep = dependency('libcurl')
json_dep = dependency('nlohmann_json', version: '>=3.5.0')

#ide:editable-filelist
application_files = [
  'application.cpp',
  'db.cpp',
  'tui.cpp',
  'yt.cpp',
]

#ide:editable-filelist
application_deps = [
  termpaint_dep,
  sqlite3_dep,
  curl_dep,
  json_dep
]

application = static_library('yttui-application', application_files, dependencies: application_deps)

tui_files = [
  'yttui.cpp',
]
executable('yttui', tui_files, link_with: [application], install: true)
