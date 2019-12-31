#ifndef GENTEXTDOCUMENT_H
#define GENTEXTDOCUMENT_H

#include <QString>
#include <QTextDocument>
#include "xmlelement.h"

void genTextDocument(QTextDocument &doc,
                    const XmlElement *root,
                    int max_image_width,
                    bool use_reveal_mask);

#endif // OUTHTML4SUBSET_H
