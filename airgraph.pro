QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = airgraph
TEMPLATE = app

HEADERS += \
    src/apmodel.h \
    src/nmwifisource.h \
    src/scanbackend.h \
    src/theme.h \
    src/translator.h \
    src/wifisource.h

SOURCES += \
    src/main.cpp \
    src/apmodel.cpp \
    src/nmwifisource.cpp \
    src/scanbackend.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
