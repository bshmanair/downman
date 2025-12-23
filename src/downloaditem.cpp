#include "downloaditem.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QStringList>

namespace
{
    const auto kUserAgent{QByteArrayLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/119.0 Safari/537.36")};
    constexpr qint64 kMaxDownloadBytes{1024LL * 1024LL * 1024LL}; // 1 GiB cap
    constexpr int kMaxRedirects{5};
}

bool DownloadItem::ResumeData::isValid() const
{
    return url.isValid() && !filePath.isEmpty();
}

DownloadItem::DownloadItem(QObject *parent)
    : QObject(parent), manager_(this)
{
    speedTimer_.setInterval(1000);
    connect(&speedTimer_, &QTimer::timeout, this, &DownloadItem::updateSpeed);
}

void DownloadItem::startNew(const QUrl &url, const QString &filePath)
{
    resetReply();

    url_ = url;
    targetPath_ = filePath;
    downloaded_ = 0;
    totalBytes_ = -1;
    paused_ = false;
    redirectCount_ = 0;

    QFileInfo info{targetPath_};
    QDir dir{info.path()};
    dir.mkpath(QStringLiteral("."));

    if (!openFile(true))
    {
        emit downloadFailed(QStringLiteral("Cannot open file for writing."));
        return;
    }

    persistResumeData();
    startRequest();
    emit statusTextChanged(QStringLiteral("Downloading..."));
}

void DownloadItem::resumeFromSaved()
{
    resetReply();

    const ResumeData saved{loadSavedState()};
    if (!saved.isValid())
    {
        emit downloadFailed(QStringLiteral("No download to resume."));
        return;
    }

    url_ = saved.url;
    targetPath_ = saved.filePath;

    QFileInfo info{targetPath_};
    downloaded_ = info.exists() ? info.size() : 0;
    totalBytes_ = -1;
    paused_ = false;
    redirectCount_ = 0;

    QDir dir{info.path()};
    dir.mkpath(QStringLiteral("."));

    if (!openFile(false))
    {
        emit downloadFailed(QStringLiteral("Cannot open file for writing."));
        return;
    }

    startRequest();
    emit statusTextChanged(QStringLiteral("Resuming..."));
}

void DownloadItem::pause()
{
    if (!reply_)
    {
        return;
    }

    paused_ = true;
    speedTimer_.stop();
    bytesThisSecond_ = 0;

    persistResumeData();
    reply_->abort();
    emit speedUpdated(0.0);
    emit statusTextChanged(QStringLiteral("Paused"));
}

DownloadItem::ResumeData DownloadItem::currentState() const
{
    return ResumeData{url_, targetPath_, downloaded_};
}

DownloadItem::ResumeData DownloadItem::loadSavedState() const
{
    QFile stateFile{resumeDataPath()};
    if (!stateFile.open(QIODevice::ReadOnly))
    {
        return {};
    }

    const auto doc{QJsonDocument::fromJson(stateFile.readAll())};
    if (!doc.isObject())
    {
        return {};
    }

    const QJsonObject obj{doc.object()};
    ResumeData data{};
    data.url = QUrl{obj.value(QStringLiteral("url")).toString()};
    data.filePath = obj.value(QStringLiteral("filePath")).toString();
    data.bytesDownloaded = static_cast<qint64>(obj.value(QStringLiteral("bytesDownloaded")).toDouble());

    QFileInfo pathInfo{data.filePath};
    if (!pathInfo.isAbsolute())
    {
        return {};
    }
    const QString homeDir{QDir::homePath()};
    const QString downloadDir{QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)};
    const QString cleanPath{QDir::cleanPath(pathInfo.absoluteFilePath())};
    const QString cleanHome{QDir::cleanPath(homeDir)};
    const QString cleanDownload{QDir::cleanPath(downloadDir.isEmpty() ? homeDir : downloadDir)};
    const bool underHome{cleanPath.startsWith(cleanHome + QLatin1Char('/')) || cleanPath == cleanHome};
    const bool underDownload{cleanPath.startsWith(cleanDownload + QLatin1Char('/')) || cleanPath == cleanDownload};
    if (!underHome && !underDownload)
    {
        return {};
    }

    QFileInfo fileInfo{data.filePath};
    if (fileInfo.exists())
    {
        data.bytesDownloaded = fileInfo.size();
    }

