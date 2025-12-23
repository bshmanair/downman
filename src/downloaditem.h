#pragma once

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QSslError>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <QObject>

class DownloadItem : public QObject
{
    Q_OBJECT

public:
    struct ResumeData
    {
        QUrl url{};
        QString filePath{};
        qint64 bytesDownloaded{};

        bool isValid() const;
    };

    explicit DownloadItem(QObject *parent = nullptr);

    void startNew(const QUrl &url, const QString &filePath);
    void resumeFromSaved();
    void pause();

    ResumeData currentState() const;
    ResumeData loadSavedState() const;
    void clearSavedState();

    bool isActive() const;
    bool isPaused() const;

signals:
    void progressChanged(qint64 bytesReceived, qint64 bytesTotal);
    void speedUpdated(double kilobytesPerSecond);
    void statusTextChanged(const QString &text);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &errorText);
    void paused();

private slots:
    void handleReadyRead();
    void handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void handleFinished();
    void handleError(QNetworkReply::NetworkError error);
    void handleMetaDataChanged();
    void handleSslErrors(const QList<QSslError> &errors);
    void updateSpeed();

private:
    void startRequest();
    bool openFile(bool truncate);
    void resetReply();
    void persistResumeData() const;
    QString resumeDataPath() const;
    bool checkSizeLimit(qint64 nextChunkBytes);

    QNetworkAccessManager manager_{};
    QNetworkReply *reply_{nullptr};
    QFile file_{};
    QUrl url_{};
    QString targetPath_;
    qint64 downloaded_{0};
    qint64 startOffset_{0};
    qint64 totalBytes_{-1};
    bool paused_{false};
    int redirectCount_{0};
    bool suppressErrors_{false};

    QTimer speedTimer_{};
    qint64 bytesThisSecond_{0};
};
