#include "Tone3000Api.h"
#include "CredentialStore.h"
#include "Tone3000Library.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QSet>
#include <algorithm>
#include <memory>
#include <iostream>

namespace {
constexpr int kMaxRetries = 3;
constexpr int kMaxCacheEntries = 400;
}

Tone3000Api* Tone3000Api::instance() {
    static auto* api = new Tone3000Api();
    return api;
}

Tone3000Api::Tone3000Api(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

bool Tone3000Api::hasKey() {
    return !CredentialStore::tone3000ApiKey().isEmpty();
}

QNetworkRequest Tone3000Api::authorized(const QUrl& url) {
    QNetworkRequest request(url);
    const QString key = CredentialStore::tone3000ApiKey();
    if (!key.isEmpty()) request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    return request;
}

bool Tone3000Api::cached(const QString& cacheKey, QJsonObject* object) const {
    const auto it = m_cache.constFind(cacheKey);
    if (it == m_cache.constEnd() || QDateTime::currentMSecsSinceEpoch() - it->storedAt > kCacheTtlMs) return false;
    if (object) *object = it->object;
    return true;
}

void Tone3000Api::clearCache() {
    m_cache.clear();
}

QPointer<QObject> Tone3000Api::getJson(const QUrl& url, QObject* context, Callback done, WaitCallback waiting,
                                       const QString& cacheKey) {
    QPointer<QObject> handle = new QObject(context ? context : this);
    QJsonObject object;
    if (!cacheKey.isEmpty() && cached(cacheKey, &object)) {
        QTimer::singleShot(0, handle, [handle, done, object]() {
            handle->deleteLater();
            done(Response{true, 200, QNetworkReply::NoError, {}, object});
        });
        return handle;
    }
    if (!hasKey()) {
        QTimer::singleShot(0, handle, [handle, done]() {
            handle->deleteLater();
            done(Response{false, 401, QNetworkReply::AuthenticationRequiredError,
                          "Enter your TONE3000 secret key.", {}});
        });
        return handle;
    }
    send(url, handle, std::move(done), std::move(waiting), cacheKey, 0);
    return handle;
}

void Tone3000Api::send(const QUrl& url, QPointer<QObject> handle, Callback done, WaitCallback waiting,
                       QString cacheKey, int attempt) {
    if (!handle) return;
    QNetworkReply* reply = m_network->get(authorized(url));
    // Deleting the handle aborts the request.
    reply->setParent(handle);
    connect(reply, &QNetworkReply::finished, handle, [=, this]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if ((status == 429 || status == 503) && attempt < kMaxRetries) {
            bool numeric = false;
            int seconds = reply->rawHeader("Retry-After").trimmed().toInt(&numeric);
            if (!numeric || seconds <= 0) seconds = 2 << attempt;
            seconds = std::min(seconds, 30);
            if (waiting) waiting(seconds);
            QTimer::singleShot(seconds * 1000, handle, [=, this]() {
                send(url, handle, done, waiting, cacheKey, attempt + 1);
            });
            return;
        }

        Response response;
        response.httpStatus = status;
        response.error = reply->error();
        if (reply->error() != QNetworkReply::NoError) {
            response.errorString = status == 429 ? QStringLiteral("TONE3000 is limiting requests. Try again in a minute.")
                                                 : reply->errorString();
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                std::cerr << "TONE3000 request failed: " << reply->errorString().toStdString() << std::endl;
            }
        } else {
            const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
            if (document.isObject()) {
                response.object = document.object();
                response.ok = true;
            } else if (document.isArray()) {
                response.object = QJsonObject{{"data", document.array()}};
                response.ok = true;
            } else {
                response.errorString = QStringLiteral("TONE3000 sent an unexpected response.");
            }
        }
        if (response.ok && !cacheKey.isEmpty()) {
            if (m_cache.size() >= kMaxCacheEntries) {
                auto oldest = std::min_element(m_cache.begin(), m_cache.end(),
                                               [](const CacheEntry& a, const CacheEntry& b) { return a.storedAt < b.storedAt; });
                m_cache.erase(oldest);
            }
            m_cache.insert(cacheKey, CacheEntry{response.object, QDateTime::currentMSecsSinceEpoch()});
        }
        handle->deleteLater();
        done(response);
    });
}

void Tone3000Api::fetchAllModels(int toneId, QObject* context, ModelsCallback done) {
    const QString key = QString("models|%1|").arg(toneId);
    QJsonObject cachedObject;
    if (cached(key, &cachedObject)) {
        const QJsonArray models = cachedObject.value("data").toArray();
        QTimer::singleShot(0, context, [done, models]() { done(true, models, {}); });
        return;
    }
    struct State {
        int pending = 3;
        bool ok = true;
        QString error;
        QJsonArray parts[3];
    };
    auto state = std::make_shared<State>();
    const QStringList architectures{"2", "1", "custom"};
    for (int i = 0; i < architectures.size(); ++i) {
        QUrl url("https://www.tone3000.com/api/v1/models");
        QUrlQuery q;
        q.addQueryItem("tone_id", QString::number(toneId));
        q.addQueryItem("page", "1");
        q.addQueryItem("page_size", "300");
        q.addQueryItem("architecture", architectures[i]);
        url.setQuery(q);
        getJson(url, context, [this, state, i, key, done](const Response& response) {
            if (response.ok) state->parts[i] = response.object.value("data").toArray();
            else if (state->ok) {
                state->ok = false;
                state->error = response.errorString;
            }
            if (--state->pending > 0) return;
            // A2 first, then A1, then custom; each model once.
            QJsonArray merged;
            QSet<QString> seen;
            for (const QJsonArray& part : state->parts) {
                for (const QJsonValue& value : part) {
                    const QJsonObject model = value.toObject();
                    const QString id = model.value("id").toVariant().toString() + "|" + model.value("model_url").toString();
                    if (seen.contains(id)) continue;
                    seen.insert(id);
                    merged.append(model);
                }
            }
            if (state->ok) m_cache.insert(key, CacheEntry{QJsonObject{{"data", merged}}, QDateTime::currentMSecsSinceEpoch()});
            done(state->ok || !merged.isEmpty(), merged, state->error);
        }, {}, QString("models|%1|%2").arg(toneId).arg(architectures[i] == "2" ? "2" : architectures[i]));
    }
}

void Tone3000Api::refreshFavorites(bool force) {
    if (!hasKey() || m_fetchingFavorites) return;
    if (!force && m_favoritesFetchedAt > 0
        && QDateTime::currentMSecsSinceEpoch() - m_favoritesFetchedAt < kCacheTtlMs) {
        return;
    }
    m_fetchingFavorites = true;
    fetchFavoritesPage(1, {});
}

void Tone3000Api::fetchFavoritesPage(int page, QJsonArray collected) {
    QUrl url("https://www.tone3000.com/api/v1/tones/favorited");
    QUrlQuery query;
    query.addQueryItem("page", QString::number(page));
    query.addQueryItem("page_size", "100");
    url.setQuery(query);
    getJson(url, this, [this, page, collected](const Response& response) mutable {
        if (!response.ok) {
            m_fetchingFavorites = false;
            return;
        }
        for (const QJsonValue& value : response.object.value("data").toArray()) collected.append(value);
        const int totalPages = response.object.value("total_pages").toInt();
        if (totalPages > page && page < 50) {
            fetchFavoritesPage(page + 1, collected);
            return;
        }
        m_fetchingFavorites = false;
        m_favoritesFetchedAt = QDateTime::currentMSecsSinceEpoch();
        Tone3000Library::instance().setRemoteFavorites(collected);
    });
}
