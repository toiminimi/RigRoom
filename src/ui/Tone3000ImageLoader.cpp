#include "Tone3000ImageLoader.h"
#include "Tone3000Types.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <memory>

namespace {
constexpr qsizetype kMaxImageBytes = 5 * 1024 * 1024;
constexpr int kMaxImageSide = 8192;
constexpr qint64 kFailureMs = 60000;

QString cacheDirectory() {
    return QDir::homePath() + "/.cache/RigRoom/tone3000/images";
}

// Decodes straight to the size needed and crops it to fill, like
// background-size: cover.
QImage decodeToFill(QImageReader& reader, const QSize& target) {
    const QSize source = reader.size();
    if (!source.isValid() || source.width() > kMaxImageSide || source.height() > kMaxImageSide) return {};
    const QSize expanded = source.scaled(target, Qt::KeepAspectRatioByExpanding);
    // Decode a little larger than needed and scale smoothly for quality.
    if (expanded.width() * 2 < source.width()) reader.setScaledSize(expanded * 2);
    QImage image = reader.read();
    if (image.isNull()) return {};
    image = image.scaled(expanded, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int x = std::max(0, (image.width() - target.width()) / 2);
    const int y = std::max(0, (image.height() - target.height()) / 2);
    return image.copy(x, y, target.width(), target.height());
}

void pruneDiskCache() {
    QDir directory(cacheDirectory());
    const QFileInfoList files = directory.entryInfoList({"*.img", "*.png"}, QDir::Files, QDir::Time);
    constexpr qint64 maxBytes = 300LL * 1024 * 1024;
    constexpr int maxFiles = 3000;
    qint64 retainedBytes = 0;
    for (int index = 0; index < files.size(); ++index) {
        retainedBytes += files[index].size();
        if (index >= maxFiles || retainedBytes > maxBytes) QFile::remove(files[index].absoluteFilePath());
    }
}
} // namespace

Tone3000ImageLoader* Tone3000ImageLoader::instance() {
    // Never deleted: worker threads post their results back to it.
    static auto* loader = new Tone3000ImageLoader();
    return loader;
}

Tone3000ImageLoader::Tone3000ImageLoader(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)), m_pool(new QThreadPool(this)) {
    m_pool->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 1, 3));
    m_thumbnails.setMaxCost(96 * 1024);
    m_lastPrune = QDateTime::currentMSecsSinceEpoch();
    m_pool->start([]() { pruneDiskCache(); }, -1);
    connect(this, &Tone3000ImageLoader::ready, this, [this](const QString& url) {
        const auto it = m_labels.find(url);
        if (it == m_labels.end()) return;
        const QList<QPointer<QLabel>> labels = it.value();
        m_labels.erase(it);
        for (const QPointer<QLabel>& label : labels) {
            if (label && label->property("tone3000ImageUrl").toString() == url) {
                m_labels[url] << label;
                applyToLabel(label);
            }
        }
    });
}

QString Tone3000ImageLoader::firstImageUrl(const QJsonObject& tone) {
    return Tone3000::firstImageUrl(tone);
}

QString Tone3000ImageLoader::cacheKey(const QString& imageUrl, const QSize& size) {
    return QString("%1|%2x%3").arg(imageUrl).arg(size.width()).arg(size.height());
}

QString Tone3000ImageLoader::diskPath(const QString& imageUrl) {
    const QString key = QString::fromLatin1(QCryptographicHash::hash(imageUrl.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(cacheDirectory()).filePath(key + ".img");
}

bool Tone3000ImageLoader::allowedUrl(const QString& imageUrl) {
    const QUrl url(imageUrl);
    const QString host = url.host().toLower();
    return url.scheme() == "https" && (host == "tone3000.com" || host.endsWith(".tone3000.com"));
}

bool Tone3000ImageLoader::failed(const QString& imageUrl) const {
    const auto it = m_failedUntil.constFind(imageUrl);
    return it != m_failedUntil.constEnd() && it.value() > QDateTime::currentMSecsSinceEpoch();
}

void Tone3000ImageLoader::markFailed(const QString& imageUrl) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_failedUntil.size() > 200) {
        for (auto it = m_failedUntil.begin(); it != m_failedUntil.end();) {
            it = it.value() <= now ? m_failedUntil.erase(it) : std::next(it);
        }
    }
    m_failedUntil.insert(imageUrl, now + kFailureMs);
    m_waitingSizes.remove(imageUrl);
    emit ready(imageUrl);
}

QPixmap Tone3000ImageLoader::thumbnail(const QString& imageUrl, const QSize& size) {
    if (imageUrl.isEmpty() || size.isEmpty()) return {};
    const QString key = cacheKey(imageUrl, size);
    if (const QPixmap* cached = m_thumbnails.object(key)) return *cached;
    if (failed(imageUrl) || m_decoding.contains(key)) return {};
    if (!allowedUrl(imageUrl)) {
        markFailed(imageUrl);
        return {};
    }
    if (m_fetching.contains(imageUrl) || m_networkStack.contains(imageUrl)) {
        QList<QSize>& sizes = m_waitingSizes[imageUrl];
        if (!sizes.contains(size)) sizes << size;
        fetch(imageUrl); // moves it to the front of the queue
        return {};
    }
    decode(imageUrl, size);
    return {};
}

