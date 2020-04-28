#ifndef FILEUPLOADER_H
#define FILEUPLOADER_H

#include <QObject>
#include <QFile>
#include <QQueue>
#include <QtNetwork/QNetworkReply>

class FileUploader : public QObject
{
    Q_OBJECT
public:
    explicit FileUploader(QObject *parent = nullptr);
    bool isBusy() const;
public slots:
    bool uploadFile(const QString &source, const QString &destination);
    bool uploadDirectory(const QString &source, const QString &destination);
signals:
    void uploadProgress(const QString &filename, qint64 bytesSent, qint64 bytesTotal);
    void uploadFailed(const QString &filename);
    void uploadFinished();
private slots:
    void upload_one_file(const QString &source);
    void p_uploadFinished(QNetworkReply *reply);  // Upload finish slot
    void p_uploadProgress(qint64 bytesSent, qint64 bytesTotal);  // Upload progress slot
private:
    bool m_is_busy{false};
    QNetworkAccessManager *m_manager{nullptr};
    QString m_fileName;
    // You must save the file on the heap
    // If you create a file object on the stack, the program will crash.
    QFile *m_file{nullptr};
    QQueue<QString> queue;
    QString m_destination;
};

#endif // FILEUPLOADER_H
