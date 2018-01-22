#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("Amusing Time");
    QCoreApplication::setOrganizationDomain("amusingtime.uk");
    QCoreApplication::setApplicationName("RWoutput");

    MainWindow w;
    w.show();

    return a.exec();
}
