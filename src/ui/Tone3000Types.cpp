#include "Tone3000Types.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace Tone3000 {

QString formatKey(Format format) {
    return format == Format::Ir ? QStringLiteral("ir") : QStringLiteral("nam");
}

static QStringList objectNames(const QJsonObject& tone, const QString& key) {
    QStringList names;
    for (const QJsonValue& value : tone.value(key).toArray()) {
        const QString name = value.isString() ? value.toString() : value.toObject().value("name").toString();
        if (!name.isEmpty()) names << name;
    }
    return names;
}

QString firstImageUrl(const QJsonObject& tone) {
    for (const QJsonValue& image : tone.value("images").toArray()) {
        const QString url = image.isString() ? image.toString() : image.toObject().value("url").toString();
        if (!url.isEmpty()) return url;
    }
    return {};
}

QString absoluteToneUrl(const QString& url) {
    if (url.startsWith('/')) return "https://www.tone3000.com" + url;
    return url;
}

ToneItem ToneItem::fromJson(const QJsonObject& tone) {
    ToneItem item;
    item.id = tone.value("id").toInt();
    item.title = tone.value("title").toString();
    const QJsonValue user = tone.value("user");
    item.creator = user.isObject() ? user.toObject().value("username").toString() : tone.value("username").toString();
    item.gear = tone.value("gear").toString();
    item.imageUrl = firstImageUrl(tone);
    item.url = absoluteToneUrl(tone.value("url").toString());
    item.slug = tone.value("slug").toString();
    item.description = tone.value("description").toString();
    item.tags = objectNames(tone, "tags");
    item.makes = objectNames(tone, "makes");
    item.downloads = tone.value("downloads_count").toInt();
    item.favorites = tone.value("favorites_count").toInt();
    item.modelCount = tone.value("models_count").toInt(tone.value("model_count").toInt());
    item.a1Models = tone.value("a1_models_count").toInt(-1);
    item.a2Models = tone.value("a2_models_count").toInt(-1);
    item.raw = tone;
    return item;
}

int ToneItem::modelCountFor(const QString& architecture) const {
    if (architecture == "2" && a2Models >= 0) return a2Models;
    if (architecture == "1" && a1Models >= 0) return a1Models;
    return modelCount;
}

CreatorItem CreatorItem::fromJson(const QJsonObject& user) {
    CreatorItem item;
    item.username = user.value("username").toString();
    item.displayName = user.value("display_name").toString();
    if (item.displayName.isEmpty()) item.displayName = item.username;
    item.avatarUrl = user.value("avatar_url").toString();
    item.url = user.value("url").toString();
    if (item.url.isEmpty() && !item.username.isEmpty()) item.url = "https://www.tone3000.com/" + item.username;
    item.url = absoluteToneUrl(item.url);
    item.verified = user.value("is_verified").toBool();
    item.tones = user.value("tones_count").toInt();
    item.models = user.value("models_count").toInt();
    item.downloads = user.value("downloads_count").toInt();
    item.favorites = user.value("favorites_count").toInt();
    return item;
}

bool matchesText(const ToneItem& tone, const QString& text) {
    const QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty()) return true;
    const QString haystack = QStringList{tone.title, tone.creator, tone.gear, tone.description,
                                         tone.tags.join(' '), tone.makes.join(' ')}.join(' ');
    for (const QString& word : words) {
        if (!haystack.contains(word, Qt::CaseInsensitive)) return false;
    }
    return true;
}

bool formatOf(const QJsonObject& tone, Format* format) {
    for (const char* key : {"rigroom_format", "format", "platform"}) {
        const QString value = tone.value(key).toString().toLower();
        if (value == "ir") { *format = Format::Ir; return true; }
        if (value == "nam") { *format = Format::Nam; return true; }
    }
    return false;
}

static QString slugify(QString text) {
    text = text.toLower();
    text.replace(QRegularExpression("[^a-z0-9]+"), "-");
    text.remove(QRegularExpression("^-|-$"));
    return text;
}

QString toneFolderName(const QJsonObject& tone) {
    const int toneId = tone.value("id").toInt();
    QString slug = tone.value("slug").toString();
    if (slug.isEmpty()) slug = slugify(tone.value("title").toString());
    slug.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
    if (slug.isEmpty()) slug = "profile";
    return QString("tone_%1_%2").arg(toneId > 0 ? QString::number(toneId) : "0", slug);
}

QString modelFileName(const QJsonObject& model, Format format, bool preview) {
    static const QStringList known{"nam", "json", "wav", "flac", "aif", "aiff"};
    QString ext = QFileInfo(QUrl(model.value("model_url").toString()).path()).suffix().toLower();
    if (!known.contains(ext)) ext = format == Format::Ir ? "wav" : "nam";
    QString name = model.value("name").toString();
    name.replace(QRegularExpression("[^a-zA-Z0-9_\\-.]"), "_");
    if (name.isEmpty()) name = "file";
    if (!name.endsWith("." + ext, Qt::CaseInsensitive)) name += "." + ext;
    return preview ? "preview_" + name : name;
}

// ─── Query ───────────────────────────────────────────────────────────────────

QString Query::endpoint() const {
    return source == Source::Creators ? QStringLiteral("https://www.tone3000.com/api/v1/users")
                                      : QStringLiteral("https://www.tone3000.com/api/v1/tones/search");
}

