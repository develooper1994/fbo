TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.c

LIBS += -lpthread

# Compiler flags
QMAKE_CFLAGS += -O3 -mfpu=neon # -march=armv7-a
QMAKE_CXXFLAGS += -O3 -mfpu=neon # -march=armv7-a

TARGET = fbo
#target.path = # path on device
INSTALLS += target
