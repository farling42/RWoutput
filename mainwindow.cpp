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
#include <QHeaderView>
#include <QIntValidator>
#include <QLineEdit>
#include <QSettings>
#include <QPrinter>
#include <QPdfWriter>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextEdit>
#include <QTextDocument>
#include <QDialogButtonBox>

#include "xmlelement.h"
#include "outputhtml.h"
#include "outhtml4subset.h"
#include "outputfgmod.h"
#include "outputmarkdown.h"
#include "mappinsdialog.h"
#include "fg_category_delegate.h"
#include "ui_obsidiandialog.h"

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
    ui->saveMarkdown->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    ui->savePdf->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    //ui->saveFgMod->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    //ui->print->setIcon(style()->standardIcon(QStyle::));
    ui->simpleHtml->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    // No options available until a file has been loaded.
    ui->rwexport->setEnabled(false);
    ui->rwoutput->setEnabled(false);
    if (QLineEdit *edit = qobject_cast<QLineEdit*>(ui->maxImageWidth))
        edit->setValidator(new QIntValidator(100,5000));

    connect(ui->separateTopicFiles, &QCheckBox::clicked, ui->indexOnEveryPage, &QCheckBox::setEnabled);
    ui->indexOnEveryPage->setEnabled(ui->separateTopicFiles->isChecked());

    QSettings settings;
    for (auto *widget : ui->centralWidget->findChildren<QCheckBox*>())
    {
        QVariant value = settings.value("checked/" + widget->objectName());
        if (value.isValid()) widget->setChecked(value.toBool());
    }
    QVariant value = settings.value("image/maxWidth");
    if (value.isValid()) ui->maxImageWidth->setText(value.toString());
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::saveSettings()
{
    QSettings settings;
    for (auto *widget : ui->centralWidget->findChildren<QCheckBox*>())
    {
        settings.setValue("checked/" + widget->objectName(), widget->isChecked());
    }
    settings.setValue("image/maxWidth", ui->maxImageWidth->text());
}

int MainWindow::maxWidth()
{
    QString value;
    if (QLineEdit *edit = qobject_cast<QLineEdit*>(ui->maxImageWidth))
        value = edit->text();
    //if (QComboBox *edit = qobject_cast<QComboBox*>(ui->maxImageWidth))
    //    value = ui->maxImageWidth->currentText();
    bool ok = true;
    int number = value.toInt(&ok);
    if (!ok) number = -1;
    return number;
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

    ui->rwexport->setEnabled(false);
    ui->rwoutput->setEnabled(false);
    ui->filename->setText(QFileInfo(in_file).fileName());

    setStatusText("Loading Realm Works File...");
    root_element = XmlElement::readTree(&in_file);
    setStatusText("Realm Works file LOAD complete.");

    ui->topicCount->setText(QString("Topic Count: %1").arg(root_element->findChildren<XmlElement*>("topic").count()));
    in_file.close();

    bool is_export = in_filename.endsWith(".rwexport");
    ui->rwexport->setEnabled(is_export);
    ui->rwoutput->setEnabled(!is_export);
    return true;
}

