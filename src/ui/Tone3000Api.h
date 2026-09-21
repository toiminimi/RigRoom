#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;

// The TONE3000 web API for the whole application. Keeps one network manager
// and a session cache of JSON responses, so reopening the browser or going
// back to a search shows results at once.
class Tone3000Api : public QObject {
    Q_OBJECT
public:
    struct Response {
        bool ok = false;
        int httpStatus = 0;
        QNetworkReply::NetworkError error = QNetworkReply::NoError;
        QString errorString;
        QJsonObject object; // the body; an array body is wrapped as {"data": [...]}
    };
    using Callback = std::function<void(const Response&)>;
    // Called while a rate-limited request waits to be retried.
    using WaitCallback = std::function<void(int seconds)>;

    static Tone3000Api* instance();

    QNetworkAccessManager* network() const { return m_network; }
    static bool hasKey();
    static QNetworkRequest authorized(const QUrl& url);

    // GETs JSON with the user's key. The returned handle is a child of
    // `context`; deleting it cancels the request, callbacks included. It
    // deletes itself once `done` has run. With a cache key, a fresh cached
    // answer is delivered without touching the network (still asynchronously)
    // and successful answers are stored.
    QPointer<QObject> getJson(const QUrl& url, QObject* context, Callback done, WaitCallback waiting = {},
                              const QString& cacheKey = {});
    // A cached answer younger than the TTL, if any.
    bool cached(const QString& cacheKey, QJsonObject* object) const;
    void clearCache();

    // Every model (variant) of a tone. The models endpoint lists only A2
    // captures unless asked for another architecture, and has no "all", so
    // this asks for A2, A1 and custom separately and merges them. Cached like
    // getJson under "models|<id>|".
    using ModelsCallback = std::function<void(bool ok, const QJsonArray& models, const QString& error)>;
    void fetchAllModels(int toneId, QObject* context, ModelsCallback done);

    // All tones on the user's TONE3000 favorites list, every page, handed to
    // Tone3000Library. Skipped when fetched recently unless forced.
    void refreshFavorites(bool force = false);

    static constexpr qint64 kCacheTtlMs = 10 * 60 * 1000;

private:
    explicit Tone3000Api(QObject* parent = nullptr);
    void send(const QUrl& url, QPointer<QObject> handle, Callback done, WaitCallback waiting, QString cacheKey,
              int attempt);
    void fetchFavoritesPage(int page, QJsonArray collected);

    struct CacheEntry {
        QJsonObject object;
        qint64 storedAt = 0;
    };
    QNetworkAccessManager* m_network = nullptr;
    QHash<QString, CacheEntry> m_cache;
    qint64 m_favoritesFetchedAt = 0;
    bool m_fetchingFavorites = false;
};
