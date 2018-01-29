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
    XmlElement(QXmlStreamReader*, QObject *parent = 0);
    XmlElement(GumboNode *info, QObject *parent);

    static XmlElement *readTree(QIODevice*);

    bool hasAttribute(const QString &name) const;
    QString attribute(const QString &name) const;
    bool isFixedString() const;
    inline QString fixedText() const { return p_fixed_text; }

    QList<XmlElement *> xmlChildren(const QString &name = QString()) const { return findChildren<XmlElement*>(name, Qt::FindDirectChildrenOnly); }
    QList<XmlElement *> childrenWithAttributes(const QString &attribute, const QString &value = QString()) const;
    XmlElement *xmlChild(const QString &name = QString()) const { return findChild<XmlElement*>(name, Qt::FindDirectChildrenOnly); }

    // objectName == XML element title
    struct Attribute {
        QString name;
        QString value;
        Attribute(const QString &name, const QString &value) : name(name), value(value) {}
    };
    QString p_fixed_text;
    QByteArray p_byte_data;
    QList<Attribute> p_attributes;
    void dump_tree() const;
    QString snippetName() const;
    QString childString() const;
private:
    XmlElement(const QString &fixed_text, QObject *parent);
};

#endif // XMLELEMENT_H
