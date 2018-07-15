#ifndef LINKAGE_H
#define LINKAGE_H

#include <QMap>

class LinkageList
{
private:
    QMap<QString, QString> map;
    int maxlen{0};

public:
    void clear()
    {
        map.clear();
        maxlen = 0;
    }

    void add(const QString &name, const QString &id)
    {
        if (name.length() > maxlen) maxlen = name.length();
        map.insert(name.toUpper(), id);
    }

    QString find(const QString &text) const
    {
        static const QString null_string;
        if (!map.isEmpty() && text.length() <= maxlen)
        {
            return map.value(text.toUpper(), null_string);
        }
        return null_string;
    }
};

#endif // LINKAGE_H
