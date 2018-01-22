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
    //settings.setValue(LOAD_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath());

    root_element = XmlElement::readTree(&in_file);

    ui->saveHtml->setEnabled(true);
    qInfo() << "LOAD FILE complete";
}

void MainWindow::on_saveHtml_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");
    QString out_filename = QFileDialog::getSaveFileName(this, tr("HTML File"), /*dir*/ settings.value(SAVE_DIRECTORY_PARAM).toString(), /*template*/ tr("HTML Files (*.html)"));
    if (out_filename.isEmpty()) return;

    QFile out_file(out_filename);
    if (!out_file.open(QFile::WriteOnly))
    {
        qWarning() << tr("Failed to find file") << out_file.fileName();
        return;
    }
    settings.setValue(SAVE_DIRECTORY_PARAM, QFileInfo(out_file).absolutePath());

    QTextStream out_stream(&out_file);
    out_stream << root_element->toHtml();
    qInfo() << "SAVE HTML complete";
}
