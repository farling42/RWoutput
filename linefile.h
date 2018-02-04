#ifndef LINEFILE_H
#define LINEFILE_H

#include <QFile>

class LineFile : public QFile
{
    Q_OBJECT
public:
    LineFile();
    LineFile(const QString &name);
    LineFile(QObject *parent);
    LineFile(const QString &name, QObject *parent);

protected:
    virtual qint64 writeData(const char *data, qint64 len);
private:
    void init();
    qint64 linelen = 0;
};

#endif // LINEFILE_H
