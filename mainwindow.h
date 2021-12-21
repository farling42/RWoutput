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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFile>

namespace Ui {
class MainWindow;
}

class XmlElement;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    bool loadFile(const QString &filename);

private slots:
    void on_loadFile_clicked();
    void on_saveHtml_clicked();
    void on_saveMarkdown_clicked();
    void on_savePdf_clicked();
    void on_print_clicked();
    void on_simpleHtml_clicked();
    void on_mapPins_clicked();
    void on_saveFgMod_clicked();    
    void on_obsidianPlugins_clicked();
private:
    Ui::MainWindow *ui;
    XmlElement *root_element;
    QFile in_file;
    void setStatusText(const QString &text);
    int maxWidth();
    void saveSettings();
};

#endif // MAINWINDOW_H
