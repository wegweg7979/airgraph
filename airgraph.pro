QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = airgraph
TEMPLATE = app

HEADERS += \
    src/apfiltermodel.h \
    src/aphistory.h \
    src/apmodel.h \
    src/bandplan.h \
    src/cli.h \
    src/historyitem.h \
    src/nmwifisource.h \
    src/scanbackend.h \
    src/sessionexport.h \
    src/spectrum.h \
    src/spectrumitem.h \
    src/theme.h \
    src/translator.h \
    src/wifisource.h

SOURCES += \
    src/main.cpp \
    src/apfiltermodel.cpp \
    src/aphistory.cpp \
    src/apmodel.cpp \
    src/bandplan.cpp \
    src/cli.cpp \
    src/historyitem.cpp \
    src/nmwifisource.cpp \
    src/scanbackend.cpp \
    src/sessionexport.cpp \
    src/spectrum.cpp \
    src/spectrumitem.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
