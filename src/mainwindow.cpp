#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUiDefaults();
    connectSignals();
    loadSavedState();
    updateStatusLabel();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupUiDefaults()
{
    ui->downloadInput->setPlaceholderText(tr("Enter URL..."));
    ui->downloadInput->setClearButtonEnabled(true);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->statusLabel->setText(tr("Idle"));
    ui->pauseResumeButton->setEnabled(false);
}

void MainWindow::connectSignals()
{
    connect(ui->buttonDownload, &QPushButton::clicked, this, &MainWindow::handleDownload);
    connect(ui->pauseResumeButton, &QPushButton::clicked, this, &MainWindow::handlePauseResume);
    connect(ui->downloadInput, &QLineEdit::returnPressed, this, &MainWindow::handleDownload);

    connect(&downloader_, &DownloadItem::progressChanged, this, &MainWindow::updateProgress);
    connect(&downloader_, &DownloadItem::speedUpdated, this, &MainWindow::updateSpeed);
    connect(&downloader_, &DownloadItem::statusTextChanged, this, &MainWindow::updateStatusText);
    connect(&downloader_, &DownloadItem::downloadFinished, this, &MainWindow::handleFinished);
    connect(&downloader_, &DownloadItem::downloadFailed, this, &MainWindow::handleFailure);
    connect(&downloader_, &DownloadItem::paused, this, &MainWindow::handlePaused);
}

void MainWindow::handleDownload()
{
    if (downloader_.isActive())
    {
        return;
    }

    const QString text{ui->downloadInput->text().trimmed()};
    const QUrl url{QUrl::fromUserInput(text)};
    if (!url.isValid() || url.isRelative())
    {
        handleFailure(tr("Invalid URL"));
        return;
    }

    const QString savePath{chooseSavePath(url)};
    if (savePath.isEmpty())
    {
        return;
    }

    currentUrl_ = url;
    resetProgress();
    lastStatus_ = tr("Starting...");
    updateStatusLabel();

    ui->buttonDownload->setEnabled(false);
    ui->pauseResumeButton->setEnabled(true);
    ui->pauseResumeButton->setText(tr("Pause"));

    downloader_.startNew(url, savePath);
    hasSavedState_ = true;
}

void MainWindow::handlePauseResume()
{
    if (downloader_.isActive())
    {
        if (downloader_.isPaused())
        {
            ui->buttonDownload->setEnabled(false);
            ui->pauseResumeButton->setText(tr("Pause"));
            lastStatus_ = tr("Resuming...");
            updateStatusLabel();
            downloader_.resumeFromSaved();
        }
        else
        {
            downloader_.pause();
        }
        return;
    }

    if (hasSavedState_)
    {
        ui->buttonDownload->setEnabled(false);
        ui->pauseResumeButton->setEnabled(true);
        ui->pauseResumeButton->setText(tr("Pause"));
        lastStatus_ = tr("Resuming...");
        updateStatusLabel();
        downloader_.resumeFromSaved();
    }
}

void MainWindow::updateProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    lastReceived_ = bytesReceived;
    lastTotal_ = bytesTotal;

    if (bytesTotal > 0)
    {
        ui->progressBar->setRange(0, 100);
        const int percent = static_cast<int>((bytesReceived * 100.0) / bytesTotal);
        ui->progressBar->setValue(percent);
    }
    else
    {
        ui->progressBar->setRange(0, 0);
    }

    updateStatusLabel();
}

void MainWindow::updateSpeed(double kbps)
{
    lastSpeed_ = kbps;
    updateStatusLabel();
}

void MainWindow::updateStatusText(const QString &text)
{
    lastStatus_ = text;
    updateStatusLabel();
}

void MainWindow::handleFinished(const QString &filePath)
{
    lastStatus_ = tr("Completed: %1").arg(QFileInfo(filePath).fileName());
    lastSpeed_ = 0.0;
    ui->buttonDownload->setEnabled(true);
    ui->pauseResumeButton->setEnabled(false);
    ui->pauseResumeButton->setText(tr("Resume"));
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(100);
    hasSavedState_ = false;
    updateStatusLabel();
}

