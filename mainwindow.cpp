#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setInputField();

    manager = new QNetworkAccessManager(this);

    connect(ui->downloadInput, &QLineEdit::returnPressed, this, &MainWindow::onDownloadInputConfirmed);
    connect(ui->buttonDownload, &QPushButton::pressed, this, &MainWindow::onDownloadInputConfirmed);
}

void MainWindow::setInputField()
{
    ui->downloadInput->setPlaceholderText("Enter link...");
    ui->downloadInput->setClearButtonEnabled(true);
}

void MainWindow::onDownloadInputConfirmed()
{
    const QString text = ui->downloadInput->text().trimmed();
    QUrl url = QUrl::fromUserInput(text);
    if (!url.isValid())
    {
        ui->labelResult->setText("Invalid URL.");
        return;
    }
    downloadURL = url;
    ui->labelResult->setText("Starting download...");
    startDownload(downloadURL);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::startDownload(const QUrl &url)
{
    QNetworkRequest request(url);

    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = manager->get(request);

    auto func = [this, reply]()
    {
        if (reply->error() != QNetworkReply::NoError)
        {
            ui->labelResult->setText("Download failed: " + reply->errorString());
        }
    };
    connect(reply, &QNetworkReply::finished, this, func);
}