void MainWindow::on_loadFile_clicked()
{
    const QString LOAD_DIRECTORY_PARAM("inputDirectory");
    QSettings settings;
    QString in_filename = QFileDialog::getOpenFileName(this, tr("Realm Works Output File"), /*dir*/ settings.value(LOAD_DIRECTORY_PARAM).toString(), /*filter*/ tr("Realm Works® Files (*.rwoutput *.rwexport)"));
    if (in_filename.isEmpty()) return;

    XmlElement::setTranslateHtml(in_filename.endsWith("rwoutput"));
    if (loadFile(in_filename))
    {
        // Don't translate embedded HTML if we are processing RWEXPORT, it will be done AFTER link conversion.
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

    setStatusText("Saving XHTML file...");
    qApp->processEvents();

    toHtml(path, root_element,
           maxWidth(),
           separate_files,
           ui->revealMask->isChecked(),
           separate_files && ui->indexOnEveryPage->isChecked());

    setStatusText("XHTML file SAVE complete.");
    qApp->processEvents();
    saveSettings();
}

void MainWindow::on_obsidianPlugins_clicked()
{
    QSettings settings;

    QDialog dialog(this);
    Ui_ObsidianDialog obsidian;
    obsidian.setupUi(&dialog);

    // Set current settings
    foreach (auto *widget, dialog.findChildren<QCheckBox*>())
    {
        QVariant value = settings.value("obsidian/" + widget->objectName());
        if (value.isValid()) widget->setChecked(value.toBool());
    }

    // Remember current settings
    if (dialog.exec() == QDialog::Accepted)
    {
        foreach (auto *widget, dialog.findChildren<QCheckBox*>())
        {
            settings.setValue("obsidian/" + widget->objectName(), widget->isChecked());
        }
    }
}


void MainWindow::on_saveMarkdown_clicked()
{
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    // Choose a directory in which to generate all the files:
    // we'll create index.html in that directory
    QString path = QFileDialog::getExistingDirectory(this, tr("Output Directory"),
                                                     /*dir*/ settings.value(SAVE_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath()).toString());

    if (path.isEmpty()) return;
    QDir dir(path);

    if (!dir.exists())
    {
        setStatusText("The directory does not exist!");
        qApp->processEvents();
        return;
    }
    settings.setValue(SAVE_DIRECTORY_PARAM, dir.absolutePath());
    QDir::setCurrent(dir.absolutePath());

    setStatusText("Saving Markdown file...");
    qApp->processEvents();

    toMarkdown(root_element,
               maxWidth(),
               settings.value("obsidian/useLeaflet").toBool(),
               ui->revealMask->isChecked(),
               ui->foldersByCategory->isChecked(),
               ui->useWikilinks->isChecked(),
               ui->createNavPanel->isChecked(),
               ui->tagForEachPrefix->isChecked(),
               ui->tagForEachSuffix->isChecked(),
               settings.value("obsidian/useMermaid").toBool(),
               settings.value("obsidian/useDiceRollsSnippets").toBool(),
               settings.value("obsidian/useDiceRollsHtml").toBool(),
               ui->decodeStatblocks->isChecked(),
               settings.value("obsidian/use5estatblocks").toBool(),
               settings.value("obsidian/useAdmonitionGMdir").toBool(),
               settings.value("obsidian/useAdmonitionStyles").toBool(),
               settings.value("obsidian/fmLabeledText").toBool(),
               settings.value("obsidian/fmNumeric").toBool(),
               settings.value("obsidian/useInitiativeTracker").toBool(),
               settings.value("obsidian/useTableExtended").toBool(),
               settings.value("obsidian/createCategoryTemplates").toBool(),
               ui->linkPorFile->isChecked()
               );

    setStatusText("Markdown file SAVE complete.");
    qApp->processEvents();
    saveSettings();
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

    static QTextDocument doc;
    doc.clear();
    // Ensure layout uses correct margins (and hide page numbers)
    doc.setPageSize(printer.pageRect().size());

#ifndef GEN_TEXT_DOCUMENT
    setStatusText("Generating HTML4 subset contents...");
    {
        QString result;   // only keep long enough to call doc.setHtml
        QTextStream stream(&result, QIODevice::WriteOnly|QIODevice::Text);
        outHtml4Subset(stream, root_element, maxWidth(), ui->revealMask->isChecked());
        setStatusText("Transferring to QTextDocument...");
        doc.setHtml(result);
    }
#else
    genTextDocument(doc, root_element, maxWidth(), ui->revealMask->isChecked());
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

    static QTextDocument doc;
    doc.clear();
    // Ensure layout uses correct margins (and hide page numbers)
    doc.setPageSize(printer.pageRect().size());

    setStatusText("Generating HTML4 subset contents...");
    {
        QString result;   // only keep long enough to call doc.setHtml
        QTextStream stream(&result, QIODevice::WriteOnly|QIODevice::Text);
        outHtml4Subset(stream, root_element, maxWidth(), ui->revealMask->isChecked());
        setStatusText("Transferring to QTextDocument...");
        doc.setHtml(result);
    }

    setStatusText("Printing...");
    doc.print(&printer);

    setStatusText("Print complete.");
    saveSettings();
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

    setStatusText("Saving HTML4 file...");
    QTextStream stream(&file);
    outHtml4Subset(stream, root_element, maxWidth(), ui->revealMask->isChecked());

    setStatusText("Simple HTML4 SAVE complete.");
    saveSettings();
}

void MainWindow::on_mapPins_clicked()
{
    QSettings settings;
    QVariant value;

    value = settings.value("pins/title");
    if (value.isValid()) map_pin_title = value.toString();
    value = settings.value("pins/description");
    if (value.isValid()) map_pin_description = value.toString();
    value = settings.value("pins/gmDirections");
    if (value.isValid()) map_pin_gm_directions = value.toString();

    MapPinsDialog dialog(map_pin_title, map_pin_description, map_pin_gm_directions,
                         map_pin_title_default, map_pin_description_default, map_pin_gm_directions_default,
                         this);
    if (dialog.exec() == QDialog::Accepted)
    {
        // Read values from the dialog
        map_pin_title = dialog.titleTemplate();
        map_pin_description = dialog.descriptionTemplate();
        map_pin_gm_directions = dialog.gmDirectionsTemplate();

        // Save for next time
        settings.setValue("pins/title",        map_pin_title);
        settings.setValue("pins/description",  map_pin_description);
        settings.setValue("pins/gmDirections", map_pin_gm_directions);
    }
}

void MainWindow::on_saveFgMod_clicked()
{
    //
    // Get the mapping of category_name to FG section
    //
    QMultiMap<QString,const XmlElement*> cat_map;
    for (const XmlElement *topic : root_element->findChildren<XmlElement*>("topic"))
    {
        QString cat = topic->attribute("category_name");
        if (!cat.isEmpty()) cat_map.insert(cat, topic);
    }
    // Get a list of unique category names
    QList<QString> cat_names = cat_map.uniqueKeys();

    QStringList fg_cats{"battle","battlerandom","charsheet","encounter","effects","image","item","library","modifiers",
                        "notes","npc","quest","storytemplate","tables","treasureparcels" };
    fg_cats.sort();

    // Get mapping from RW category to FG name.
    QStandardItemModel cat_model;
    cat_model.setHorizontalHeaderLabels(QStringList{"RW Category","FG section"});
    for (auto cat : cat_names)
    {
        QList<QStandardItem*> row;
        row.append(new QStandardItem(cat));
        row.append(new QStandardItem("library"));
        cat_model.appendRow(row);
    }

    // Present model to allow mappings to be changed
    QTableView *view = new QTableView;
    view->setModel(&cat_model);
    FgCategoryDelegate *delegate = new FgCategoryDelegate;
    delegate->setValues(fg_cats);
    view->setItemDelegateForColumn(1, delegate);
    view->resizeColumnsToContents();
    view->setSizeAdjustPolicy(QTableView::AdjustToContents);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->verticalHeader()->hide();

    QDialog dialog;
    dialog.setModal(true);
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(view);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setLayout(layout);
    if (dialog.exec() == QDialog::Rejected)
    {
        return;
    }

    // Read required mapping
    QMap<QString,QString> rw_to_fg;
    while (cat_model.rowCount() > 0)
    {
        auto row = cat_model.takeRow(0);
        rw_to_fg.insert(row[0]->text(), row[1]->text());
        qDeleteAll(row);
    }
    // Convert cat_map (RW category) to section_to_topic (FG section)
    QMap<QString,const XmlElement *> section_to_topic;
    for (auto topic : cat_map)
    {
        section_to_topic.insert(rw_to_fg.value(topic->attribute("category_name")), topic);
    }

    //
    // Get the SAVE filename
    //
    QSettings settings;
    const QString SAVE_DIRECTORY_PARAM("outputDirectory");

    // Choose a directory in which to generate all the files:
    // we'll create index.html in that directory
    QString start_dir = settings.value(SAVE_DIRECTORY_PARAM, QFileInfo(in_file).absolutePath()).toString();
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"),
                                           /*dir*/ start_dir + "/" + QFileInfo(in_file).baseName() + ".mod",
                                           /*filter*/ "MOD files (*.mod)");

    if (path.isEmpty()) return;
    QDir dir(QFileInfo(path).absolutePath());

    if (!dir.exists())
    {
        setStatusText("The directory does not exist!");
        qApp->processEvents();
        return;
    }
    settings.setValue(SAVE_DIRECTORY_PARAM, dir.absolutePath());
    QDir::setCurrent(dir.absolutePath());

    //
    // Perform the actual conversion
    //
    setStatusText("Saving MOD file...");
    qApp->processEvents();

    toFgMod(path, root_element,
            section_to_topic,
            ui->revealMask->isChecked());

    setStatusText("MOD file SAVE complete.");
    qApp->processEvents();
    saveSettings();
}
