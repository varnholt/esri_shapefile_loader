#-------------------------------------------------
#
# Project created by QtCreator 2014-07-06T15:14:51
#
#-------------------------------------------------

QT       += core
QT       += gui
QT       += opengl

TARGET = shapeloader
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app

OBJECTS_DIR=.obj
MOC_DIR=.moc

win32 {
   DEFINES += _USE_MATH_DEFINES
   LIBS += -lopengl32
}

SOURCES += src/main.cpp \
    src/shapeloader.cpp \
    src/shapeheader.cpp \
    src/shape.cpp \
    src/shapepolygon.cpp \
    src/shapepoint.cpp \
    src/shapebox.cpp \
    src/recordheader.cpp \
    src/endian.cpp \
    src/shapenull.cpp \
    src/widget.cpp \
    src/pointrecord.cpp \
    src/streamreader.cpp

HEADERS += \
    src/shapeloader.h \
    src/shapeheader.h \
    src/shape.h \
    src/shapepolygon.h \
    src/shapepoint.h \
    src/shapebox.h \
    src/recordheader.h \
    src/endian.h \
    src/shapenull.h \
    src/widget.h \
    src/pointrecord.h \
    src/vector2.h \
    src/streamreader.h

DISTFILES += \
   data/ne_110m_land.shp \
   data/ne_110m_populated_places_simple.shp
