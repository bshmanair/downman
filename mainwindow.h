#pragma once
#include <QMainWindow>

#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setInputField();

private slots:
    void onDownloadInputConfirmed();

private:
    Ui::MainWindow *ui;
    QUrl downloadURL;

    QNetworkAccessManager *manager;
    void startDownload(const QUrl &url);

};
