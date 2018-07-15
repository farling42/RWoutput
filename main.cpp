/*
 * This file is part of the RWout (https://github.com/farling42/RWoutput).
 * Copyright (c) 2018 Martin Smith.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

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

    // See if we have a file to load
    QStringList args = qApp->arguments();
    if (args.size() > 1)
    {
        a.processEvents();
        w.loadFile(args.at(1));
    }

    return a.exec();
}
