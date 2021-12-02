#ifndef OUTPUTFGMOD_H
#define OUTPUTFGMOD_H

#include <QMap>
class QString;
class XmlElement;

void toFgMod(const QString &path,
             const XmlElement *root_elem,
             const QMap<QString,const XmlElement*> section_topics,
             bool use_reveal_mask);

#endif // OUTPUTFGMOD_H
