TEMPLATE = app
TARGET = qtglassflow-demo
QT += core gui widgets opengl
CONFIG += c++11

INCLUDEPATH += ../src
LIBS += -L../src -lqtglassflow

SOURCES += main.cpp mainwindow.cpp
HEADERS += mainwindow.h

target.path = /usr/bin
INSTALLS += target
