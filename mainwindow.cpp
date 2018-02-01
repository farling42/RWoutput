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
#include "outputhtml.h"

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
    ui->statusBar->showMessage(qApp->applicationName() + " version " + qApp->applicationVersion());

    connect(ui->separateTopicFiles, &QCheckBox::clicked, ui->indexOnEveryPage, &QCheckBox::setEnabled);
    ui->indexOnEveryPage->setEnabled(ui->separateTopicFiles->isChecked());
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_loadFile_clicked()
{
    QSettings settings;
    const QString LOAD_DIRECTORY_PARAM("inputDirectory");
    QString in_filename = QFileDialog::getOpenFileName(this, tr("Realm Works Output File"), /*dir*/ settings.value(LOAD_DIRECTORY_PARAM).toString(), /*template*/ tr("Realm Works® RWoutput Files (*.rwoutput)"));
    if (in_filename.isEmpty()) return;

    // Delete any previously loaded data
    if (root_element != nullptr)
    {
        delete root_element;
        root_element = nullptr;
    }

    in_file.setFileName(in_filename);

    if (!in_file.open(QFile::ReadOnly))
    {
        qWarning() << tr("Failed to find file") << in_file.fileName();
        return;
    }
    settings.setValue(LOAD_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath());

    ui->htmlOutput->setEnabled(false);
    ui->filename->setText(QFileInfo(in_file).fileName());

    ui->statusBar->showMessage("Loading RWoutput file...");
    root_element = XmlElement::readTree(&in_file);
    ui->statusBar->showMessage("RWoutput file LOAD complete.");

    ui->htmlOutput->setEnabled(true);
    in_file.close();
}

void MainWindow::on_saveHtml_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    // Choose a directory in which to generate all the files:
    // we'll create index.html in that directory
    bool separate_files = ui->separateTopicFiles->isChecked();
    QString start_dir = settings.value(SAVE_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath()).toString();
    QString path = separate_files
            ? QFileDialog::getExistingDirectory(this, tr("Output Directory"),
                                                /*dir*/ start_dir)
            : QFileDialog::getSaveFileName(this, tr("Output File"),
                                           /*dir*/ start_dir + "/" + QFileInfo(in_file).baseName() + ".xhtml",
                                           /*filter*/ "XHTML files (*.xhtml)");

    if (path.isEmpty()) return;
    QDir dir(separate_files ? path : QFileInfo(path).absolutePath());

    if (!dir.exists())
    {
        ui->statusBar->showMessage("The directory does not exist!");
        return;
    }
    settings.setValue(SAVE_DIRECTORY_PARAM, dir.absolutePath());
    QDir::setCurrent(dir.absolutePath());

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;
    ui->statusBar->showMessage("Saving XHTML file...");

    toHtml(path, root_element,
           max_image_width,
           separate_files,
           ui->revealMask->isChecked(),
           separate_files && ui->indexOnEveryPage->isChecked());

    ui->statusBar->showMessage("XHTML file SAVE complete.");
}
