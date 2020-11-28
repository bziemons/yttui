project('yttui', ['cpp'], default_options: ['buildtype=release', 'cpp_std=c++17'], version: '0.1.0')

add_project_arguments('-Werror=return-type', language: 'cpp')

termpaint_dep = dependency('termpaint', static: true)
sqlite3_dep = dependency('sqlite3')
curl_dep = dependency('libcurl')
json_dep = dependency('nlohmann_json')

#ide:editable-filelist
src_files = [
  'db.cpp',
  'main.cpp',
  'tui.cpp',
  'yt.cpp',
]

deps = [
  termpaint_dep,
  sqlite3_dep,
  curl_dep,
  json_dep
]

executable('yttui', src_files, dependencies: deps, install: true)
