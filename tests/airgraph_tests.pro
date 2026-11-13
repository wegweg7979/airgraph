QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = airgraph_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/apfiltermodel.h ../src/aphistory.h ../src/apmodel.h ../src/bandplan.h \
           ../src/scanbackend.h ../src/sessionexport.h ../src/spectrum.h ../src/theme.h \
           ../src/translator.h ../src/wifisource.h
SOURCES += airgraph_tests.cpp ../src/apfiltermodel.cpp ../src/aphistory.cpp ../src/apmodel.cpp \
           ../src/bandplan.cpp ../src/scanbackend.cpp ../src/sessionexport.cpp ../src/spectrum.cpp \
           ../src/theme.cpp ../src/translator.cpp
