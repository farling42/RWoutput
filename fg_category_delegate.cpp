#include "fg_category_delegate.h"

#include <QComboBox>

FgCategoryDelegate::FgCategoryDelegate(QObject *parent) :
    QItemDelegate(parent)
{
}

void FgCategoryDelegate::setValues(const QStringList &values)
{
    value_list = values;
}

QWidget *FgCategoryDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QComboBox *editor = new QComboBox(parent);
    editor->addItems(value_list);
    return editor;
}

void FgCategoryDelegate::setEditorData(QWidget *editor,
                                       const QModelIndex &index) const
{
    QComboBox *comboBox = static_cast<QComboBox*>(editor);
    comboBox->setCurrentText(index.model()->data(index, Qt::EditRole).toString());
}

void FgCategoryDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                      const QModelIndex &index) const
{
    QComboBox *comboBox = static_cast<QComboBox*>(editor);
    model->setData(index, comboBox->currentText(), Qt::EditRole);
}

void FgCategoryDelegate::updateEditorGeometry(QWidget *editor,
                                              const QStyleOptionViewItem &option, const QModelIndex &/* index */) const
{
    editor->setGeometry(option.rect);
}
