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

    void toHtml(QXmlStreamWriter *stream, bool multi_page, int max_image_width, bool use_reveal_mask) const;

    bool hasAttribute(const QString &name) const;
    QString attribute(const QString &name) const;

    QList<XmlElement *> xmlChildren(const QString &name = QString()) const { return findChildren<XmlElement*>(name, Qt::FindDirectChildrenOnly); }
    QList<XmlElement *> childrenWithAttributes(const QString &attribute, const QString &value = QString()) const;
    XmlElement *xmlChild(const QString &name = QString()) const { return findChild<XmlElement*>(name, Qt::FindDirectChildrenOnly); }

private:
    // objectName == XML element title
    struct Attribute {
        QString name;
        QString value;
        Attribute(const QString &name, const QString &value) : name(name), value(value) {}
    };
    QString p_fixed_text;
    QList<Attribute> p_attributes;
    void dump_tree() const;
    void writeHtml(QXmlStreamWriter *writer) const;
    void writeTopic(QXmlStreamWriter *stream) const;
    void writeAttributes(QXmlStreamWriter *stream) const;
    struct Linkage {
        QString name;
        QString id;
        Linkage(const QString &name, const QString &id) : name(name), id(id) {}
    };
    typedef QList<Linkage> LinkageList;
    void writeSnippet(QXmlStreamWriter *stream, const LinkageList &links) const;
    void writeSection(QXmlStreamWriter *stream, const LinkageList &links) const;
    void writeSpan(QXmlStreamWriter *stream, const LinkageList &links, const QString &classname) const;
    void writePara(QXmlStreamWriter *stream, const QString &classname, const LinkageList &links, const QString &prefix = QString(), const QString &value = QString()) const;
    void writeParaChildren(QXmlStreamWriter *stream, const QString &classname, const LinkageList &links, const QString &prefix = QString(), const QString &value = QString()) const;
    int writeImage(QXmlStreamWriter *stream, const LinkageList &links, const QString &image_name,
                   const QString &base64_data, XmlElement *mask_elem,
                   const QString &filename, XmlElement *annotation, const QString &usemap = QString()) const;
    void writeExtObject(QXmlStreamWriter *stream, const LinkageList &links, const QString &prefix,
                        const QString &base64_data, const QString &filename, XmlElement *annotation) const;
    QString snippetName() const;
    XmlElement(const QString &fixed_text, QObject *parent);
    QString childString() const;
    void writeChildren(QXmlStreamWriter *stream, const QString &classname, const LinkageList &links, const QString &first_label, const QString &first_bodytext) const;
};

#endif // XMLELEMENT_H
