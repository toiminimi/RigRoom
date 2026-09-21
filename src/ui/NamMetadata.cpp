#include "NamMetadata.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <algorithm>

namespace NamMetadata {
namespace {

constexpr qint64 kHeadBytes = 64 * 1024;
constexpr qint64 kTailBytes = 16 * 1024;

// The JSON object that starts at `start` ('{'), cut out by matching braces.
QByteArray objectAt(const QByteArray& data, qsizetype start) {
    int depth = 0;
    bool inString = false;
    for (qsizetype i = start; i < data.size(); ++i) {
        const char c = data[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return data.mid(start, i - start + 1);
    }
    return {};
}

QString sizeForChannels(int channels) {
    switch (channels) {
    case 16: return "standard";
    case 12: return "lite";
    case 8: return "feather";
    case 4: return "nano";
    default: return {};
    }
}

// Gear brands worth a tag when they show up in a capture's name or gear.
const QStringList& brands() {
    static const QStringList list{
        "ampeg", "bogner", "boss", "blackstar", "diezel", "dumble", "egnater", "engl", "evh", "fender", "friedman",
        "fulltone", "gibson", "hiwatt", "ibanez", "jet city", "klon", "laney", "marshall", "matchless", "mesa",
        "orange", "peavey", "randall", "revv", "soldano", "splawn", "suhr", "supro", "two-rock", "victory", "vox",
        "hughes & kettner", "dr. z", "carvin", "krank", "framus", "koch", "magnatone", "silvertone", "tone king",
        "electro-harmonix", "mxr", "proco", "maxon", "strymon", "darkglass"};
    return list;
}

QString brandTag(const QString& brand) {
    if (brand == "hughes & kettner") return "hughes-kettner";
    if (brand == "dr. z") return "dr-z";
    if (brand == "jet city") return "jet-city";
    if (brand == "tone king") return "tone-king";
    return brand;
}
} // namespace

Info parse(const QJsonObject& root) {
    Info info;
    const QJsonObject meta = root.value("metadata").toObject();
    auto text = [&meta](const char* key) {
        // TONE3000 writes placeholders such as "t3k-unset" or "T3K-Null" into empty fields.
        const QString value = meta.value(key).toString().trimmed();
        return value.startsWith("t3k", Qt::CaseInsensitive) || value.compare("null", Qt::CaseInsensitive) == 0
            ? QString() : value;
    };
    info.name = text("name");
    info.modeledBy = text("modeled_by");
    info.gearType = text("gear_type");
    info.gearMake = text("gear_make");
    info.gearModel = text("gear_model");
    info.toneType = text("tone_type");
    info.loudness = meta.value("loudness").toDouble();
    info.calibrated = meta.value("input_level_dbu").isDouble();
    info.architecture = root.value("architecture").toString();
    if (info.architecture == "WaveNet") {
        const QJsonArray layers = root.value("config").toObject().value("layers").toArray();
        if (!layers.isEmpty()) info.size = sizeForChannels(layers.first().toObject().value("channels").toInt());
    }
    info.valid = root.contains("metadata") || !info.architecture.isEmpty();
    return info;
}

namespace {
// Top-level "architecture" and "metadata" from the start of the file, reading
// only keys at the first level so nested models (which repeat both) are skipped.
void scanTopLevel(const QByteArray& data, QString* architecture, QByteArray* metadata) {
    int depth = 0;
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data[i];
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
        } else if (c == '"') {
            const qsizetype end = data.indexOf('"', i + 1);
            if (end < 0) return;
            if (depth == 1) {
                const QByteArray key = data.mid(i + 1, end - i - 1);
                qsizetype colon = end + 1;
                while (colon < data.size() && (data[colon] == ' ' || data[colon] == '\n')) ++colon;
                if (colon < data.size() && data[colon] == ':') {
                    qsizetype value = colon + 1;
                    while (value < data.size() && (data[value] == ' ' || data[value] == '\n')) ++value;
                    if (key == "architecture" && value < data.size() && data[value] == '"') {
                        const qsizetype close = data.indexOf('"', value + 1);
                        if (close > value) *architecture = QString::fromUtf8(data.mid(value + 1, close - value - 1));
                    } else if (key == "metadata" && value < data.size() && data[value] == '{') {
                        *metadata = objectAt(data, value);
                    } else if (key == "weights") {
                        return; // the rest is numbers
                    }
                }
            }
            // Skip the string, including escaped quotes.
            qsizetype j = i + 1;
            while (j < data.size() && data[j] != '"') j += data[j] == '\\' ? 2 : 1;
            i = j;
        }
    }
}
} // namespace

