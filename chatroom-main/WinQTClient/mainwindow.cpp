#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <thread>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    chathistory = ui->textEdit_chat_history;
    input = ui->lineEdit_inputbox;
    ipEdit = ui->lineEdit_ip;
    portEdit = ui->lineEdit_port;
    userEdit = ui->lineEdit_user;
    connectBtn = ui->btn_connect;
    sendBtn = ui->btn_send;
    closeBtn = ui->btn_close;
    downloadBtn = ui->btn_accept;
    uploadBtn = ui->btn_upload;
    cancelBtn = ui->btn_cancel;
    filenameLb = ui->label_filename;
    filesizeLb = ui->label_filesize;
    speedLb = ui->label_speed;
    progressBar = ui->progressBar;
    tableList = ui->table_userlist;

    statusBar()->showMessage("Not connected to server");
    sendBtn->setDisabled(true);
    progressBar->setVisible(false);
    workerThread = nullptr;
}

MainWindow::~MainWindow()
{
    if (client_sock) {
        client_sock->disconnectFromHost();
        delete client_sock;
    }
    if (workerThread)
    {
        workerThread->quit();
        workerThread->wait();
        delete workerThread;
    }
    if (fileworker)
    {
        delete fileworker;
    }
    delete ui;
}

void MainWindow::on_btn_connect_clicked()
{
    client_sock = new QTcpSocket(this);
    ip = ipEdit->text();
    port = portEdit->text().toUInt();
    username = userEdit->text();
    connect(client_sock, &QTcpSocket::readyRead, this, &MainWindow::receiveMsg);
    connect(client_sock, &QTcpSocket::connected, this, &MainWindow::onConnected);
    connect(client_sock, &QTcpSocket::disconnected, this, &MainWindow::onDisconnected);
    connect(client_sock, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &MainWindow::onError);
    client_sock->connectToHost(ip, port);
    if (!client_sock->waitForConnected(3000))
    {
        QMessageBox::critical(this, "Error", "Failed to connect to server");
    }
    else
    {
        connect(sendBtn, &QPushButton::clicked, this, &MainWindow::sendMsg);
        QMessageBox::information(this, "Connected", "Connected to server Success");
        connectBtn->setDisabled(true);
        getUserList();
    }
    qDebug() << "Connected Success!\n";
    sendBtn->setDisabled(false);
    connect(uploadBtn, &QPushButton::clicked, this, &MainWindow::upload);
    connect(downloadBtn, &QPushButton::clicked, this, &MainWindow::download);
    connect(cancelBtn, &QPushButton::clicked, this, &MainWindow::cancel);
}

void MainWindow::sendMsg()
{
    if (!client_sock->isOpen())
    {
        chathistory->append("Error: Not connected to server");
        return;
    }

    QString message = u8"[" + username + "]: " + input->text().trimmed();
    if (!message.isEmpty())
    {
        client_sock->write(message.toUtf8());
        input->clear();
    }
}

void MainWindow::receiveMsg()
{
    QByteArray data = client_sock->readAll();
    QString msg = QString::fromUtf8(data);
    if (msg.startsWith("FILE"))
    {
        filenameLb->setText(msg.split(" ")[1].trimmed());
        filesizeLb->setText(msg.split(" ")[2].trimmed());
        downloadFileSize = filesizeLb->text().toULong();
        downloadBtn->setEnabled(true);
    }
    else if (msg.startsWith("USERLIST"))
    {
        QString ipUserStr = msg.split(" ")[1];
        QStringList list = ipUserStr.split("\n");
        qDebug() << ipUserStr;
        tableList->clear();
        tableList->setColumnCount(2);
        tableList->setRowCount(list.size());
        tableList->setItem(0,0, new QTableWidgetItem("用户"));
        tableList->setItem(0,1, new QTableWidgetItem("IP"));
        for (int i = 0; i < list.size() - 1; i++)
        {
            QString ipaddr = list[i].split(":")[0];
            QString username = list[i].split(":")[1];
            qDebug() << ipaddr << username;
            tableList->setItem(i + 1, 0, new QTableWidgetItem(username));
            tableList->setItem(i + 1, 1, new QTableWidgetItem(ipaddr));
        }
    }
    else
    {
        chathistory->append(msg);
    }
}

