#pragma once
#include <QMainWindow>
#include <QString>
#include <QUrl>

#include "downloaditem.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void handleDownload();
    void handlePauseResume();
    void updateProgress(qint64 bytesReceived, qint64 bytesTotal);
    void updateSpeed(double kbps);
    void updateStatusText(const QString &text);
    void handleFinished(const QString &filePath);
    void handleFailure(const QString &errorText);
    void handlePaused();

private:
    void setupUiDefaults();
    void connectSignals();
    void refreshPauseResumeState();
    void updateStatusLabel();
    QString chooseSavePath(const QUrl &url);
    bool isSafePath(const QString &path) const;
    void loadSavedState();
    void resetProgress();

    Ui::MainWindow *ui{};
    DownloadItem downloader_;
    QUrl currentUrl_{};
    qint64 lastReceived_{0};
    qint64 lastTotal_{-1};
    double lastSpeed_{0.0};
    QString lastStatus_{QStringLiteral("Idle")};
    bool hasSavedState_{false};
};
