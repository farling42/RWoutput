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
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QSettings>
#include "xmlelement.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    root_element(nullptr)
{
    ui->setupUi(this);
    // Set icons that can't be set in Designer.
    ui->loadFile->setIcon(style()->standardIcon(QStyle::SP_FileDialogStart));
    ui->saveHtml->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    // No options available until a file has been loaded.
    ui->htmlOutput->setEnabled(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_loadFile_clicked()
{
    QSettings settings;
    const QString LOAD_DIRECTORY_PARAM("inputDirectory");
    QString in_filename = QFileDialog::getOpenFileName(this, tr("RWoutput File"), /*dir*/ settings.value(LOAD_DIRECTORY_PARAM).toString(), /*template*/ tr("Realm Works® RWoutput Files (*.rwoutput)"));
    if (in_filename.isEmpty()) return;

    // Delete any previously loaded data
    if (root_element != nullptr)
    {
        delete root_element;
        root_element = nullptr;
    }

    QFile in_file(in_filename);
    if (!in_file.open(QFile::ReadOnly))
    {
        qWarning() << tr("Failed to find file") << in_file.fileName();
        return;
    }
    settings.setValue(LOAD_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath());

    ui->htmlOutput->setEnabled(false);

    ui->statusBar->showMessage("Loading RWoutput file...");
    root_element = XmlElement::readTree(&in_file);
    ui->statusBar->showMessage("RWoutput file LOAD complete.");

    ui->htmlOutput->setEnabled(true);
}

void MainWindow::on_saveHtml_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    QFile out_file;
    if (ui->oneTopicPerFile->isChecked())
    {
        // Choose a directory in which to generate all the files:
        // we'll create index.html in that directory
        QString out_directory = QFileDialog::getExistingDirectory(this, tr("Output Directory"), /*dir*/ settings.value(SAVE_DIRECTORY_PARAM).toString());
        if (out_directory.isEmpty()) return;
        out_file.setFileName(out_directory + "/index.xhtml");
    }
    else
    {
        // Enter a filename to generate a single massive file
        QString out_filename = QFileDialog::getSaveFileName(this, tr("XHTML File"), /*dir*/ settings.value(SAVE_DIRECTORY_PARAM).toString(), /*template*/ tr("XHTML Files (*.xhtml)"));
        if (out_filename.isEmpty()) return;
        out_file.setFileName(out_filename);
    }

    if (!out_file.open(QFile::WriteOnly))
    {
        qWarning() << tr("Failed to find file") << out_file.fileName();
        return;
    }

    settings.setValue(SAVE_DIRECTORY_PARAM, QFileInfo(out_file).absolutePath());
    QDir::setCurrent(QFileInfo(out_file).path());

    out_file.write("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">");

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;
    ui->statusBar->showMessage("Saving XHTML file...");
    QXmlStreamWriter out_stream(&out_file);
    root_element->toHtml(out_stream, ui->oneTopicPerFile->isChecked(), max_image_width, ui->revealMask->isChecked());
    ui->statusBar->showMessage("XHTML file SAVE complete.");
}
