#ifndef XMLELEMENT_H
#define XMLELEMENT_H

#include <QObject>
#include <QHash>
#include <QIODevice>
#include <QXmlStreamReader>
#include <QXmlStreamAttributes>

class XmlElement : public QObject
{
    Q_OBJECT
public:
    XmlElement(QObject *parent = 0);
    XmlElement(QXmlStreamReader*, QObject *parent = 0);

    static XmlElement *readTree(QIODevice*);
    QString p_text;

    void toHtml(QXmlStreamWriter &stream, bool multi_page, int max_image_width, bool use_reveal_mask) const;

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
};

#endif // XMLELEMENT_H