Info parseParts(const QByteArray& head, const QByteArray& tail) {
    // Whole small files parse directly.
    const QJsonDocument whole = QJsonDocument::fromJson(head);
    if (whole.isObject()) return parse(whole.object());

    QString architecture;
    QByteArray metadata;
    scanTopLevel(head, &architecture, &metadata);
    // Some trainers write the metadata last, after the weights.
    if (metadata.isEmpty() && !tail.isEmpty()) {
        const qsizetype key = tail.lastIndexOf("\"metadata\"");
        const qsizetype brace = key >= 0 ? tail.indexOf('{', key) : -1;
        if (brace >= 0) metadata = objectAt(tail, brace);
    }
    QJsonObject root;
    const QJsonDocument meta = QJsonDocument::fromJson(metadata);
    if (meta.isObject()) root["metadata"] = meta.object();
    if (!architecture.isEmpty()) root["architecture"] = architecture;
    Info info = parse(root);
    if (architecture == "WaveNet") {
        // Size from the first layer's channels, which come right after.
        static const QRegularExpression channels("\"channels\"\\s*:\\s*(\\d+)");
        const QRegularExpressionMatch c = channels.match(QString::fromUtf8(head.left(32768)));
        if (c.hasMatch()) info.size = sizeForChannels(c.captured(1).toInt());
    }
    return info;
}

Info read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray head = file.read(kHeadBytes);
    QByteArray tail;
    if (file.size() > kHeadBytes) {
        file.seek(std::max<qint64>(kHeadBytes, file.size() - kTailBytes));
        tail = file.readAll();
    }
    return parseParts(head, tail);
}

QString architectureLabel(const Info& info) {
    if (info.architecture.isEmpty()) return {};
    return info.architecture == "SlimmableContainer" ? QStringLiteral("A2") : QStringLiteral("A1");
}

QString normalizedGear(const QString& gearType) {
    const QString g = gearType.toLower().replace('_', '-');
    if (g == "amp-cab" || g == "full-rig" || g == "amp+cab") return "amp-cab";
    if (g == "amp" || g == "pedal" || g == "outboard" || g == "cab") return g;
    if (g.contains("pedal")) return "pedal";
    return {};
}

QString normalizeTag(const QString& tag) {
    if (tag.trimmed().isEmpty()) return {};
    static const QRegularExpression spaces("\\s+");
    static const QRegularExpression unwanted("[^a-z0-9&.+\\-]");
    static const QRegularExpression dashes("-{2,}");
    static const QRegularExpression edges("^-|-$");
    QString t = tag.trimmed().toLower();
    t.replace('_', '-');
    t.replace(spaces, "-");
    t.remove(unwanted);
    t.replace(dashes, "-");
    t.remove(edges);
    if (t.isEmpty() || t.startsWith("t3k") || t == "null" || t == "unset" || t == "none" || t == "nam") return {};
    static const QHash<QString, QString> synonyms{
        {"hi-gain", "high-gain"}, {"higain", "high-gain"}, {"highgain", "high-gain"}, {"hg", "high-gain"},
        {"od", "overdrive"}, {"drive", "overdrive"}, {"dist", "distortion"}, {"heavy-metal", "metal"},
        {"full-rig", "amp-cab"}, {"amp+cab", "amp-cab"}, {"ampcab", "amp-cab"}, {"cleanboost", "boost"},
        {"clean-boost", "boost"}, {"edge", "edge-of-breakup"}, {"eob", "edge-of-breakup"}, {"breakup", "edge-of-breakup"},
        {"rhy", "rhythm"}, {"distortion", "overdrive"}, {"high-gain-metal", "high-gain"}, {"cabinet", "cab"},
        {"room", "space"}, {"hall", "space"}, {"plate", "space"},
        {"a1-nam", "a1"}, {"a2-nam", "a2"}};
    return synonyms.value(t, t);
}

