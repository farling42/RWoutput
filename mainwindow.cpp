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

#undef GEN_TEXT_DOCUMENT

#include <QDebug>
#include <QFileDialog>
#include <QSettings>
#include <QPrinter>
#include <QPdfWriter>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QTextEdit>
#include <QTextDocument>

#include "xmlelement.h"
#include "outputhtml.h"
#include "outhtml4subset.h"
#ifdef GEN_TEXT_DOCUMENT
#include "gentextdocument.h"
#endif

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


void MainWindow::setStatusText(const QString &text)
{
    ui->statusBar->showMessage(text);
    // Ensure it is drawn immediately
    qApp->processEvents();
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

    setStatusText("Loading RWoutput file...");
    root_element = XmlElement::readTree(&in_file);
    setStatusText("RWoutput file LOAD complete.");

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
        setStatusText("The directory does not exist!");
        qApp->processEvents();
        return;
    }
    settings.setValue(SAVE_DIRECTORY_PARAM, dir.absolutePath());
    QDir::setCurrent(dir.absolutePath());

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;
    setStatusText("Saving XHTML file...");
    qApp->processEvents();

    toHtml(path, root_element,
           max_image_width,
           separate_files,
           ui->revealMask->isChecked(),
           separate_files && ui->indexOnEveryPage->isChecked());

    setStatusText("XHTML file SAVE complete.");
    qApp->processEvents();
}

void MainWindow::on_savePdf_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    // Firstly get the page properties.
    QPrinter printer;
    QPageSetupDialog page_props(&printer, this);
    if (page_props.exec() != QDialog::Accepted) return;

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
    if (!file.open(QFile::WriteOnly))
    {
        setStatusText("Failed to open PDF file.");
        return;
    }

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;

    static QTextDocument doc;
    doc.clear();
    // Ensure layout uses correct margins (and hide page numbers)
    doc.setPageSize(printer.pageRect().size());

#ifndef GEN_TEXT_DOCUMENT
    setStatusText("Generating HTML4 subset contents...");
    {
        QString result;   // only keep long enough to call doc.setHtml
        QTextStream stream(&result, QIODevice::WriteOnly|QIODevice::Text);
        outHtml4Subset(stream, root_element, max_image_width, ui->revealMask->isChecked());
        setStatusText("Transferring to QTextDocument...");
        doc.setHtml(result);
    }
#else
    genTextDocument(doc, root_element, max_image_width, ui->revealMask->isChecked());
#endif

    setStatusText("Saving PDF file...");
    QPdfWriter pdf(&file);
    pdf.setCreator("RWout");
    pdf.setTitle(QFileInfo(in_file).baseName());
    pdf.setPdfVersion(QPdfWriter::PdfVersion_1_6);  /* Allows Embedded fonts, rather than linked */
    pdf.setPageLayout(printer.pageLayout());
    doc.print(&pdf);
    setStatusText("PDF file SAVE complete.");
}

void MainWindow::on_print_clicked()
{
    // Request all properties for the printer.
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;

    static QTextDocument doc;
    doc.clear();
    // Ensure layout uses correct margins (and hide page numbers)
    doc.setPageSize(printer.pageRect().size());

    setStatusText("Generating HTML4 subset contents...");
    {
        QString result;   // only keep long enough to call doc.setHtml
        QTextStream stream(&result, QIODevice::WriteOnly|QIODevice::Text);
        outHtml4Subset(stream, root_element, max_image_width, ui->revealMask->isChecked());
        setStatusText("Transferring to QTextDocument...");
        doc.setHtml(result);
    }

    setStatusText("Printing...");
    doc.print(&printer);

    setStatusText("Print complete.");
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
    if (!file.open(QFile::WriteOnly|QFile::Text))
    {
        setStatusText("Failed to open output file.");
        return;
    }

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;

    setStatusText("Saving HTML4 file...");
    QTextStream stream(&file);
    outHtml4Subset(stream, root_element, max_image_width, ui->revealMask->isChecked());

    setStatusText("Simple HTML4 SAVE complete.");

}
