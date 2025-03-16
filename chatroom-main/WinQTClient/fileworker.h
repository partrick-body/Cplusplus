#ifndef FILEWORKER_H
#define FILEWORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QElapsedTimer>

class FileWorker : public QObject
{
    Q_OBJECT
public:
    explicit FileWorker(QString &ip, quint16 port, const QString &filePath, bool isUpload, qint64 bytesReceived=0,size_t filesize = 0);
    ~FileWorker();
signals:
    void progressUpdated(int value);
    void speedUpdated(QString speed);
    void transferComplete();
    void transferFailed(const QString &errorMsg);
public slots:
    void startTransfer();
    void pauseTransfer();
    void resumeTransfer();
    void cancelTransfer();
private:

    bool pause = false;
    void upload();
    void download();
    QTcpSocket *socket;
    QString ip;
    quint16 port;
    QString filePath;
    bool isUpload;
    QFile *file;
    QElapsedTimer *timer;
    qint64 bytesReceived;
    size_t filesize;

    void speedStr(qint64 bytes, double time, QString &str);
};

#endif // FILEWORKER_H
