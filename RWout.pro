#-------------------------------------------------
#
# Project created by QtCreator 2018-01-21T00:11:58
#
#-------------------------------------------------

VERSION = 1.17

QT       += core gui printsupport

CONFIG  += c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = RWout
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x051100

SOURCES += \
        main.cpp \
        mainwindow.cpp \
    xmlelement.cpp \
    outputhtml.cpp \
    linefile.cpp \
    outhtml4subset.cpp

HEADERS += \
        mainwindow.h \
    xmlelement.h \
    outputhtml.h \
    linefile.h \
    outhtml4subset.h

FORMS += \
        mainwindow.ui

macx {
MACDEPLOYQT_OPTIONS = -verbose 3 -dmg
CONFIG += macdeployqt
}

win32 {
WINDEPLOYQT_OPTIONS = -verbose 3
CONFIG += windeployqt

COMPANY = com.amusingtime.RWOutput

# Get the 32-bit compiler to allow 2GB or more of accessible memory.
QMAKE_CXXFLAGS += -Wl,--large-address-aware
QMAKE_LFLAGS   += -Wl,--large-address-aware

DESTDIR = install

# The DISTFILE appears in the "Other files" section of Qt Creator
DISTFILES += LICENSE \
    config.nsi

# Installation on Windows is performed using NSIS (see http://nsis.sourceforge.net/)
# On Project page, under "Run" step.
# 1 - add a "Make" step with the following:
#     Make arguments = "-f Makefile.%{CurrentBuild:Name} windeployqt"
# 2 - add a "Custom" step with the following:
#     Command   = "C:\Program Files (x86)\NSIS\makeNSIS.exe"
#     Arguments = "/nocd %{CurrentProject:NativePath}\config.nsi"
#     Working Directory = "%{buildDir}"
}

RESOURCES += \
    rwout.qrc

# Add ZLIB (required for QUAZIP)
ZLIB = $$PWD/../zlib-1.2.11
INCLUDEPATH += $$ZLIB
LIBS += -L$$ZLIB -lz
EXTRA_BINFILES += $$ZLIB/zlib1.dll

# Add QUAZIP (used to open HL portfolio files)
# (quazip.dll is not copied into the install directory!)
QUAZIP = $$PWD/../quazip-0.7.3
INCLUDEPATH += $$QUAZIP
LIBS += -L$$QUAZIP/quazip/release -lquazip
EXTRA_BINFILES += $$QUAZIP/quazip/release/quazip.dll

for(FILE,EXTRA_BINFILES){
QMAKE_POST_LINK += $$$$shell_path($(COPY_FILE) $${FILE} $(DESTDIR)$$escape_expand(\n\t))
}

MYMINGW = Desktop_Qt_5_10_1_MinGW_32bit
MYMSVC  = Desktop_Qt_5_10_1_MSVC2015_64bit

win32-g++:CONFIG(release, debug|release): LIBS += -L$$PWD/../build-gumbo-$${MYMINGW}-Release/release/ -lgumbo
else:win32-g++:CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-gumbo-$${MYMINGW}-Debug/debug/ -lgumbo
else:win32:!win32-g++:CONFIG(release, debug|release): LIBS += -L$$PWD/../build-gumbo-$${MYMSVC}-Release/release/ -lgumbo
else:win32:!win32-g++:CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-gumbo-$${MYMSVC}-Debug/debug/ -lgumbo
else:unix: LIBS += -L$$PWD/../build-gumbo-$${MYMINGW}-Release/ -lgumbo

INCLUDEPATH += $$PWD/../gumbo
DEPENDPATH += $$PWD/../gumbo

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMINGW}-Release/release/libgumbo.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMINGW}-Debug/debug/libgumbo.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMSVC}-Release/release/gumbo.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMSVC}-Debug/debug/gumbo.lib
else:unix: PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMINGW}-Release/libgumbo.a

# %{CurrentKit:FileSystemName} = Desktop_...32bit
