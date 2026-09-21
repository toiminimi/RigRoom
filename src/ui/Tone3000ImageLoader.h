#pragma once

#include <QCache>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QSize>

class QLabel;
class QNetworkAccessManager;
class QThreadPool;

// Images from tone3000.com, shared by the whole application. Downloads are
// cached on disk as the original bytes; decoding and scaling happen on worker
// threads, and only the scaled result is kept in memory. Nothing here blocks
// the GUI thread, so lists can ask for a thumbnail on every paint.
class Tone3000ImageLoader final : public QObject {
    Q_OBJECT
public:
    static Tone3000ImageLoader* instance();

    // The image cropped to fill `size` (device pixels), if it is ready.
    // Otherwise returns a null pixmap, starts loading it and emits ready(url)
    // once it can be asked for again.
    QPixmap thumbnail(const QString& imageUrl, const QSize& size);
    // True when the image failed recently; callers draw their placeholder.
    bool failed(const QString& imageUrl) const;

    // Shows the image in a label, following the label's size.
    void load(QLabel* label, const QString& imageUrl);
    static QString firstImageUrl(const QJsonObject& tone);

signals:
    void ready(const QString& imageUrl);

private:
    explicit Tone3000ImageLoader(QObject* parent = nullptr);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void applyToLabel(QLabel* label);
    void decode(const QString& imageUrl, const QSize& size);
    void fetch(const QString& imageUrl);
    void pumpNetwork();
    void markFailed(const QString& imageUrl);
    void schedulePrune();
    static QString cacheKey(const QString& imageUrl, const QSize& size);
    static QString diskPath(const QString& imageUrl);
    static bool allowedUrl(const QString& imageUrl);

    QNetworkAccessManager* m_network = nullptr;
    QThreadPool* m_pool = nullptr;
    QCache<QString, QPixmap> m_thumbnails;       // key: url|WxH, cost in KiB
    QSet<QString> m_decoding;                    // keys being decoded
    QHash<QString, QList<QSize>> m_waitingSizes; // url -> sizes wanted once downloaded
    QList<QString> m_networkStack;               // newest last; served first
    QSet<QString> m_fetching;
    QHash<QString, qint64> m_failedUntil;
    QHash<QString, QList<QPointer<QLabel>>> m_labels;
    int m_activeFetches = 0;
    int m_priority = 0;
    int m_savedSincePrune = 0;
    qint64 m_lastPrune = 0;
    static constexpr int kMaxActiveFetches = 6;
    static constexpr int kMaxQueuedFetches = 80;
};
