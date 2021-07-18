# SPDX-License-Identifier: MIT
project('yttui', ['cpp'], default_options: ['buildtype=release', 'cpp_std=c++17'], version: '0.1.0', license: 'MIT')

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
executable('yttui',
    tui_files,
    link_with: [application],
    install: true
)

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: ['Core', 'Gui', 'Widgets'], required: false)
if qt5_dep.found()
    kf5service_dep = dependency('KF5Service', required: false)
    kf5core_dep = dependency('KF5CoreAddons', required: false)
    kf5parts_dep = dependency('KF5Parts', required: false)

    cppc = meson.get_compiler('cpp')
    libutil_dep = cppc.find_library('util')

    moc_files = qt5.preprocess(moc_sources: ['yttui-qt5.cpp'], qresources: ['yttui-qt5.qrc'])

    executable('yttui-qt5',
        ['yttui-qt5.cpp', moc_files],
        link_with: [application],
        dependencies: [libutil_dep, qt5_dep, kf5service_dep, kf5core_dep, kf5parts_dep],
        install: true
    )
endif
