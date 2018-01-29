#-------------------------------------------------
#
# Project created by QtCreator 2018-01-21T00:11:58
#
#-------------------------------------------------

VERSION = 1.10

QT       += core gui

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
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x051000    # disables all the APIs deprecated before Qt 6.0.0


SOURCES += \
        main.cpp \
        mainwindow.cpp \
    xmlelement.cpp \
    outputhtml.cpp

HEADERS += \
        mainwindow.h \
    xmlelement.h \
    outputhtml.h

FORMS += \
        mainwindow.ui \
    Installer/packages/com.amusingtime.RWoutput/meta/page.ui

macx {
MACDEPLOYQT_OPTIONS = -verbose 3 -dmg
CONFIG += macdeployqt
}

win32 {
WINDEPLOYQT_OPTIONS = -verbose 3
CONFIG += windeployqt

COMPANY = com.amusingtime.RWOutput

DESTDIR = packages/$${COMPANY}/data

# The DISTFILE appears in the "Other files" section of Qt Creator
DISTFILES += \
    Installer/config/config.xml \
    Installer/packages/$${COMPANY}/meta/package.xml \
    Installer/packages/$${COMPANY}/meta/installscript.qs \
    Installer/packages/$${COMPANY}/meta/LICENSE.txt

# Put binarycreator packages files in the correct place
bincre.path=$${OUT_PWD}
bincre.files=Installer/packages ${DISTFILES}
INSTALLS += bincre

# Add new rule that will run the binarycreator (needs adding in Projects/Build Steps)
}

RESOURCES += \
    rwout.qrc

DISTFILES +=

win32-g++:CONFIG(release, debug|release): LIBS += -L$$PWD/../build-gumbo-Desktop_Qt_5_9_2_MinGW_32bit-Release/release/ -lgumbo
else:win32-g++:CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-gumbo-Desktop_Qt_5_9_2_MinGW_32bit-Release/debug/ -lgumbo
else:win32:!win32-g++:CONFIG(release, debug|release): LIBS += -L$$PWD/../build-gumbo-Desktop_Qt_5_9_2_MSVC2015_64bit-Release/release/ -lgumbo
else:win32:!win32-g++:CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-gumbo-Desktop_Qt_5_9_2_MSVC2015_64bit-Release/debug/ -lgumbo
else:unix: LIBS += -L$$PWD/../build-gumbo-Desktop_Qt_5_9_2_MinGW_32bit-Release/ -lgumbo

INCLUDEPATH += $$PWD/../gumbo
DEPENDPATH += $$PWD/../gumbo

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-Desktop_Qt_5_9_2_MinGW_32bit-Release/release/libgumbo.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-Desktop_Qt_5_9_2_MinGW_32bit-Release/debug/libgumbo.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-Desktop_Qt_5_9_2_MSVC2015_64bit-Release/release/gumbo.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../build-gumbo-Desktop_Qt_5_9_2_MSVC2015_64bit-Release/debug/gumbo.lib
else:unix: PRE_TARGETDEPS += $$PWD/../build-gumbo-Desktop_Qt_5_9_2_MinGW_32bit-Release/libgumbo.a
