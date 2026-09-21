#include "Tone3000Library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

using Tone3000::Format;

namespace {
QJsonArray readArray(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}

void writeJson(const QString& path, const QJsonDocument& document) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(document.toJson());
    file.commit();
}
} // namespace

Tone3000Library& Tone3000Library::instance() {
    static auto* library = new Tone3000Library(QDir::homePath() + "/.config/RigRoom");
    return *library;
}

Tone3000Library::Tone3000Library(const QString& configDir, QObject* parent)
    : QObject(parent), m_configDir(configDir) {
    loadFavorites();
    loadSavedSearches();
}

bool Tone3000Library::inFormat(const QJsonObject& tone, Format format, bool assumeNam) {
    Format stored;
    if (Tone3000::formatOf(tone, &stored)) return stored == format;
    // Local favorites from before IR support are all NAM captures; tones from
    // the account that do not say are shown in both browsers.
    return assumeNam ? format == Format::Nam : true;
}

// ─── Favorites ───────────────────────────────────────────────────────────────

void Tone3000Library::loadFavorites() {
    m_localFavorites.clear();
    for (const QJsonValue& value : readArray(m_configDir + "/favorites.json")) {
        const QJsonObject tone = value.toObject();
        if (tone.value("id").toInt() == 0) continue;
        m_localFavorites << tone;
        m_favoriteIds.insert(tone.value("id").toInt());
    }
}

void Tone3000Library::saveFavorites() const {
    QJsonArray array;
    for (const QJsonObject& tone : m_localFavorites) array.append(tone);
    writeJson(m_configDir + "/favorites.json", QJsonDocument(array));
}

bool Tone3000Library::isFavorite(int toneId) const {
    return m_favoriteIds.contains(toneId);
}

QList<QJsonObject> Tone3000Library::favorites(Format format) const {
    QList<QJsonObject> out;
    QSet<int> seen;
    for (const QJsonObject& tone : m_remoteFavorites) {
        const int id = tone.value("id").toInt();
        if (!m_favoriteIds.contains(id) || seen.contains(id) || !inFormat(tone, format, false)) continue;
        seen.insert(id);
        out << tone;
    }
    for (const QJsonObject& tone : m_localFavorites) {
        const int id = tone.value("id").toInt();
        if (seen.contains(id) || !inFormat(tone, format, true)) continue;
        seen.insert(id);
        out << tone;
    }
    return out;
}

void Tone3000Library::setFavorite(const QJsonObject& tone, const QJsonArray& models, Format format, bool favorite) {
    const int id = tone.value("id").toInt();
    if (id == 0) return;
    m_localFavorites.erase(std::remove_if(m_localFavorites.begin(), m_localFavorites.end(),
                                          [id](const QJsonObject& t) { return t.value("id").toInt() == id; }),
                           m_localFavorites.end());
    if (favorite) {
        QJsonObject stored = tone;
        stored["models"] = models;
        stored["rigroom_format"] = Tone3000::formatKey(format);
        m_localFavorites << stored;
        m_favoriteIds.insert(id);
    } else {
        m_remoteFavorites.erase(std::remove_if(m_remoteFavorites.begin(), m_remoteFavorites.end(),
                                               [id](const QJsonObject& t) { return t.value("id").toInt() == id; }),
                                m_remoteFavorites.end());
        m_favoriteIds.remove(id);
    }
    saveFavorites();
    emit favoritesChanged();
}

void Tone3000Library::setRemoteFavorites(const QJsonArray& tones) {
    m_remoteFavorites.clear();
    m_favoriteIds.clear();
    for (const QJsonValue& value : tones) {
        const QJsonObject tone = value.toObject();
        const int id = tone.value("id").toInt();
        if (id == 0) continue;
        m_remoteFavorites << tone;
        m_favoriteIds.insert(id);
    }
    for (const QJsonObject& tone : m_localFavorites) m_favoriteIds.insert(tone.value("id").toInt());
    m_remoteLoaded = true;
    emit favoritesChanged();
}

QJsonArray Tone3000Library::storedModels(int toneId) const {
    for (const QJsonObject& tone : m_localFavorites) {
        if (tone.value("id").toInt() == toneId) return tone.value("models").toArray();
    }
    return {};
}

// ─── Saved searches ──────────────────────────────────────────────────────────

void Tone3000Library::loadSavedSearches() {
    m_saved.clear();
    for (const QJsonValue& value : readArray(m_configDir + "/tone3000_saved_searches.json")) {
        const QJsonObject object = value.toObject();
        const Format format = object.value("format").toString() == "ir" ? Format::Ir : Format::Nam;
        SavedSearch search{object.value("name").toString(), Tone3000::Query::fromJson(object.value("query").toObject(), format)};
        if (search.name.isEmpty()) search.name = search.query.describe();
        m_saved.append({format, search});
    }
}

void Tone3000Library::saveSavedSearches() const {
    QJsonArray array;
    for (const auto& [format, search] : m_saved) {
        array.append(QJsonObject{{"name", search.name},
                                 {"format", Tone3000::formatKey(format)},
                                 {"query", search.query.toJson()}});
    }
    writeJson(m_configDir + "/tone3000_saved_searches.json", QJsonDocument(array));
}

QList<Tone3000Library::SavedSearch> Tone3000Library::savedSearches(Format format) const {
    QList<SavedSearch> out;
    for (const auto& [f, search] : m_saved) {
        if (f == format) out << search;
    }
    return out;
}

void Tone3000Library::addSavedSearch(const QString& name, const Tone3000::Query& query) {
    for (auto& [format, search] : m_saved) {
        if (format == query.format && search.query == query) {
            search.name = name;
            saveSavedSearches();
            emit savedSearchesChanged();
            return;
        }
    }
    m_saved.append({query.format, SavedSearch{name.isEmpty() ? query.describe() : name, query}});
    saveSavedSearches();
    emit savedSearchesChanged();
}

void Tone3000Library::removeSavedSearch(Format format, int index) {
    int seen = 0;
    for (int i = 0; i < m_saved.size(); ++i) {
        if (m_saved[i].first != format) continue;
        if (seen++ == index) {
            m_saved.removeAt(i);
            saveSavedSearches();
            emit savedSearchesChanged();
            return;
        }
    }
}