QUrlQuery Query::toUrlQuery(int page) const {
    QUrlQuery q;
    const QString trimmed = text.trimmed();
    if (source == Source::Creators) {
        if (!trimmed.isEmpty()) q.addQueryItem("query", trimmed);
        q.addQueryItem("sort", "downloads");
        q.addQueryItem("page", QString::number(page));
        q.addQueryItem("page_size", QString::number(kCreatorPageSize));
        return q;
    }
    const bool nam = format == Format::Nam;
    if (!trimmed.isEmpty()) q.addQueryItem("query", trimmed);
    q.addQueryItem("page", QString::number(page));
    q.addQueryItem("page_size", QString::number(kPageSize));
    q.addQueryItem("format", formatKey(format));
    if (!creator.isEmpty()) q.addQueryItem("creators", creator);
    q.addQueryItem("sort", sort == "downloads" ? QStringLiteral("downloads-all-time") : sort);
    if (!gear.isEmpty()) q.addQueryItem("gears", gear);
    if (!tags.isEmpty()) q.addQueryItem("tags", tags);
    if (nam) {
        if (!architecture.isEmpty()) q.addQueryItem("architecture", architecture);
        if (!size.isEmpty()) q.addQueryItem("sizes", size);
        if (calibrated) q.addQueryItem("calibrated", "true");
    }
    return q;
}

QString Query::cacheKey(int page) const {
    return endpoint() + "?" + toUrlQuery(page).toString(QUrl::FullyEncoded);
}

Query Query::defaults(Format format) {
    Query q;
    q.format = format;
    if (format == Format::Ir) q.gear.clear();
    return q;
}

bool Query::hasFilters() const {
    const Query d = defaults(format);
    if (gear != d.gear || !tags.isEmpty()) return true;
    return format == Format::Nam && (architecture != d.architecture || !size.isEmpty() || calibrated);
}

void Query::resetFilters() {
    const Query d = defaults(format);
    gear = d.gear;
    tags.clear();
    architecture = d.architecture;
    size.clear();
    calibrated = false;
}

static QString labelFor(const QString& value, std::initializer_list<std::pair<const char*, const char*>> labels) {
    for (const auto& [key, label] : labels) {
        if (value == QLatin1String(key)) return QString::fromUtf8(label);
    }
    return value;
}

QString Query::describe() const {
    QStringList parts;
    if (!text.trimmed().isEmpty()) parts << "\"" + text.trimmed() + "\"";
    if (!creator.isEmpty()) parts << "@" + creator;
    QStringList gears;
    for (const QString& g : gear.split(',', Qt::SkipEmptyParts)) {
        gears << labelFor(g, {{"amp-cab", "Amp + Cab"}, {"amp", "Amp"}, {"cab", "Cab"}, {"pedal", "Pedal"},
                              {"outboard", "Outboard"}, {"space", "Space"}});
    }
    if (!gears.isEmpty()) parts << gears.join(" / ");
    if (!tags.isEmpty()) parts << "#" + tags.split(',', Qt::SkipEmptyParts).join(" #");
    if (format == Format::Nam) {
        parts << labelFor(architecture, {{"2", "A2"}, {"1", "A1"}, {"custom", "Custom"}, {"", "All architectures"}});
        if (!size.isEmpty()) parts << size.left(1).toUpper() + size.mid(1);
        if (calibrated) parts << "Calibrated";
    }
    parts << labelFor(sort, {{"trending", "Trending"}, {"best-match", "Best match"}, {"downloads", "Most downloaded"},
                             {"newest", "Newest"}, {"oldest", "Oldest"}});
    return parts.join(QString::fromUtf8(" · "));
}

QJsonObject Query::toJson() const {
    return QJsonObject{
        {"source", source == Source::Creators ? "creators" : "catalog"},
        {"text", text},
        {"creator", creator},
        {"sort", sort},
        {"gear", gear},
        {"tags", tags},
        {"architecture", architecture},
        {"size", size},
        {"calibrated", calibrated},
    };
}

Query Query::fromJson(const QJsonObject& object, Format format) {
    Query q = defaults(format);
    q.source = object.value("source").toString() == "creators" ? Source::Creators : Source::Catalog;
    q.text = object.value("text").toString();
    q.creator = object.value("creator").toString();
    static const QStringList sorts{"trending", "best-match", "downloads", "newest", "oldest"};
    const QString sort = object.value("sort").toString(q.sort);
    if (sorts.contains(sort)) q.sort = sort;
    if (object.contains("gear")) {
        // Keep only gear types this format has.
        const QStringList valid = format == Format::Ir ? QStringList{"cab", "pedal", "outboard", "space"}
                                                       : QStringList{"amp", "amp-cab", "pedal", "outboard", "cab"};
        QStringList gears;
        for (const QString& g : object.value("gear").toString().split(',', Qt::SkipEmptyParts)) {
            if (valid.contains(g)) gears << g;
        }
        q.gear = gears.join(',');
    }
    // Older settings had one "character", sent as search words; it is a tag.
    q.tags = object.value("tags").toString(object.value("character").toString());
    q.architecture = object.value("architecture").toString(q.architecture);
    q.size = object.value("size").toString();
    q.calibrated = object.value("calibrated").toBool();
    return q;
}

} // namespace Tone3000
