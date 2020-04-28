#include "fileuploader.h"

#include <QDir>
#include <QFileInfo>

/**
 * @brief FileUploader::FileUploader
 * @param source The local file/directory to be uploaded
 * @param destination The destination directory (prefixed with ftp:// or http:// or https://)
 */
FileUploader::FileUploader(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    connect(m_manager, &QNetworkAccessManager::finished, this, &FileUploader::p_uploadFinished);
}

bool FileUploader::isBusy() const
{
    return m_is_busy;
}

bool FileUploader::uploadDirectory(const QString &source, const QString &destination)
{
    if (m_is_busy) return false;

    QDir dir(source);
    for (auto filename : dir.entryList(QDir::Files))
    {
        queue.enqueue(QFileInfo(dir,filename).filePath());
    }
    if (queue.isEmpty()) return false;

    m_is_busy = true;
    m_destination = destination;
    upload_one_file(queue.dequeue());
    return true;
}

bool FileUploader::uploadFile(const QString &source, const QString &destination)
{
    if (m_is_busy) return false;
    if (!QFile(source).exists()) return false;

    queue.enqueue(source);
    if (queue.isEmpty()) return false;

    m_is_busy = true;
    m_destination = destination;
    upload_one_file(queue.dequeue());
    return true;
}

void FileUploader::upload_one_file(const QString &source)
{
    m_file = new QFile(source);

    // Next, you need information about the file name
    // The upload path to the server should look like this
    // ftp://example.com/path/to/file/filename.txt
    // That is, we specify the protocol -> ftp
    // Server -> example.com
    // The path where the file will be located -> path/to/file/
    // And the name of the file itself, which we take from QFileInfo -> filename.txt
    QFileInfo fileInfo(*m_file);
    QUrl url(m_destination + fileInfo.fileName());
    url.setUserName("login");    // Set login
    url.setPassword("password"); // Set password
    //url.setPort(21);             // Protocol port, which we will work on

    if (m_file->open(QIODevice::ReadOnly))
    {
        // Start upload
        QNetworkReply *reply = m_manager->put(QNetworkRequest(url), m_file);
        // And connect to the progress upload signal
        connect(reply, &QNetworkReply::uploadProgress, this, &FileUploader::p_uploadProgress);
    }

}

void FileUploader::p_uploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
    // Display the progress of the upload
    emit uploadProgress(m_file->fileName(), bytesSent, bytesTotal);
}

void FileUploader::p_uploadFinished(QNetworkReply *reply)
{
    // If the upload was successful without errors
    if (reply->error())
    {
        queue.empty();
        emit uploadFailed(m_file->fileName());
        m_file->close();
        m_file->deleteLater();  // delete object of file
        m_file->close();
        return;
    }

    m_file->deleteLater();  // delete object of file
    reply->deleteLater();   // delete object of reply
    if (!queue.isEmpty())
    {
        upload_one_file(queue.dequeue());
    }
    else
    {
        emit uploadFinished();
    }
}
