TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        db.cpp \
        main.cpp \
        tui.cpp \
        yt.cpp

HEADERS += \
    db.h \
    tui.h \
    yt.h

unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += libcurl sqlite3 nlohmann_json termpaint

DISTFILES += \
    README.md \
    yttui.conf.example
