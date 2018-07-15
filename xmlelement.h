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

#ifndef XMLELEMENT_H
#define XMLELEMENT_H

#include <QObject>
#include <QHash>
#include <QIODevice>
#include <QXmlStreamReader>
#include <QXmlStreamAttributes>
#include "gumbo.h"

class XmlElement : public QObject
{
    Q_OBJECT
public:
    XmlElement(QXmlStreamReader*, QObject *parent = nullptr);
    XmlElement(GumboNode *info, QObject *parent);

    // objectName == XML element title
    struct Attribute {
        const QString name;
        const QString value;
        Attribute() {}
        Attribute(const char *name, const char *value) : name(name), value(value) {}
        Attribute(const QStringRef &name, const QStringRef &value) : name(name.toString()), value(value.toString()) {}
    };

    static XmlElement *readTree(QIODevice*);

    bool hasAttribute(const QString &name) const;
    const QString &attribute(const QString &name) const;
    inline bool isFixedString() const { return is_fixed_text; }
    inline const QString fixedText() const { return QString(p_byte_data); }
    inline const QByteArray &byteData() const { return p_byte_data; }
    inline const QVector<Attribute> &attributes() const { return p_attributes; }

    inline QList<XmlElement *> xmlChildren(const QString &name = QString()) const { return findChildren<XmlElement*>(name, Qt::FindDirectChildrenOnly); }
    inline XmlElement *xmlChild(const QString &name = QString()) const { return findChild<XmlElement*>(name, Qt::FindDirectChildrenOnly); }

    void dump_tree() const;
    QString snippetName() const;
    QString childString() const;
private:
    XmlElement(const QByteArray &fixed_text, QObject *parent);
    void parse_gumbo_nodes(GumboNode *node);
    // Real data is...
    QByteArray p_byte_data;
    QVector<Attribute> p_attributes;
    const bool is_fixed_text{false};
};

#endif // XMLELEMENT_H
