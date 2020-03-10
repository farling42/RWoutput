#include "mappinsdialog.h"
#include "ui_mappinsdialog.h"

MapPinsDialog::MapPinsDialog(const QString &titleTemplate, const QString &descriptionTemplate, const QString &gmDirectionsTemplate,
                             const QString &title_default, const QString &description_default, const QString &gmDirections_default,
                             QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MapPinsDialog),
    title_default(title_default),
    description_default(description_default),
    gmDirections_default(gmDirections_default)
{
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    ui->titleTemplate->setText(titleTemplate);
    ui->descriptionTemplate->setText(descriptionTemplate);
    ui->gmDirectionsTemplate->setText(gmDirectionsTemplate);
}

MapPinsDialog::~MapPinsDialog()
{
    delete ui;
}

QString MapPinsDialog::titleTemplate() const
{
    return ui->titleTemplate->toPlainText();
}

QString MapPinsDialog::descriptionTemplate() const
{
    return ui->descriptionTemplate->toPlainText();
}

QString MapPinsDialog::gmDirectionsTemplate() const
{
    return ui->gmDirectionsTemplate->toPlainText();
}

void MapPinsDialog::on_defaultTitle_clicked()
{
    ui->titleTemplate->setText(title_default);
}

void MapPinsDialog::on_defaultDescription_clicked()
{
    ui->descriptionTemplate->setText(description_default);
}

void MapPinsDialog::on_defaultGmDirections_clicked()
{
    ui->gmDirectionsTemplate->setText(gmDirections_default);
}
