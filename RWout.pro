#-------------------------------------------------
#
# Project created by QtCreator 2018-01-21T00:11:58
#
#-------------------------------------------------

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
    xmlelement.cpp

HEADERS += \
        mainwindow.h \
    xmlelement.h

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
    Installer/packages/com.amusingtime.RWoutput/meta/package.xml \
    Installer/packages/com.amusingtime.RWoutput/meta/installscript.qs \
    Installer/packages/com.amusingtime.RWoutput/meta/LICENSE.txt

# Put binarycreator packages files in the correct place
bincre.path=$${OUT_PWD}
bincre.files=Installer/packages
INSTALLS += bincre

# Add new rule that will run the binarycreator (needs adding in Projects/Build Steps)
QMAKE_EXTRA_TARGETS += binarycreator
#binarycreator.target = binarycreator
#binarycreator.depends = $${PWD}/Installer/config/config.xml packages install
#binarycreator.commands = D:\Qt\QtIFW-3.0.1\bin\binarycreator.exe --offline-only \
#    -c $$shell_path($${PWD}/Installer/config/config.xml) \
#    -p packages RWout.exe

INSTALLERS = Installer/config/config.xml

binarycreator.input = INSTALLERS
binarycreator.name = binarycreator
binarycreator.depends = $$INSTALLERS install install_bincre
binarycreator.output = RWoutput.exe
binarycreator.variable_out = HEADERS
binarycreator.commands = D:\Qt\QtIFW-3.0.1\bin\binarycreator.exe --offline-only \
    -c ${QMAKE_FILE_IN} -p packages ${QMAKE_FILE_OUT}

QMAKE_EXTRA_COMPILERS += binarycreator
}