void Tone3000ImageLoader::decode(const QString& imageUrl, const QSize& size) {
    const QString key = cacheKey(imageUrl, size);
    m_decoding.insert(key);
    // Later requests are the rows on screen now; decode those first.
    m_pool->start([this, imageUrl, size, key]() {
        const QString path = diskPath(imageUrl);
        if (!QFile::exists(path)) {
            QMetaObject::invokeMethod(this, [this, imageUrl, size, key]() {
                m_decoding.remove(key);
                QList<QSize>& sizes = m_waitingSizes[imageUrl];
                if (!sizes.contains(size)) sizes << size;
                fetch(imageUrl);
            }, Qt::QueuedConnection);
            return;
        }
        QImageReader reader(path);
        const QImage image = decodeToFill(reader, size);
        if (image.isNull()) QFile::remove(path);
        QMetaObject::invokeMethod(this, [this, imageUrl, key, image]() {
            m_decoding.remove(key);
            if (image.isNull()) {
                markFailed(imageUrl);
                return;
            }
            m_thumbnails.insert(key, new QPixmap(QPixmap::fromImage(image)),
                                std::max<qsizetype>(1, image.sizeInBytes() / 1024));
            emit ready(imageUrl);
        }, Qt::QueuedConnection);
    }, ++m_priority);
}

void Tone3000ImageLoader::fetch(const QString& imageUrl) {
    if (m_fetching.contains(imageUrl)) return;
    m_networkStack.removeAll(imageUrl);
    m_networkStack.append(imageUrl);
    // Rows scrolled past long ago are forgotten; they ask again when painted.
    while (m_networkStack.size() > kMaxQueuedFetches) m_waitingSizes.remove(m_networkStack.takeFirst());
    pumpNetwork();
}

void Tone3000ImageLoader::pumpNetwork() {
    while (m_activeFetches < kMaxActiveFetches && !m_networkStack.isEmpty()) {
        const QString imageUrl = m_networkStack.takeLast();
        m_fetching.insert(imageUrl);
        ++m_activeFetches;

        QNetworkRequest request{QUrl(imageUrl)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        QNetworkReply* reply = m_network->get(request);
        reply->setReadBufferSize(kMaxImageBytes + 1);
        QTimer::singleShot(15000, reply, [reply]() {
            if (reply->isRunning()) reply->abort();
        });
        auto data = std::make_shared<QByteArray>();
        connect(reply, &QNetworkReply::readyRead, this, [reply, data]() {
            data->append(reply->read(kMaxImageBytes + 1 - data->size()));
            if (data->size() > kMaxImageBytes || reply->bytesAvailable() > 0) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, imageUrl, data]() {
            if (reply->error() == QNetworkReply::NoError) data->append(reply->readAll());
            const bool ok = reply->error() == QNetworkReply::NoError
                && !reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()
                && data->size() <= kMaxImageBytes && !data->isEmpty();
            reply->deleteLater();
            --m_activeFetches;
            if (!ok) {
                m_fetching.remove(imageUrl);
                markFailed(imageUrl);
                pumpNetwork();
                return;
            }
            // Check and store the bytes off the GUI thread, then decode each
            // size that was asked for while downloading.
            m_pool->start([this, imageUrl, data]() {
                QBuffer buffer(data.get());
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                bool stored = false;
                if (reader.canRead()) {
                    const QSize size = reader.size();
                    if (size.isValid() && size.width() <= kMaxImageSide && size.height() <= kMaxImageSide) {
                        QDir().mkpath(cacheDirectory());
                        QSaveFile file(diskPath(imageUrl));
                        stored = file.open(QIODevice::WriteOnly) && file.write(*data) == data->size() && file.commit();
                    }
                }
                QMetaObject::invokeMethod(this, [this, imageUrl, stored]() {
                    m_fetching.remove(imageUrl);
                    if (!stored) {
                        markFailed(imageUrl);
                    } else {
                        for (const QSize& size : m_waitingSizes.take(imageUrl)) decode(imageUrl, size);
                        if (++m_savedSincePrune >= 50) schedulePrune();
                    }
                    pumpNetwork();
                }, Qt::QueuedConnection);
            }, ++m_priority);
        });
    }
}

void Tone3000ImageLoader::schedulePrune() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastPrune < 60000) return;
    m_lastPrune = now;
    m_savedSincePrune = 0;
    m_pool->start([]() { pruneDiskCache(); }, -1);
}

// ─── Labels ──────────────────────────────────────────────────────────────────

void Tone3000ImageLoader::load(QLabel* label, const QString& imageUrl) {
    if (!label) return;
    const QString previous = label->property("tone3000ImageUrl").toString();
    label->setProperty("tone3000ImageUrl", imageUrl);
    if (previous.isEmpty() && !label->property("tone3000Watched").toBool()) {
        label->setProperty("tone3000Watched", true);
        label->installEventFilter(this);
    }
    if (imageUrl.isEmpty()) return;
    QList<QPointer<QLabel>>& labels = m_labels[imageUrl];
    if (!labels.contains(label)) labels << label;
    applyToLabel(label);
}

void Tone3000ImageLoader::applyToLabel(QLabel* label) {
    const QString imageUrl = label->property("tone3000ImageUrl").toString();
    if (imageUrl.isEmpty() || label->size().isEmpty()) return;
    const qreal dpr = label->devicePixelRatioF();
    QPixmap pixmap = thumbnail(imageUrl, label->size() * dpr);
    if (!pixmap.isNull()) {
        pixmap.setDevicePixelRatio(dpr);
        label->setPixmap(pixmap);
        label->setText({});
    } else if (failed(imageUrl) && label->text() == "Loading image...") {
        label->setText("Image unavailable");
    }
}

bool Tone3000ImageLoader::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (auto* label = qobject_cast<QLabel*>(watched)) applyToLabel(label);
    }
    return QObject::eventFilter(watched, event);
}
