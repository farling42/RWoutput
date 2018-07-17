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
#include <QPrinter>
#include <QPdfWriter>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QTextDocument>

#include "xmlelement.h"
#include "outputhtml.h"
#include "outhtml4subset.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    root_element(nullptr)
{
    ui->setupUi(this);

    setWindowTitle(QString("%1   v%2").arg(windowTitle()).arg(qApp->applicationVersion()));

    // Set icons that can't be set in Designer.
    ui->loadFile->setIcon(style()->standardIcon(QStyle::SP_FileDialogStart));
    ui->saveHtml->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    ui->savePdf->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    //ui->print->setIcon(style()->standardIcon(QStyle::));
    ui->simpleHtml->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    // No options available until a file has been loaded.
    ui->output->setEnabled(false);

    connect(ui->separateTopicFiles, &QCheckBox::clicked, ui->indexOnEveryPage, &QCheckBox::setEnabled);
    ui->indexOnEveryPage->setEnabled(ui->separateTopicFiles->isChecked());
}

MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::loadFile(const QString &in_filename)
{
    qDebug() << "Loading file" << in_filename;

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
        return false;
    }

    ui->output->setEnabled(false);
    ui->filename->setText(QFileInfo(in_file).fileName());

    ui->statusBar->showMessage("Loading RWoutput file...");
    root_element = XmlElement::readTree(&in_file);
    ui->statusBar->showMessage("RWoutput file LOAD complete.");

    ui->topicCount->setText(QString("Topic Count: %1").arg(root_element->findChildren<XmlElement*>("topic").count()));
    ui->output->setEnabled(true);
    in_file.close();
    return true;
}

void MainWindow::on_loadFile_clicked()
{
    const QString LOAD_DIRECTORY_PARAM("inputDirectory");
    QSettings settings;
    QString in_filename = QFileDialog::getOpenFileName(this, tr("Realm Works Output File"), /*dir*/ settings.value(LOAD_DIRECTORY_PARAM).toString(), /*template*/ tr("Realm Works® RWoutput Files (*.rwoutput)"));
    if (in_filename.isEmpty()) return;

    if (loadFile(in_filename))
    {
        settings.setValue(LOAD_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath());
    }
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


void MainWindow::on_savePdf_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    // Choose a directory in which to generate all the files:
    // we'll create index.html in that directory
    QString start_dir = settings.value(SAVE_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath()).toString();
    QString fileName = QFileDialog::getSaveFileName(this, tr("Output File"),
                                           /*dir*/ start_dir + "/" + QFileInfo(in_file).baseName() + ".pdf",
                                           /*filter*/ "PDF files (*.pdf)");
    if (fileName.isEmpty()) return;

    QDir dir(QFileInfo(fileName).absolutePath());
    settings.setValue(SAVE_DIRECTORY_PARAM, dir.absolutePath());
    QDir::setCurrent(dir.absolutePath());

    QFile file(fileName);
    if (file.open(QFile::WriteOnly|QFile::Text))
    {
        bool ok = true;
        int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
        if (!ok) max_image_width = -1;

        QTextDocument doc;

        ui->statusBar->showMessage("Generating HTML4 subset contents...");
        {
            QString result;   // only keep long enough to call doc.setHtml
            QTextStream stream(&result, QIODevice::WriteOnly|QIODevice::Text);
            outHtml4Subset(stream, root_element, max_image_width, ui->revealMask->isChecked());
            ui->statusBar->showMessage("Transferring to QTextDocument...");
            doc.setHtml(result);
        }
        //doc.setPageSize(printer.pageRect().size()); // This is necessary if you want to hide the page number

        ui->statusBar->showMessage("Saving PDF file...");
        QPdfWriter pdf(&file);
        pdf.setPageSize(QPrinter::A4);
        // Try explicitly setting page size so that QTextDocument doesn't have to lay out
        // the entire document itself. PDF viewer reports unrecognised font!
        //doc.setPageSize(pdf.pageLayout().pageSize().size(QPageSize::Point));
        doc.print(&pdf);
        ui->statusBar->showMessage("PDF file SAVE complete.");
    }
    else
    {
        ui->statusBar->showMessage("Failed to open PDF file.");
    }
}

void MainWindow::on_print_clicked()
{
    // Choose a directory in which to generate all the files:
    // we'll create index.html in that directory
    QPrintDialog dialog(this);

#if 0
    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setPaperSize(QPrinter::A4);
    printer.setOutputFileName(fileName);
#endif
    if (dialog.exec() == QDialog::Accepted)
    {
        bool ok = true;
        int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
        if (!ok) max_image_width = -1;

        QTextDocument doc;

        ui->statusBar->showMessage("Generating HTML4 subset contents...");
        {
            QString result;   // only keep long enough to call doc.setHtml
            QTextStream stream(&result, QIODevice::WriteOnly|QIODevice::Text);
            outHtml4Subset(stream, root_element, max_image_width, ui->revealMask->isChecked());
            ui->statusBar->showMessage("Transferring to QTextDocument...");
            doc.setHtml(result);
        }
        doc.setPageSize(dialog.printer()->pageRect().size()); // This is necessary if you want to hide the page number

        ui->statusBar->showMessage("Printing...");
        doc.print(dialog.printer());

        ui->statusBar->showMessage("Print complete.");
    }
}


void MainWindow::on_simpleHtml_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    // Choose a directory in which to generate all the files:
    // we'll create index.html in that directory
    QString start_dir = settings.value(SAVE_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath()).toString();
    QString fileName = QFileDialog::getSaveFileName(this, tr("Output File"),
                                           /*dir*/ start_dir + "/" + QFileInfo(in_file).baseName() + ".html",
                                           /*filter*/ "HTML files (*.html)");
    if (fileName.isEmpty()) return;

    QDir dir(QFileInfo(fileName).absolutePath());
    settings.setValue(SAVE_DIRECTORY_PARAM, dir.absolutePath());
    QDir::setCurrent(dir.absolutePath());

    QFile file(fileName);
    if (file.open(QFile::WriteOnly|QFile::Text))
    {
        bool ok = true;
        int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
        if (!ok) max_image_width = -1;

        ui->statusBar->showMessage("Saving HTML4 file...");
        QTextStream stream(&file);
        outHtml4Subset(stream, root_element, max_image_width, ui->revealMask->isChecked());

        ui->statusBar->showMessage("Simple HTML4 SAVE complete.");
    }
    else
    {
        ui->statusBar->showMessage("Failed to open output file.");
    }

}
