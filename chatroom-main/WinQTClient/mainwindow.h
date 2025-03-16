#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QTcpSocket>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QFile>
#include <QProgressBar>
#include <QLabel>
#include <QTableWidget>
#include <QStatusBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include "fileworker.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
signals:
    void transferFinished();
    void cancelTransfer();
private slots:
    void on_lineEdit_ip_textChanged(const QString &arg1);

    void on_btn_connect_clicked();

    void sendMsg();
    void receiveMsg();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError sockErr);
    void on_btn_close_clicked();

    void upload();
    void download();
    void cancel();

    void updateProgressBar(int progress);
    void updateSpeed(QString speed);

    void onTransferComplete();
    void onTransferFailed(const QString &errorMsg);

    void getUserList();

protected:
    void closeEvent(QCloseEvent *event) override;
private:
    Ui::MainWindow *ui;
    QTcpSocket *client_sock;
    QTcpSocket *file_sock;
    QTextEdit *chathistory;
    QLineEdit *input;
    QLineEdit *ipEdit;
    QLineEdit *userEdit;
    QLineEdit *portEdit;
    QPushButton *connectBtn;
    QPushButton *sendBtn;
    QPushButton *closeBtn;
    QPushButton *downloadBtn;
    QPushButton *uploadBtn;
    QPushButton *cancelBtn;
    QLabel *filenameLb;
    QLabel *filesizeLb;
    QLabel *speedLb;
    QProgressBar *progressBar;
    QTableWidget *tableList;
    QFile *file = nullptr;
    QThread *workerThread;
    FileWorker *fileworker;
    QString ip;
    quint16 port;
    QString username;
    bool isUpload;
    QElapsedTimer *timer;
    size_t downloadFileSize;
};
#endif // MAINWINDOW_H
