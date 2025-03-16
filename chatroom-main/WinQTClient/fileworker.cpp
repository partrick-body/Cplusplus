#include "fileworker.h"
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <QThread>

// 定义常量，用于表示千字节和兆字节的大小
#define KB 1024
#define MB (1024 * 1024)

// FileWorker类的构造函数，用于初始化类的成员变量
// ip: 服务器的IP地址
// port: 服务器的端口号
// filePath: 文件的路径
// isUpload: 是否为上传操作
// bytesReceived: 已接收的字节数
// filesize: 文件的总大小
FileWorker::FileWorker(QString &ip, quint16 port, const QString &filePath, bool isUpload, qint64 bytesReceived, size_t filesize)
    : ip(ip), port(port), filePath(filePath), isUpload(isUpload), bytesReceived(bytesReceived), filesize(filesize)
{
    // 初始化文件指针为nullptr
    file = nullptr;
}

// FileWorker类的析构函数，用于释放资源
FileWorker::~FileWorker()
{
    // 如果文件指针不为空
    if (file)
    {
        // 关闭文件
        file->close();
        // 删除文件对象
        delete file;
        // 将文件指针置为nullptr
        file = nullptr;
    }
    // 如果socket指针不为空
    if (socket)
    {
        // 断开与服务器的连接
        socket->disconnectFromHost();
        // 删除socket对象
        delete socket;
        // 将socket指针置为nullptr
        socket = nullptr;
    }
}

// 开始文件传输的函数
void FileWorker::startTransfer()
{
    // 如果是上传操作
    if (isUpload)
    {
        // 调用上传函数
        upload();
    }
    else
    {
        // 调用下载函数
        download();
    }
}

// 暂停文件传输的函数
void FileWorker::pauseTransfer()
{
    // 将暂停标志置为true
    pause = true;
}

// 恢复文件传输的函数
void FileWorker::resumeTransfer()
{
    // 将暂停标志置为false
    pause = false;
}

// 取消文件传输的函数
void FileWorker::cancelTransfer()
{
    // 如果文件指针不为空
    if (file)
    {
        // 关闭文件
        file->close();
        // 删除文件对象
        delete file;
        // 将文件指针置为nullptr
        file = nullptr;
    }
    // 发送传输失败的信号，提示传输已取消
//    socket->abort();
    emit transferFailed("Transfer canceled");
}


void FileWorker::upload()
{
    // 创建一个QFile对象，用于打开要上传的文件
    QFile file(filePath);
    // 以只读方式打开文件，如果打开失败
    if (!file.open(QIODevice::ReadOnly))
    {
        // 输出调试信息，提示文件打开失败
        qDebug() << "Failed to open file!";
        // 直接返回
        return;
    }

    // 创建一个QTcpSocket对象，用于与服务器建立连接
    socket = new QTcpSocket(this);
    // 如果socket创建失败
    if (!socket)
    {
        // 直接返回
        return;
    }
    // 连接到指定的服务器IP和端口
    socket->connectToHost(ip, port);
    // 如果连接超时或者连接状态不是已连接状态
    if (!socket->waitForConnected(3000) ||
        socket->state() != QAbstractSocket::ConnectedState)
    {
        // 删除socket对象
        delete socket;
        // 直接返回
        return;
    }
    // 获取文件的信息
    QFileInfo fileInfo(filePath);

    // 记录已发送的字节数
    qint64 bytesSent = 0;
    // 获取文件的总大小
    qint64 total = file.size();
    // 构造文件元信息，包含上传指令、文件名和文件大小
    QString fileMeta = QString("UPLOAD %1 %2").arg(fileInfo.fileName()).arg(file.size());
    // 输出文件元信息
    qDebug() << fileMeta;
    // 将文件元信息发送给服务器
    socket->write(fileMeta.toUtf8());

    // 确保文件元信息发送完成
    socket->waitForBytesWritten();

    // 等待服务器确认，避免数据丢失
    socket->waitForReadyRead(1000);

    // 设置socket的低延迟选项
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // 创建一个QElapsedTimer对象，用于计时
    timer = new QElapsedTimer();
    // 开始计时
    timer->start();

    // 线程休眠100毫秒
    QThread::msleep(100);

    // 当文件未读取到末尾且socket处于连接状态时
    while (!file.atEnd() && socket->state() == QAbstractSocket::ConnectedState)
    {
        // 从文件中读取4096字节的数据
        QByteArray chunk = file.read(4096);
        // 如果读取的数据为空
        if (chunk.isEmpty())
        {
            // 关闭文件
            file.close();
            // 发送传输失败的信号，提示读取的数据块为空
            emit transferFailed("Read Chunk is empty");
            // 直接返回
            return;
        }
        // 记录需要写入的字节数
        qint64 bytesToWrite = chunk.size();
        // 当还有字节需要写入时
        while (bytesToWrite > 0)
        {
            // 将数据写入socket
            qint64 bytesWritten = socket->write(chunk);
            // 如果写入超时
            if (!socket->waitForBytesWritten(30000))
            {
                // 发送传输失败的信号，提示写入超时
                emit transferFailed("Wait For bytes Written Timeout");
                // 直接返回
                return;
            }
            // 如果写入失败
            if (bytesWritten == -1)
            {
                // 发送传输失败的信号，提示写入错误及错误信息
                emit transferFailed("Write Error:" + socket->errorString());
                // 直接返回
                return;
            }
            // 减去已写入的字节数
            bytesToWrite -= bytesWritten;
            // 截取未写入的数据
            chunk = chunk.mid(bytesWritten);
            // 累加已发送的字节数
            bytesSent += bytesWritten;
        }

        // 计算已过去的时间（秒）
        double seconds = timer->elapsed() / 1000.0;
        // 用于存储速度信息的字符串
        QString speed;
        // 调用speedStr函数计算速度信息
        speedStr(bytesSent, seconds, speed);
        // 发送速度更新的信号
        emit speedUpdated(speed);
        // 计算上传进度
        int progress = static_cast<int>((bytesSent * 100) / total);
        // 发送进度更新的信号
        emit progressUpdated(progress);
//        qDebug() << "Progress: " << progress << "DEBUG 5 Speed: " << speed;
    }
    // 输出已发送的字节数
    qDebug() << "Haven Sent" << bytesSent;
    // 关闭socket连接
    socket->close();
    // 关闭文件
    file.close();
    // 发送传输完成的信号
    emit transferComplete();
}

