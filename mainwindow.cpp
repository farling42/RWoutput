#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QSettings>
#include "xmlelement.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    root_element(0)
{
    ui->setupUi(this);
    // Set icons that can't be set in Designer.
    ui->loadFile->setIcon(style()->standardIcon(QStyle::SP_FileDialogStart));
    ui->saveHtml->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    // No options available until a file has been loaded.
    ui->saveHtml->setEnabled(false);
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

    QFile in_file(in_filename);
    if (!in_file.open(QFile::ReadOnly))
    {
        qWarning() << tr("Failed to find file") << in_file.fileName();
        return;
    }
    settings.setValue(LOAD_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath());

    ui->statusBar->showMessage("Loading RWoutput file...");
    root_element = XmlElement::readTree(&in_file);
    ui->statusBar->showMessage("RWoutput file LOAD complete.");

    ui->saveHtml->setEnabled(true);
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
        out_file.setFileName(out_directory + "/index.html");
    }
    else
    {
        // Enter a filename to generate a single massive file
        QString out_filename = QFileDialog::getSaveFileName(this, tr("HTML File"), /*dir*/ settings.value(SAVE_DIRECTORY_PARAM).toString(), /*template*/ tr("HTML Files (*.html)"));
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

    bool ok = true;
    int max_image_width = ui->maxImageWidth->currentText().toInt(&ok);
    if (!ok) max_image_width = -1;
    ui->statusBar->showMessage("Saving HTML file...");
    QXmlStreamWriter out_stream(&out_file);
    root_element->toHtml(out_stream, ui->oneTopicPerFile->isChecked(), max_image_width);
    ui->statusBar->showMessage("HTML file SAVE complete.");
}