void MainWindow::handleFailure(const QString &errorText)
{
    lastStatus_ = tr("Failed: %1").arg(errorText);
    lastSpeed_ = 0.0;
    ui->buttonDownload->setEnabled(true);
    hasSavedState_ = downloader_.loadSavedState().isValid();
    refreshPauseResumeState();
    updateStatusLabel();
}

void MainWindow::handlePaused()
{
    lastStatus_ = tr("Paused");
    lastSpeed_ = 0.0;
    hasSavedState_ = downloader_.loadSavedState().isValid();
    ui->buttonDownload->setEnabled(true);
    refreshPauseResumeState();
    updateStatusLabel();
}

void MainWindow::refreshPauseResumeState()
{
    if (downloader_.isActive())
    {
        ui->pauseResumeButton->setEnabled(true);
        ui->pauseResumeButton->setText(downloader_.isPaused() ? tr("Resume") : tr("Pause"));
        return;
    }

    ui->pauseResumeButton->setEnabled(hasSavedState_);
    ui->pauseResumeButton->setText(tr("Resume"));
}

void MainWindow::updateStatusLabel()
{
    QStringList parts{};
    parts << lastStatus_;

    if (lastTotal_ > 0)
    {
        const int percent{static_cast<int>((lastReceived_ * 100.0) / lastTotal_)};
        parts << tr("%1%").arg(percent);
    }
    else
    {
        parts << tr("Unknown size");
    }

    parts << tr("Speed: %1 KB/s").arg(QString::number(lastSpeed_, 'f', 1));
    ui->statusLabel->setText(parts.join(QStringLiteral(" | ")));
}

QString MainWindow::chooseSavePath(const QUrl &url)
{
    const QString defaultDir{QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)};
    const QString suggestedName{[&]()
                                {
                                    const QString name = QFileInfo(url.path()).fileName();
                                    if (!name.isEmpty())
                                    {
                                        return name;
                                    }
                                    return QStringLiteral("download.bin");
                                }()};

    const QString target{QFileDialog::getSaveFileName(
        this,
        tr("Save File"),
        (defaultDir.isEmpty() ? QDir::homePath() : defaultDir) + QStringLiteral("/") + suggestedName)};

    if (!target.isEmpty() && !isSafePath(target))
    {
        handleFailure(tr("Invalid save location"));
        return {};
    }

    return target;
}

void MainWindow::loadSavedState()
{
    const DownloadItem::ResumeData saved{downloader_.loadSavedState()};
    hasSavedState_ = saved.isValid() && isSafePath(saved.filePath);
    if (!hasSavedState_)
    {
        downloader_.clearSavedState();
        refreshPauseResumeState();
        return;
    }

    currentUrl_ = saved.url;
    ui->downloadInput->setText(saved.url.toString());
    lastReceived_ = saved.bytesDownloaded;
    lastTotal_ = -1;
    lastStatus_ = tr("Ready to resume");
    ui->progressBar->setRange(0, 0);
    refreshPauseResumeState();
}

void MainWindow::resetProgress()
{
    lastReceived_ = 0;
    lastTotal_ = -1;
    lastSpeed_ = 0.0;
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
}

bool MainWindow::isSafePath(const QString &path) const
{
    if (path.isEmpty())
    {
        return false;
    }

    QFileInfo info{path};
    if (!info.isAbsolute())
    {
        return false;
    }

    const QString downloadDir{QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)};
    const QString homeDir{QDir::homePath()};
    const QString cleanPath{QDir::cleanPath(info.absoluteFilePath())};
    const QString cleanDownload{QDir::cleanPath(downloadDir.isEmpty() ? homeDir : downloadDir)};
    const QString cleanHome{QDir::cleanPath(homeDir)};

    const bool underDownload{cleanPath.startsWith(cleanDownload + QLatin1Char('/')) || cleanPath == cleanDownload};
    const bool underHome{cleanPath.startsWith(cleanHome + QLatin1Char('/')) || cleanPath == cleanHome};

    return underDownload || underHome;
}
