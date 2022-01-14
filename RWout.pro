#-------------------------------------------------
#
# Project created by QtCreator 2018-01-21T00:11:58
#
#-------------------------------------------------

# Ensure "Enable Qt quick compiler" is disabled in the "qmake" step of Projects -> Build Options

VERSION = 4.17

QT       += core gui printsupport network

CONFIG  += c++17

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = RWout
TEMPLATE = app

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x051200

SOURCES += \
    fg_category_delegate.cpp \
    fileuploader.cpp \
    gentextdocument.cpp \
    main.cpp \
    mainwindow.cpp \
    mappinsdialog.cpp \
    outputfgmod.cpp \
    outputmarkdown.cpp \
    xmlelement.cpp \
    outputhtml.cpp \
    linefile.cpp \
    outhtml4subset.cpp

HEADERS += \
    fg_category_delegate.h \
    fileuploader.h \
    gentextdocument.h \
    mainwindow.h \
    mappinsdialog.h \
    outputfgmod.h \
    outputmarkdown.h \
    xmlelement.h \
    outputhtml.h \
    linefile.h \
    outhtml4subset.h \
    linkage.h

FORMS += \
        mainwindow.ui \
        mappinsdialog.ui \
        obsidiandialog.ui

macx {
MACDEPLOYQT_OPTIONS = -verbose 3 -dmg
CONFIG += macdeployqt
}

win32|win64 {
WINDEPLOYQT_OPTIONS = -verbose 3
CONFIG += windeployqt
}

COMPANY = com.amusingtime.RWOutput

DESTDIR = install

# The DISTFILE appears in the "Other files" section of Qt Creator
DISTFILES += LICENSE \
    CHANGELOG.md \
    README.md \
    config.nsi

# Installation on Windows is performed using NSIS (see http://nsis.sourceforge.net/)
# On Project page, under "Run" step.
# 1 - add a "Make" step with the following:
#     Make arguments = "-f Makefile.%{CurrentBuild:Name} windeployqt"
# 2 - add a "Custom" step with the following:
#     Command   = "C:\Program Files (x86)\NSIS\makeNSIS.exe"
#     Arguments = "/nocd %{CurrentProject:NativePath}\config.nsi"
#     Working Directory = "%{buildDir}"

RESOURCES += \
    rwout.qrc

# %{CurrentKit:FileSystemName} = Desktop_...32bit
MYMINGW = Desktop_Qt_5_12_8_MinGW_64_bit

#
# GUMBO library
#
CONFIG(release, debug|release): LIBS += -L$$PWD/../build-gumbo-$${MYMINGW}-Release/release/ -lgumbo
CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-gumbo-$${MYMINGW}-Debug/debug/ -lgumbo

INCLUDEPATH += $$PWD/../gumbo
DEPENDPATH += $$PWD/../gumbo

CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMINGW}-Release/release/libgumbo.a
CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-$${MYMINGW}-Debug/debug/libgumbo.a

#
# QUAZIP library (+ zlib library)
#
LIBS += -lz
CONFIG(release, debug|release): LIBS += -L$$PWD/../build-quazip-$${MYMINGW}-Release/quazip/release/ -lquazip -lz
CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-quazip-$${MYMINGW}-Debug/quazip/debug/ -lquazipd -lz

INCLUDEPATH += $$PWD/../quazip-0.7.3
DEPENDPATH += $$PWD/../quazip-0.7.3

CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../build-quazip-$${MYMINGW}-Release/quazip/release/libquazip.a
CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../build-quazip-$${MYMINGW}-Debug/quazip/debug/libquazipd.a
