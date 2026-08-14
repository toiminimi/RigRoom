#include "Tone3000ImageLoader.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfoList>
#include <QImageReader>
#include <QJsonArray>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <iterator>
#include <memory>

Tone3000ImageLoader::Tone3000ImageLoader(QObject* parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)) {
    m_memoryCache.setMaxCost(64 * 1024);
    pruneDiskCache();
}

QString Tone3000ImageLoader::firstImageUrl(const QJsonObject& tone) {
    const QJsonArray images = tone.value("images").toArray();
    for (const QJsonValue& image : images) {
        const QString url = image.isString() ? image.toString() : image.toObject().value("url").toString();
        if (!url.isEmpty()) return url;
    }
    return {};
}

QString Tone3000ImageLoader::cachePath(const QString& imageUrl) {
    const QString directory = QDir::homePath() + "/.cache/RigRoom/tone3000/images";
    QDir().mkpath(directory);
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(imageUrl.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(directory).filePath(key + ".png");
}

void Tone3000ImageLoader::pruneDiskCache() {
    QDir directory(QDir::homePath() + "/.cache/RigRoom/tone3000/images");
    const QFileInfoList files = directory.entryInfoList({"*.png"}, QDir::Files, QDir::Time);
    constexpr qint64 maxBytes = 256LL * 1024 * 1024;
    constexpr int maxFiles = 200;
    qint64 retainedBytes = 0;
    for (int index = 0; index < files.size(); ++index) {
        retainedBytes += files[index].size();
        if (index >= maxFiles || retainedBytes > maxBytes) QFile::remove(files[index].absoluteFilePath());
    }
}

void Tone3000ImageLoader::applyImage(QLabel* label, const QPixmap& source, const QString& imageUrl) {
    if (!label || source.isNull() || label->property("tone3000ImageUrl").toString() != imageUrl) return;
    const QSize target = label->size();
    if (target.isEmpty()) return;
    const QPixmap scaled = source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - target.width()) / 2);
    const int y = std::max(0, (scaled.height() - target.height()) / 2);
    label->setPixmap(scaled.copy(x, y, target.width(), target.height()));
    label->setText({});
}

void Tone3000ImageLoader::load(QLabel* label, const QString& imageUrl) {
    if (!label) return;
    label->installEventFilter(this);
    label->setProperty("tone3000ImageUrl", imageUrl);
    if (imageUrl.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_failedUntil.size() > 100) {
        for (auto it = m_failedUntil.begin(); it != m_failedUntil.end();) {
            it = it.value() <= now ? m_failedUntil.erase(it) : std::next(it);
        }
    }
    const auto failed = m_failedUntil.constFind(imageUrl);
    if (failed != m_failedUntil.constEnd()) {
        if (failed.value() > now) {
            if (label->text() == "Loading image...") label->setText("Image unavailable");
            return;
        }
        m_failedUntil.remove(imageUrl);
    }

    const QUrl url(imageUrl);
    const QString host = url.host().toLower();
    if (url.scheme() != "https" || (host != "tone3000.com" && !host.endsWith(".tone3000.com"))) {
        m_failedUntil.insert(imageUrl, now + 60000);
        if (label->text() == "Loading image...") label->setText("Image unavailable");
        return;
    }

    if (const QPixmap* memoryImage = m_memoryCache.object(imageUrl)) {
        applyImage(label, *memoryImage, imageUrl);
        return;
    }

    QPixmap diskImage(cachePath(imageUrl));
    if (!diskImage.isNull()) {
        m_memoryCache.insert(imageUrl, new QPixmap(diskImage),
                            std::max(1, diskImage.width() * diskImage.height() * 4 / 1024));
        applyImage(label, diskImage, imageUrl);
        return;
    }

    const bool alreadyPending = m_waitingLabels.contains(imageUrl);
    if (!alreadyPending && m_pendingUrls.size() >= 100) {
        if (label->text() == "Loading image...") label->setText("Image unavailable");
        return;
    }
    m_waitingLabels[imageUrl].append(QPointer<QLabel>(label));
    if (!alreadyPending) {
        m_pendingUrls.enqueue(imageUrl);
        pumpQueue();
    }
}

bool Tone3000ImageLoader::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (auto* label = qobject_cast<QLabel*>(watched)) {
            const QString imageUrl = label->property("tone3000ImageUrl").toString();
            if (const QPixmap* image = m_memoryCache.object(imageUrl)) applyImage(label, *image, imageUrl);
        }
    }
    return QObject::eventFilter(watched, event);
}

void Tone3000ImageLoader::pumpQueue() {
    while (m_activeRequests < MAX_ACTIVE_REQUESTS && !m_pendingUrls.isEmpty()) {
        const QString imageUrl = m_pendingUrls.dequeue();
        QList<QPointer<QLabel>>& waiting = m_waitingLabels[imageUrl];
        waiting.erase(std::remove_if(waiting.begin(), waiting.end(), [&imageUrl](const QPointer<QLabel>& label) {
            return label.isNull() || label->property("tone3000ImageUrl").toString() != imageUrl;
        }), waiting.end());
        if (waiting.isEmpty()) {
            m_waitingLabels.remove(imageUrl);
            continue;
        }
        QNetworkRequest request{QUrl(imageUrl)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        QNetworkReply* reply = m_networkManager->get(request);
        reply->setReadBufferSize(5 * 1024 * 1024 + 1);
        QTimer::singleShot(15000, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        ++m_activeRequests;
        auto data = std::make_shared<QByteArray>();
        connect(reply, &QNetworkReply::readyRead, this, [reply, data]() {
            constexpr qsizetype maxBytes = 5 * 1024 * 1024;
            data->append(reply->read(maxBytes + 1 - data->size()));
            if (data->size() > maxBytes || reply->bytesAvailable() > 0) reply->abort();
        });

        connect(reply, &QNetworkReply::finished, this, [this, reply, imageUrl, data]() {
            if (reply->error() == QNetworkReply::NoError) data->append(reply->readAll());
            const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
            reply->deleteLater();
            --m_activeRequests;

            QPixmap image;
            if (!redirected && data->size() <= 5 * 1024 * 1024) {
                QBuffer buffer(data.get());
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const QSize sourceSize = reader.size();
                if (sourceSize.isValid() && sourceSize.width() <= 8192 && sourceSize.height() <= 8192) {
                    reader.setScaledSize(sourceSize.scaled(1600, 1000, Qt::KeepAspectRatio));
                    const QImage decoded = reader.read();
                    if (!decoded.isNull()) image = QPixmap::fromImage(decoded);
                }
            }
            if (!image.isNull()) {
                image.save(cachePath(imageUrl), "PNG");
                pruneDiskCache();
                m_memoryCache.insert(imageUrl, new QPixmap(image),
                                    std::max(1, image.width() * image.height() * 4 / 1024));
            } else {
                m_failedUntil.insert(imageUrl, QDateTime::currentMSecsSinceEpoch() + 60000);
            }

            const QList<QPointer<QLabel>> labels = m_waitingLabels.take(imageUrl);
            if (!image.isNull()) {
                for (const QPointer<QLabel>& label : labels) {
                    if (!label.isNull()) applyImage(label, image, imageUrl);
                }
            } else {
                for (const QPointer<QLabel>& label : labels) {
                    if (!label.isNull() && label->property("tone3000ImageUrl").toString() == imageUrl
                        && label->text() == "Loading image...") {
                        label->setText("Image unavailable");
                    }
                }
            }
            pumpQueue();
        });
    }
}
