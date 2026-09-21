#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

// Plain data for the TONE3000 browser: parsed tones and creators, and the
// search a view asks the API for. No networking and no widgets here.
namespace Tone3000 {

enum class Format { Nam, Ir };
QString formatKey(Format format); // "nam" / "ir"

// A tone ("profile" / IR upload) parsed once from the API's JSON.
struct ToneItem {
    int id = 0;
    QString title;
    QString creator;
    QString gear;
    QString imageUrl;
    QString url;        // absolute page on tone3000.com, may be empty
    QString slug;
    QString description;
    QStringList tags;
    QStringList makes;
    int downloads = 0;
    int favorites = 0;
    int modelCount = 0;
    // Per NAM architecture, when the API says (-1 otherwise).
    int a1Models = -1;
    int a2Models = -1;
    QJsonObject raw;

    static ToneItem fromJson(const QJsonObject& tone);
    // Models that pass the NAM architecture filter ("2", "1", or "" for all).
    int modelCountFor(const QString& architecture) const;
};

struct CreatorItem {
    QString username;
    QString displayName;
    QString avatarUrl;
    QString url;
    bool verified = false;
    int tones = 0;
    int models = 0;
    int downloads = 0;
    int favorites = 0;

    static CreatorItem fromJson(const QJsonObject& user);
};

QString firstImageUrl(const QJsonObject& tone);
QString absoluteToneUrl(const QString& url);
// Case-insensitive match of every word of `text` against title, creator,
// gear, description, tags and makes. Empty text matches everything.
bool matchesText(const ToneItem& tone, const QString& text);
// Which format a stored or fetched tone belongs to, if it says.
// Returns false when the tone does not tell.
bool formatOf(const QJsonObject& tone, Format* format);

// Folder for one tone's files inside the mode's cache: tone_<id>_<slug>.
QString toneFolderName(const QJsonObject& tone);
// File name for one model of a tone; keeps the real file type from its URL.
QString modelFileName(const QJsonObject& model, Format format, bool preview);

// What a remote list shows.
struct Query {
    enum class Source { Catalog, Creators };

    Format format = Format::Nam;
    Source source = Source::Catalog;
    QString text;
    QString creator;          // username; empty = everyone
    QString sort = "trending"; // trending, best-match, downloads, newest, oldest
    // Comma-separated; the API matches any of them. Gear values are amp,
    // amp-cab, pedal, outboard (NAM) and cab, pedal, outboard, space (IR).
    QString gear = "amp-cab";
    QString tags;
    // NAM only.
    QString architecture = "2";
    QString size;
    bool calibrated = false;

    static constexpr int kPageSize = 25;
    static constexpr int kCreatorPageSize = 10; // the API's maximum for users

    QString endpoint() const;
    int pageSize() const { return source == Source::Creators ? kCreatorPageSize : kPageSize; }
    QUrlQuery toUrlQuery(int page) const;
    QString cacheKey(int page) const;
    // The query a new browser starts with.
    static Query defaults(Format format);
    // True when the filters differ from the defaults.
    bool hasFilters() const;
    void resetFilters();
    // Short human summary, e.g. "fender clean · Amp · A2 · Lite".
    QString describe() const;

    QJsonObject toJson() const;
    static Query fromJson(const QJsonObject& object, Format format);

    bool operator==(const Query& other) const = default;
};

} // namespace Tone3000