    return data;
}

void DownloadItem::clearSavedState()
{
    QFile::remove(resumeDataPath());
}

bool DownloadItem::isActive() const
{
    return reply_ != nullptr;
}

bool DownloadItem::isPaused() const
{
    return paused_;
}

void DownloadItem::handleReadyRead()
{
    if (!reply_ || !file_.isOpen())
    {
        return;
    }

    const QByteArray data{reply_->readAll()};
    if (data.isEmpty())
    {
        return;
    }

    if (!checkSizeLimit(data.size()))
    {
        reply_->abort();
        return;
    }

    if (file_.write(data) != data.size())
    {
        emit downloadFailed(QStringLiteral("Failed to write to file."));
        pause();
        return;
    }

    downloaded_ += data.size();
    bytesThisSecond_ += data.size();
    file_.flush();

    persistResumeData();
}

void DownloadItem::handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    const qint64 receivedOverall{startOffset_ + bytesReceived};
    qint64 totalOverall{bytesTotal > 0 ? startOffset_ + bytesTotal : totalBytes_};
    if (bytesTotal < 0 && totalBytes_ < 0)
    {
        totalOverall = -1;
    }
    else if (bytesTotal < 0)
    {
        totalOverall = totalBytes_;
    }

    emit progressChanged(receivedOverall, totalOverall);
}

void DownloadItem::handleFinished()
{
    speedTimer_.stop();
    bytesThisSecond_ = 0;

    if (!reply_)
    {
        return;
    }

    const QVariant redirectTarget{reply_->attribute(QNetworkRequest::RedirectionTargetAttribute)};
    if (redirectTarget.isValid())
    {
        if (redirectCount_ >= kMaxRedirects)
        {
            emit statusTextChanged(QStringLiteral("Error: too many redirects"));
            emit downloadFailed(QStringLiteral("Redirect limit reached"));
            resetReply();
            speedTimer_.stop();
            emit speedUpdated(0.0);
            return;
        }

        ++redirectCount_;
        const QUrl redirected{url_.resolved(redirectTarget.toUrl())};
        url_ = redirected;
        resetReply();
        startRequest();
        return;
    }

    if (suppressErrors_)
    {
        if (file_.isOpen())
        {
            file_.flush();
            file_.close();
        }
        emit speedUpdated(0.0);
        resetReply();
        return;
    }

    if (reply_->error() == QNetworkReply::NoError)
    {
        if (file_.isOpen())
        {
            file_.flush();
            file_.close();
        }
        clearSavedState();
        emit statusTextChanged(QStringLiteral("Completed"));
        emit downloadFinished(targetPath_);
    }
    else if (paused_ && reply_->error() == QNetworkReply::OperationCanceledError)
    {
        if (file_.isOpen())
        {
            file_.flush();
            file_.close();
        }
        persistResumeData();
    }
    else
    {
        if (file_.isOpen())
        {
            file_.flush();
            file_.close();
        }
    }

    emit speedUpdated(0.0);
    resetReply();
}

void DownloadItem::handleError(QNetworkReply::NetworkError error)
{
    if (suppressErrors_ && error == QNetworkReply::OperationCanceledError)
    {
        suppressErrors_ = false;
        return;
    }

    if (paused_ && error == QNetworkReply::OperationCanceledError)
    {
        emit paused();
        return;
    }

    speedTimer_.stop();
    bytesThisSecond_ = 0;
    emit speedUpdated(0.0);
    emit statusTextChanged(QStringLiteral("Error: ") + reply_->errorString());
    emit downloadFailed(reply_->errorString());
    persistResumeData();
}

void DownloadItem::handleSslErrors(const QList<QSslError> &errors)
{
    QStringList messages{};
    messages.reserve(errors.size());
    for (const auto &err : errors)
    {
        messages << err.errorString();
    }

    const QString combined{messages.join(QStringLiteral("; "))};
    emit statusTextChanged(QStringLiteral("SSL error: ") + combined);
    emit downloadFailed(combined);
    if (reply_)
    {
        reply_->abort();
    }
}

