#ifndef OUTHTML4SUBSET_H
#define OUTHTML4SUBSET_H

#include <QString>
#include <QTextStream>
#include "xmlelement.h"

void outHtml4Subset(QTextStream &stream,
                    const XmlElement *root,
                    int max_image_width,
                    bool use_reveal_mask);

#endif // OUTHTML4SUBSET_H
