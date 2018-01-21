#ifndef XMLELEMENT_H
#define XMLELEMENT_H

#include <QObject>
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

    QString toHtml() const;

private:
    QString p_element_title;
    QString p_topic_id;
    QString p_public_name;  // for topic
    QString p_section_name;
    QString p_snippet_type;
    QString p_asset_filename;
    QString p_linkage_id;
    QString p_linkage_name;
    QString p_linkage_direction;
    bool p_revealed;
    QXmlStreamAttributes p_attributes;
    void dump_tree(int indent);
};

#endif // XMLELEMENT_H