// 下载文件的函数
void FileWorker::download()
{
    socket = new QTcpSocket(this);
    if (!socket)
        return;

    socket->connectToHost(ip, port);
    if (!socket->waitForConnected(5000) || socket->state() != QAbstractSocket::ConnectedState)
    {
        emit transferFailed("无法连接到服务器");
        delete socket;
        return;
    }

    QString header = QString("DOWNLOAD %1").arg(filePath);
    socket->write(header.toUtf8());
    socket->flush();

    // **等待服务器的文件大小响应**
    QByteArray response;
    while (response.isEmpty() && socket->waitForReadyRead(5000)) // **持续尝试读取，避免超时**
    {
        response.append(socket->readAll());
    }

    if (response.isEmpty())  // **如果服务器未响应，报错**
    {
        emit transferFailed("服务器无响应");
        return;
    }

    qDebug() << "收到服务器响应：" << response;

    if (response.startsWith("ERROR"))
    {
        emit transferFailed("服务器无法找到文件");
        return;
    }

    // **解析文件大小**
    QList<QByteArray> parts = response.split('\n');
    bool ok;
    qint64 filesize = parts[0].trimmed().toLongLong(&ok);
    if (!ok || filesize <= 0)
    {
        emit transferFailed("无效的文件大小: " + QString::number(filesize));
        return;
    }

    qDebug() << "服务器发送的文件大小：" << filesize;

    file = new QFile(filePath);
    if (!file->open(QIODevice::WriteOnly))
    {
        emit transferFailed("无法打开文件进行写入");
        return;
    }

    bytesReceived = 0;
    timer = new QElapsedTimer();
    timer->start();

    // **开始接收文件数据**
    while (bytesReceived < filesize)
    {
        if (!socket->waitForReadyRead(60000) && socket->bytesAvailable() == 0)
        {
            qDebug() << "等待超时，可能服务器断开连接";
            break;
        }

        // **逐步读取数据**
        QByteArray chunk = socket->read(qMin(filesize - bytesReceived, (qint64)65536));
        if (chunk.isEmpty())
        {
            qDebug() << "收到空数据，可能连接已断开";
            break;
        }

        qint64 bytesWritten = file->write(chunk);
        if (bytesWritten != chunk.size())
        {
            emit transferFailed("文件写入错误");
            break;
        }

        bytesReceived += bytesWritten;
        file->flush();
        // 计算已过去的时间（秒）
        double seconds = timer->elapsed() / 1000.0;
        // 用于存储速度信息的字符串
        QString speed;
        // 调用speedStr函数计算速度信息
        speedStr(bytesReceived, seconds, speed);
        // 计算下载进度
        int progress = static_cast<int>(bytesReceived * 100 / filesize);
        // 发送速度更新的信号
        emit speedUpdated(speed);
        // 发送进度更新的信号
        emit progressUpdated(progress);

        // **打印日志，观察进度**
        qDebug() << "下载进度：" << bytesReceived << "/" << filesize;

        // **延迟，防止过快读取数据**
       // QThread::msleep(10);
    }

    file->flush();
    file->close();
    socket->close();

    if (bytesReceived >= filesize)
    {
        emit transferComplete();
        qDebug() << "文件下载完成";
    }
    else
    {
        emit transferFailed("文件下载不完整，已接收 " + QString::number(bytesReceived) + " / " + QString::number(filesize));
        qDebug() << socket->errorString();
    }
}


// 计算速度信息的函数
// bytes: 已传输的字节数
// time: 已过去的时间（秒）
// str: 用于存储速度信息的字符串
void FileWorker::speedStr(qint64 bytes, double time, QString &str)
{
    // 如果字节数小于千字节
    if (bytes < KB)
    {
        // 以字节每秒的格式存储速度信息
        str = QString("Speed: %1 B/s").arg(bytes / time, 0, 'f', 2);
    }
    // 如果字节数小于兆字节
    else if (bytes < MB)
    {
        // 以千字节每秒的格式存储速度信息
        str = QString("Speed: %1 KB/s").arg(bytes / time / KB, 0, 'f', 2);
    }
    else
    {
        // 以兆字节每秒的格式存储速度信息
        str = QString("Speed: %1 MB/s").arg(bytes / time / MB, 0, 'f', 2);
    }
}
