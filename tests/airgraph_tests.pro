QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = airgraph_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/apmodel.h ../src/scanbackend.h ../src/theme.h ../src/translator.h ../src/wifisource.h
SOURCES += airgraph_tests.cpp ../src/apmodel.cpp ../src/scanbackend.cpp ../src/theme.cpp ../src/translator.cpp
