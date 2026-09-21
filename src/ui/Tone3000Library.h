#pragma once
#include "Tone3000Types.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>

// What the TONE3000 browser remembers about TONE3000 itself: favorite tones
// (kept locally and on the user's account) and saved searches. Loaded once and
// written only when something changes. Files on disk are CaptureLibrary's.
class Tone3000Library : public QObject {
    Q_OBJECT
public:
    struct SavedSearch {
        QString name;
        Tone3000::Query query;
    };

    // The process-wide library in ~/.config/RigRoom.
    static Tone3000Library& instance();
    explicit Tone3000Library(const QString& configDir, QObject* parent = nullptr);

    // Favorites: saved locally, plus those on the user's TONE3000 account.
    bool isFavorite(int toneId) const;
    QList<QJsonObject> favorites(Tone3000::Format format) const;
    void setFavorite(const QJsonObject& tone, const QJsonArray& models, Tone3000::Format format, bool favorite);
    void setRemoteFavorites(const QJsonArray& tones);
    bool hasRemoteFavorites() const { return m_remoteLoaded; }
    // Models stored with a favorite, if any.
    QJsonArray storedModels(int toneId) const;

    QList<SavedSearch> savedSearches(Tone3000::Format format) const;
    void addSavedSearch(const QString& name, const Tone3000::Query& query);
    void removeSavedSearch(Tone3000::Format format, int index);

signals:
    void favoritesChanged();
    void savedSearchesChanged();

private:
    void loadFavorites();
    void saveFavorites() const;
    void loadSavedSearches();
    void saveSavedSearches() const;
    static bool inFormat(const QJsonObject& tone, Tone3000::Format format, bool assumeNam);

    QString m_configDir;
    QList<QJsonObject> m_localFavorites;   // favorites.json, each with "models"
    QList<QJsonObject> m_remoteFavorites;  // from the TONE3000 account
    QSet<int> m_favoriteIds;
    bool m_remoteLoaded = false;
    QList<QPair<Tone3000::Format, SavedSearch>> m_saved;
};
