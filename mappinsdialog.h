#ifndef MAPPINSDIALOG_H
#define MAPPINSDIALOG_H

#include <QDialog>

namespace Ui {
class MapPinsDialog;
}

class MapPinsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MapPinsDialog(const QString &titleTemplate, const QString &descriptionTemplate, const QString &gmDirectionsTemplate,
                           const QString &titleDefault, const QString &descriptionDefault, const QString &gmDirectionsDefault,
                           QWidget *parent = nullptr);
    ~MapPinsDialog();

    QString titleTemplate() const;
    QString descriptionTemplate() const;
    QString gmDirectionsTemplate() const;

private slots:
    void on_defaultTitle_clicked();
    void on_defaultDescription_clicked();
    void on_defaultGmDirections_clicked();

private:
    Ui::MapPinsDialog *ui;
    QString title_default;
    QString description_default;
    QString gmDirections_default;
};

#endif // MAPPINSDIALOG_H
