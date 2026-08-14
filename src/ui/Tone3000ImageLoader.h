#pragma once

#include <QObject>
#include <QCache>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <QQueue>

class QLabel;
class QNetworkAccessManager;

class Tone3000ImageLoader final : public QObject {
public:
    explicit Tone3000ImageLoader(QObject* parent = nullptr);

    void load(QLabel* label, const QString& imageUrl);
    static QString firstImageUrl(const QJsonObject& tone);

private:
    void pumpQueue();
    bool eventFilter(QObject* watched, QEvent* event) override;
    static QString cachePath(const QString& imageUrl);
    static void pruneDiskCache();
    static void applyImage(QLabel* label, const QPixmap& source, const QString& imageUrl);

    QNetworkAccessManager* m_networkManager = nullptr;
    QCache<QString, QPixmap> m_memoryCache;
    QHash<QString, QList<QPointer<QLabel>>> m_waitingLabels;
    QQueue<QString> m_pendingUrls;
    QHash<QString, qint64> m_failedUntil;
    int m_activeRequests = 0;
    static constexpr int MAX_ACTIVE_REQUESTS = 4;
};
