TEMPLATE = lib
TARGET = qtglassflow
VERSION = 0.1.0
QT += core gui widgets opengl
CONFIG += c++11

HEADERS += qtglassflowscene.h
SOURCES += qtglassflowscene.cpp
RESOURCES += shaders.qrc

target.path = /usr/lib/$$system(dpkg-architecture -qDEB_HOST_MULTIARCH)
headers.files = qtglassflowscene.h
headers.path = /usr/include/qtglassflow
INSTALLS += target headers