void DownloadItem::handleMetaDataChanged()
{
    if (!reply_)
    {
        return;
    }

    const int status{reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()};
    if (status == 200 && startOffset_ > 0)
    {
        if (file_.isOpen())
        {
            file_.resize(0);
        }
        downloaded_ = 0;
        startOffset_ = 0;
        persistResumeData();
    }

    const QVariant lengthHeader{reply_->header(QNetworkRequest::ContentLengthHeader)};
    if (lengthHeader.isValid())
    {
        totalBytes_ = startOffset_ + lengthHeader.toLongLong();
        if (totalBytes_ > kMaxDownloadBytes)
        {
            emit statusTextChanged(QStringLiteral("Aborted: file too large"));
            emit downloadFailed(QStringLiteral("Content length exceeds limit"));
            suppressErrors_ = true;
            reply_->abort();
        }
    }
}

void DownloadItem::updateSpeed()
{
    const double speed{static_cast<double>(bytesThisSecond_) / 1024.0};
    emit speedUpdated(speed);
    bytesThisSecond_ = 0;
}

void DownloadItem::startRequest()
{
    if (!file_.isOpen())
    {
        emit downloadFailed(QStringLiteral("File is not open."));
        return;
    }

    startOffset_ = downloaded_;
    bytesThisSecond_ = 0;
    paused_ = false;
    suppressErrors_ = false;

    QNetworkRequest request{url_};
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    if (downloaded_ > 0)
    {
        const QByteArray rangeHeader{QByteArrayLiteral("bytes=") + QByteArray::number(downloaded_) + QByteArrayLiteral("-")};
        request.setRawHeader(QByteArrayLiteral("Range"), rangeHeader);
    }

    reply_ = manager_.get(request);

    connect(reply_, &QNetworkReply::readyRead, this, &DownloadItem::handleReadyRead);
    connect(reply_, &QNetworkReply::downloadProgress, this, &DownloadItem::handleDownloadProgress);
    connect(reply_, &QNetworkReply::finished, this, &DownloadItem::handleFinished);
    connect(reply_, &QNetworkReply::errorOccurred, this, &DownloadItem::handleError);
    connect(reply_, &QNetworkReply::metaDataChanged, this, &DownloadItem::handleMetaDataChanged);
    connect(reply_, &QNetworkReply::sslErrors, this, &DownloadItem::handleSslErrors);

    speedTimer_.start();
}

bool DownloadItem::openFile(bool truncate)
{
    if (file_.isOpen())
    {
        file_.close();
    }

    file_.setFileName(targetPath_);
    QIODevice::OpenMode mode{QIODevice::WriteOnly};
    mode |= truncate ? QIODevice::Truncate : QIODevice::Append;

    if (!file_.open(mode))
    {
        return false;
    }

    if (!truncate && downloaded_ > 0)
    {
        file_.seek(downloaded_);
    }
    else
    {
        downloaded_ = 0;
    }

    return true;
}

void DownloadItem::resetReply()
{
    if (reply_)
    {
        reply_->disconnect(this);
        reply_->deleteLater();
        reply_ = nullptr;
    }
    suppressErrors_ = false;
}

void DownloadItem::persistResumeData() const
{
    QJsonObject obj{};
    obj.insert(QStringLiteral("url"), url_.toString());
    obj.insert(QStringLiteral("filePath"), targetPath_);
    obj.insert(QStringLiteral("bytesDownloaded"), static_cast<double>(downloaded_));

    const QString path{resumeDataPath()};
    QFile infoFile{path};
    if (!infoFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }

    const QJsonDocument doc{obj};
    infoFile.write(doc.toJson(QJsonDocument::Compact));
}

QString DownloadItem::resumeDataPath() const
{
    const QString dir{QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)};
    QDir{}.mkpath(dir);
    return dir + QStringLiteral("/resume.json");
}

bool DownloadItem::checkSizeLimit(qint64 nextChunkBytes)
{
    if (kMaxDownloadBytes <= 0)
    {
        return true;
    }

    const qint64 projected{downloaded_ + nextChunkBytes};
    if (projected > kMaxDownloadBytes)
    {
        emit statusTextChanged(QStringLiteral("Aborted: file too large"));
        emit downloadFailed(QStringLiteral("Exceeded maximum download size"));
        if (reply_)
        {
            suppressErrors_ = true;
            reply_->abort();
        }
        return false;
    }

    return true;
}
