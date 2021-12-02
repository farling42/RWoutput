#ifndef FG_CATEGORY_DELEGATE_H
#define FG_CATEGORY_DELEGATE_H

#include <QItemDelegate>

class FgCategoryDelegate : public QItemDelegate
{
    Q_OBJECT
public:
    explicit FgCategoryDelegate(QObject *parent = nullptr);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override;
    void setValues(const QStringList &values);
private:
    QStringList value_list;
};

#endif // FG_CATEGORY_DELEGATE_H