QStringList tagsFromText(const QString& text) {
    static const QRegularExpression separators("[_\\-.,/+()\\[\\]']");
    const QString lower = " " + QString(text).toLower().replace(separators, " ") + " ";
    QStringList tags;
    static const QList<QPair<QString, QString>> words{
        {" clean ", "clean"}, {" crunch ", "crunch"}, {" lead ", "lead"}, {" rhythm ", "rhythm"},
        {" drive ", "overdrive"}, {" overdrive ", "overdrive"}, {" od ", "overdrive"}, {" boost ", "boost"},
        {" boosted ", "boost"}, {" high gain ", "high-gain"}, {" hi gain ", "high-gain"}, {" hg ", "high-gain"},
        {" metal ", "metal"}, {" fuzz ", "fuzz"}, {" di ", "di"}, {" edge of breakup ", "edge-of-breakup"},
        {" blues ", "blues"}, {" jazz ", "jazz"}, {" bass ", "bass"}};
    for (const auto& [word, tag] : words) {
        if (lower.contains(word) && !tags.contains(tag)) tags << tag;
    }
    for (const QString& brand : brands()) {
        if (lower.contains(" " + QString(brand).replace(separators, " ") + " ")) {
            const QString tag = brandTag(brand);
            if (!tags.contains(tag)) tags << tag;
        }
    }
    return tags;
}

QStringList vocabulary(TagCategory category) {
    static const QStringList type{"amp", "amp-cab", "pedal", "outboard", "cab", "space"};
    static const QStringList tone{"clean", "edge-of-breakup", "crunch", "overdrive", "high-gain", "metal", "lead",
                                  "rhythm", "fuzz", "boost", "blues", "jazz", "bass", "acoustic", "ambient"};
    static const QStringList brand = [] {
        QStringList out;
        for (const QString& b : brands()) out << brandTag(b);
        return out;
    }();
    static const QStringList tech{"a1", "a2", "standard", "lite", "feather", "nano", "calibrated", "di"};
    switch (category) {
    case TagCategory::Type: return type;
    case TagCategory::Tone: return tone;
    case TagCategory::Brand: return brand;
    case TagCategory::Tech: return tech;
    case TagCategory::Other: return {};
    }
    return {};
}

TagCategory tagCategory(const QString& tag) {
    static const QHash<QString, TagCategory> index = [] {
        QHash<QString, TagCategory> out;
        for (const TagCategory c : {TagCategory::Type, TagCategory::Tone, TagCategory::Brand, TagCategory::Tech}) {
            for (const QString& t : vocabulary(c)) out.insert(t, c);
        }
        return out;
    }();
    return index.value(tag, TagCategory::Other);
}

QString vocabularyTag(const QString& text) {
    if (text.isEmpty()) return {};
    const QString tag = normalizeTag(text);
    if (tag.isEmpty()) return {};
    if (tagCategory(tag) != TagCategory::Other) return tag;
    // A brand written out ("Mesa/Boogie", "Hughes & Kettner").
    const QStringList found = tagsFromText(text);
    return found.size() == 1 && tagCategory(found.first()) == TagCategory::Brand ? found.first() : QString();
}

QString tagLabel(const QString& tag) {
    static const QHash<QString, QString> labels{
        {"amp-cab", "Amp + Cab"}, {"high-gain", "High gain"}, {"edge-of-breakup", "Edge of breakup"},
        {"a1", "A1"}, {"a2", "A2"}, {"di", "DI"}, {"evh", "EVH"}, {"mxr", "MXR"}, {"hughes-kettner", "Hughes & Kettner"},
        {"dr-z", "Dr. Z"}, {"two-rock", "Two-Rock"}, {"jet-city", "Jet City"}, {"tone-king", "Tone King"},
        {"electro-harmonix", "Electro-Harmonix"}, {"space", "Space"}};
    const auto it = labels.constFind(tag);
    if (it != labels.constEnd()) return it.value();
    QString label = tag;
    label.replace('-', ' ');
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    return label;
}

QStringList autoTags(const Info& info, const QString& fileName) {
    QStringList tags;
    auto add = [&tags](const QString& text) {
        const QString t = vocabularyTag(text);
        if (!t.isEmpty() && !tags.contains(t)) tags << t;
    };
    add(normalizedGear(info.gearType));
    // tone_type is free text; keep short values that read like a tag.
    if (info.toneType.size() <= 24) add(info.toneType);
    const QString label = architectureLabel(info);
    if (!label.isEmpty()) add(label);
    add(info.size);
    if (info.calibrated) add("calibrated");
    const QString described = QStringList{info.name, info.gearMake, info.gearModel, QFileInfo(fileName).completeBaseName()}
                                  .join(' ');
    for (const QString& tag : tagsFromText(described)) add(tag);
    return tags;
}

} // namespace NamMetadata