void MainWindow::onConnected()
{
    statusBar()->showMessage("连接成功");
    QString greeting = username + u8" 进入了群聊";
    client_sock->write(greeting.toUtf8());
}

void MainWindow::onDisconnected()
{
    statusBar()->showMessage("断开链接");
    chathistory->append("Disconnected from the server.");
    connectBtn->setEnabled(true);
}

void MainWindow::onError(QAbstractSocket::SocketError sockErr)
{
    Q_UNUSED(sockErr);
    statusBar()->showMessage("Connection error: " + client_sock->errorString());
    chathistory->append("Error: " + client_sock->errorString());
}

void MainWindow::on_btn_close_clicked()
{
    this->close();
}

void MainWindow::upload()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to upload");
    if (filePath.isEmpty()) return;
    QFileInfo file(filePath);
    filenameLb->setText(file.fileName());

    cancelBtn->setEnabled(true);
    progressBar->setVisible(true);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);


    workerThread = new QThread(this);
    fileworker = new FileWorker(ip, port, filePath, true);
    fileworker->moveToThread(workerThread);
    connect(workerThread, &QThread::started, fileworker, &FileWorker::startTransfer);
    connect(fileworker, &FileWorker::progressUpdated, this, &MainWindow::updateProgressBar);
    connect(fileworker, &FileWorker::speedUpdated, this, &MainWindow::updateSpeed);
    connect(this, &MainWindow::cancelTransfer, fileworker, &FileWorker::cancelTransfer);
    connect(fileworker, &FileWorker::transferComplete, this, &MainWindow::onTransferComplete);
    connect(fileworker, &FileWorker::transferFailed, this, &MainWindow::onTransferFailed);

    workerThread->start();
}

void MainWindow::cancel()
{
    if (fileworker)
    {
        emit cancelTransfer();
    }
    if (file)
    {
        file->close();
        file->deleteLater();
        file = nullptr;
    }
    if (workerThread) workerThread->quit();
    progressBar->setValue(0);
    cancelBtn->setEnabled(false);
    QMessageBox::information(this, "Transfer", "Transfer canceled");
}

void MainWindow::download()
{
    QString filename = filenameLb->text().trimmed();
    if (filename.isEmpty())
    {
        QMessageBox::critical(this, "Error", "No file ready to download");
        return;
    }

    cancelBtn->setEnabled(true);
    progressBar->setVisible(true);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);

    workerThread = new QThread();
    fileworker = new FileWorker(ip, port, filename, false, downloadFileSize);
    fileworker->moveToThread(workerThread);

    connect(workerThread, &QThread::started, fileworker, &FileWorker::startTransfer);
    connect(fileworker, &FileWorker::progressUpdated, this, &MainWindow::updateProgressBar);
    connect(fileworker, &FileWorker::speedUpdated, this, &MainWindow::updateSpeed);
    connect(this, &MainWindow::cancelTransfer, fileworker, &FileWorker::cancelTransfer);
    connect(fileworker, &FileWorker::transferComplete, this, &MainWindow::onTransferComplete);
    connect(fileworker, &FileWorker::transferFailed, this, &MainWindow::onTransferFailed);

    workerThread->start();
}

void MainWindow::updateProgressBar(int progress)
{
    progressBar->setValue(progress);
}

void MainWindow::updateSpeed(QString speed)
{
    speedLb->setText(speed);
}

void MainWindow::onTransferComplete()
{
    QMessageBox::information(this, "Transfer Complete", "File transfer completed successful.");
    workerThread->quit();
    progressBar->setValue(100);
    cancelBtn->setEnabled(false);
}

void MainWindow::onTransferFailed(const QString &errorMsg)
{
    QMessageBox::critical(this, "Transfer Failed", errorMsg);
    workerThread->quit();
    cancelBtn->setEnabled(false);
    progressBar->setValue(0);
}

void MainWindow::getUserList()
{
    if (client_sock->state() == QAbstractSocket::ConnectedState)
    {
        QString request = "USERLIST";
        client_sock->write(request.toUtf8());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QString wave = username + u8" 退出了群聊";
    if (client_sock->isOpen()) client_sock->write(wave.toUtf8());
}

void MainWindow::on_lineEdit_ip_textChanged(const QString &arg1)
{

}

