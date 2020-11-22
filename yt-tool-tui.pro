TEMPLATE = app
CONFIG += console c++17 sanitizer sanitize_address
CONFIG -= app_bundle
CONFIG -= qt

unix:!macx: LIBS += -L$$PWD/termpaint/ -ltermpaint

INCLUDEPATH += $$PWD/termpaint
DEPENDPATH += $$PWD/termpaint

unix:!macx: PRE_TARGETDEPS += $$PWD/termpaint/libtermpaint.a

SOURCES += \
        main.cpp \
        tui.cpp \
        yt.cpp

HEADERS += \
    tui.h \
    yt.h

unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += libcurl sqlite3 nlohmann_json
