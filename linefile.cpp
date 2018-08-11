#include "linefile.h"

//#include <QDebug>

/**
 * @class LineFile::LineFile
 *
 * This class is sub-classed from QFile and guarantees that no line in the file will be
 * longer than 1,000 characters in length.
 *
 * This helps reading XML files since otherwise some lines might be VERY long.
 */

LineFile::LineFile() : QFile() {}
LineFile::LineFile(const QString &name) : QFile(name) {}
LineFile::LineFile(QObject *parent) : QFile(parent) {}
LineFile::LineFile(const QString &name, QObject *parent) : QFile(name, parent) {}

// Ensure that no line in the file is more than 1,000 characters long.

qint64 LineFile::writeData(const char *data, qint64 maxsize)
{
    qint64 start  = 0;
    qint64 result = 0;
    //if (maxsize > 1000) qDebug() << "LineFile::writeData: maxsize =" << maxsize << ", current linelen" << linelen;
    for (qint64 i=0; i<maxsize; ++i)
    {
        if (data[i] == '\n')
        {
            //if (maxsize > 1000) qDebug() << "LineFile::writeData: found newline";
            result += QFile::writeData(&data[start], i - start);
            linelen = 0;
            start = i+1;
        }
        else if (++linelen == 1000)
        {
            //if (maxsize > 1000) qDebug() << "LineFile::writeData: hit 1000 limit";
            result += QFile::writeData(&data[start], i - start+1);
            result += QFile::writeData("\n", 1);
            linelen = 0;
            start = i+1;
        }
    }
    if (start < maxsize)
    {
        //if (maxsize > 1000) qDebug() << "LineFile::writeData: flushing remainder: size" << (maxsize - start);
        result += QFile::writeData(&data[start], maxsize - start);
    }

    // QXmlStreamWriter requires the result to be the same as passed in (not "result"),
    // even though we might inject some additional new-lines.
    return maxsize;
}
