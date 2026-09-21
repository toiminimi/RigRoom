#include "CaptureLibrary.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <functional>

using Tone3000::Format;
using NamMetadata::TagCategory;

namespace {
constexpr int kMaxAutoTags = 12;

bool formatOfPath(const QString& path, Format* format) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (CaptureLibrary::extensions(Format::Nam).contains(suffix)) { *format = Format::Nam; return true; }
    if (CaptureLibrary::extensions(Format::Ir).contains(suffix)) { *format = Format::Ir; return true; }
    return false;
}

QString prettyName(const QString& path) {
    QString name = QFileInfo(path).completeBaseName();
    name.replace('_', ' ');
    return name.simplified();
}

QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeObject(const QString& path, const QJsonObject& object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(object).toJson());
    file.commit();
}

QStringList toStringList(const QJsonValue& value) {
    QStringList list;
    for (const QJsonValue& v : value.toArray()) list << v.toString();
    return list;
}

const QRegularExpression& toneFolderPattern() {
    static const QRegularExpression pattern("^tone_(\\d+)_");
    return pattern;
}
} // namespace

// ─── Construction and persistence ───────────────────────────────────────────

CaptureLibrary& CaptureLibrary::instance() {
    static auto* library = new CaptureLibrary(QDir::homePath() + "/.config/RigRoom",
                                              QDir::homePath() + "/.cache/RigRoom/tone3000",
                                              QDir::homePath() + "/.config/RigRoom/presets");
    return *library;
}

CaptureLibrary::CaptureLibrary(const QString& configDir, const QString& cacheDir, const QString& presetsDir,
                               QObject* parent)
    : QObject(parent), m_configDir(configDir), m_cacheDir(cacheDir), m_presetsDir(presetsDir) {
    load();
}

QStringList CaptureLibrary::extensions(Format format) {
    return format == Format::Ir ? QStringList{"wav", "flac", "aif", "aiff"} : QStringList{"nam"};
}

void CaptureLibrary::load() {
    const QString path = m_configDir + "/capture-library.json";
    const QJsonObject root = readObject(path);
    if (root.isEmpty()) return;
    if (root.value("version").toInt() != 2) {
        // A version this build does not read: keep it aside rather than overwrite it.
        QFile::rename(path, m_configDir + QString("/capture-library.v%1.json").arg(root.value("version").toInt()));
        return;
    }
    for (const QString& folder : toStringList(root.value("folders"))) {
        if (!folder.isEmpty() && !m_folders.contains(folder)) m_folders << folder;
    }
    m_lastFolder = root.value("lastFolder").toString(kDefaultFolder);
    const QJsonObject packs = root.value("packs").toObject();
    for (auto it = packs.begin(); it != packs.end(); ++it) {
        const QJsonObject p = it.value().toObject();
        m_packs.insert(it.key(), Pack{it.key(), p.value("title").toString(), p.value("creator").toString(),
                                      p.value("description").toString()});
    }
    if (!m_folders.contains(m_lastFolder)) m_lastFolder = kDefaultFolder;
    const QJsonObject entries = root.value("entries").toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        UserData data;
        data.name = o.value("name").toString();
        data.tags = toStringList(o.value("tags"));
        data.hiddenTags = toStringList(o.value("hiddenTags"));
        data.notes = o.value("notes").toString();
        data.rating = std::clamp(o.value("rating").toInt(), 0, 5);
        // Favorites were folded into ratings: an unrated favorite keeps five stars.
        if (o.value("favorite").toBool() && data.rating == 0) data.rating = 5;
        data.lastUsed = static_cast<qint64>(o.value("lastUsed").toDouble());
        data.addedAt = static_cast<qint64>(o.value("addedAt").toDouble());
        const QJsonObject gear = o.value("gear").toObject();
        data.gear = Gear{gear.value("type").toString(), gear.value("make").toString(), gear.value("model").toString(),
                         gear.value("tone").toString()};
        data.folder = o.value("folder").toString();
        data.pack = o.value("pack").toString();
        if (!m_packs.contains(data.pack)) data.pack.clear();
        if (!m_folders.contains(data.folder)) data.folder = kDefaultFolder;
        m_user.insert(it.key(), data);
    }
}

void CaptureLibrary::save() const {
    QJsonObject entries;
    for (auto it = m_user.begin(); it != m_user.end(); ++it) {
        const UserData& d = it.value();
        QJsonObject o{{"addedAt", static_cast<double>(d.addedAt)}};
        if (!d.name.isEmpty()) o["name"] = d.name;
        if (!d.tags.isEmpty()) o["tags"] = QJsonArray::fromStringList(d.tags);
        if (!d.hiddenTags.isEmpty()) o["hiddenTags"] = QJsonArray::fromStringList(d.hiddenTags);
        if (!d.notes.isEmpty()) o["notes"] = d.notes;
        if (d.rating > 0) o["rating"] = d.rating;
        if (d.lastUsed != 0) o["lastUsed"] = static_cast<double>(d.lastUsed);
        if (!d.gear.isEmpty()) {
            o["gear"] = QJsonObject{{"type", d.gear.type}, {"make", d.gear.make}, {"model", d.gear.model},
                                    {"tone", d.gear.tone}};
        }
        o["folder"] = d.folder.isEmpty() ? QString(kDefaultFolder) : d.folder;
        if (!d.pack.isEmpty()) o["pack"] = d.pack;
        entries.insert(it.key(), o);
    }
    // The previous save stays as a backup.
    const QString path = m_configDir + "/capture-library.json";
    if (QFile::exists(path)) {
        QFile::remove(path + ".bak");
        QFile::copy(path, path + ".bak");
    }
    writeObject(path,
                QJsonObject{{"version", 2},
                            {"folders", QJsonArray::fromStringList(m_folders)},
                            {"lastFolder", m_lastFolder},
                            {"packs", [this] {
                                 QJsonObject packs;
                                 for (const Pack& p : m_packs) {
                                     packs.insert(p.id, QJsonObject{{"title", p.title}, {"creator", p.creator},
                                                                    {"description", p.description}});
                                 }
                                 return packs;
                             }()},
                            {"entries", entries}});
}

void CaptureLibrary::changedData() {
    m_tagCache.clear();
    save();
    emit changed();
}

// ─── Queries ─────────────────────────────────────────────────────────────────

QList<CaptureLibrary::File> CaptureLibrary::files(Format format) const {
    QList<File> out;
    for (const File& f : m_files) {
        if (f.format == format) out << f;
    }
    return out;
}

const CaptureLibrary::File* CaptureLibrary::file(const QString& path) const {
    const auto it = m_index.constFind(path);
    return it == m_index.constEnd() ? nullptr : &m_files[it.value()];
}

bool CaptureLibrary::hasTone(int toneId, Format format) const {
    if (toneId == 0) return false;
    return std::any_of(m_files.begin(), m_files.end(),
                       [toneId, format](const File& f) { return f.toneId == toneId && f.format == format; });
}

QList<CaptureLibrary::File> CaptureLibrary::importCandidates(Format format) const {
    QList<File> out;
    for (const File& f : m_candidates) {
        if (f.format == format && !m_user.contains(f.path)) out << f;
    }
    return out;
}

QString CaptureLibrary::displayName(const File& file) const {
    const QString name = m_user.value(file.path).name;
    return name.isEmpty() ? file.originalName : name;
}

QString CaptureLibrary::creator(const File& file) const {
    const QString user = file.tone.value("user").toObject().value("username").toString();
    return !user.isEmpty() ? user : file.nam.modeledBy;
}

QStringList CaptureLibrary::tags(const File& file) const {
    const UserData d = m_user.value(file.path);
    QStringList autos = file.autoTags;
    auto replace = [&autos](TagCategory category, const QString& tag) {
        if (tag.isEmpty()) return;
        autos.erase(std::remove_if(autos.begin(), autos.end(),
                                   [category](const QString& t) { return NamMetadata::tagCategory(t) == category; }),
                    autos.end());
        autos.prepend(tag);
    };
    replace(TagCategory::Type, NamMetadata::vocabularyTag(d.gear.type));
    QString brand = NamMetadata::vocabularyTag(d.gear.make);
    if (brand.isEmpty() && !d.gear.make.isEmpty()) {
        for (const QString& t : NamMetadata::tagsFromText(d.gear.make + " " + d.gear.model)) {
            if (NamMetadata::tagCategory(t) == TagCategory::Brand) { brand = t; break; }
        }
    }
    replace(TagCategory::Brand, brand);
    replace(TagCategory::Tone, NamMetadata::vocabularyTag(d.gear.tone));
    QStringList out = d.tags;
    for (const QString& tag : autos) {
        if (!out.contains(tag) && !d.hiddenTags.contains(tag)) out << tag;
    }
    return out;
}

QMap<TagCategory, QList<QPair<QString, int>>> CaptureLibrary::tagsByCategory(Format format) const {
    const auto cached = m_tagCache.constFind(int(format));
    if (cached != m_tagCache.constEnd()) return cached.value();
    QMap<TagCategory, QHash<QString, int>> counts;
    for (const File& f : m_files) {
        if (f.format != format) continue;
        for (const QString& tag : tags(f)) ++counts[NamMetadata::tagCategory(tag)][tag];
    }
    QMap<TagCategory, QList<QPair<QString, int>>> out;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        QList<QPair<QString, int>>& list = out[it.key()];
        for (auto c = it.value().begin(); c != it.value().end(); ++c) list.append({c.key(), c.value()});
        std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
    }
    m_tagCache.insert(int(format), out);
    return out;
}

// ─── Changes ─────────────────────────────────────────────────────────────────

void CaptureLibrary::add(const QStringList& paths) {
    add(paths, AddOptions());
}

void CaptureLibrary::add(const QStringList& paths, const AddOptions& options) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString folder = options.folder.isEmpty() ? m_lastFolder : options.folder;
    if (!m_folders.contains(folder)) m_folders << folder;
    m_lastFolder = folder;
    QStringList tags;
    for (const QString& tag : options.tags) {
        const QString t = NamMetadata::normalizeTag(tag);
        if (!t.isEmpty() && !tags.contains(t)) tags << t;
    }
    bool added = false;
    for (const QString& path : paths) {
        const QString clean = QFileInfo(path).absoluteFilePath();
        Format format;
        if (clean.isEmpty() || m_user.contains(clean) || !formatOfPath(clean, &format)) continue;
        UserData data;
        data.addedAt = now;
        data.folder = folder;
        data.tags = tags;
        data.gear = options.gear;
        data.rating = std::clamp(options.rating, 0, 5);
        if (m_packs.contains(options.pack)) data.pack = options.pack;
        m_user.insert(clean, data);
        added = true;
    }
    if (!added) return;
    save();
    rescan();
}

QString CaptureLibrary::folderOf(const QString& path) const {
    const QString folder = m_user.value(path).folder;
    return folder.isEmpty() ? QString(kDefaultFolder) : folder;
}

void CaptureLibrary::addFolder(const QString& name) {
    const QString clean = name.trimmed();
    if (clean.isEmpty() || m_folders.contains(clean)) return;
    m_folders << clean;
    changedData();
}

void CaptureLibrary::renameFolder(const QString& from, const QString& to) {
    const QString clean = to.trimmed();
    if (from == kDefaultFolder || clean.isEmpty() || !m_folders.contains(from) || m_folders.contains(clean)) return;
    m_folders[m_folders.indexOf(from)] = clean;
    for (UserData& data : m_user) {
        if (data.folder == from) data.folder = clean;
    }
    if (m_lastFolder == from) m_lastFolder = clean;
    changedData();
}

void CaptureLibrary::removeFolder(const QString& name) {
    if (name == kDefaultFolder || !m_folders.contains(name)) return;
    m_folders.removeAll(name);
    // Its captures stay in the library, back in Imported.
    for (UserData& data : m_user) {
        if (data.folder == name) data.folder = kDefaultFolder;
    }
    if (m_lastFolder == name) m_lastFolder = kDefaultFolder;
    changedData();
}

void CaptureLibrary::moveToFolder(const QStringList& paths, const QString& folder) {
    if (folder.isEmpty()) return;
    if (!m_folders.contains(folder)) m_folders << folder;
    for (const QString& path : paths) {
        if (m_user.contains(path)) m_user[path].folder = folder;
    }
    changedData();
}

QString CaptureLibrary::createPack(const QString& title, const QString& creator) {
    QString id;
    do {
        id = QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + QString::number(m_packs.size());
    } while (m_packs.contains(id));
    m_packs.insert(id, Pack{id, title.trimmed().isEmpty() ? QString("New pack") : title.trimmed(), creator.trimmed(), {}});
    changedData();
    return id;
}

void CaptureLibrary::updatePack(const Pack& pack) {
    if (!m_packs.contains(pack.id)) return;
    Pack updated = pack;
    if (updated.title.trimmed().isEmpty()) updated.title = m_packs.value(pack.id).title;
    m_packs.insert(pack.id, updated);
    changedData();
}

void CaptureLibrary::setPack(const QStringList& paths, const QString& id) {
    if (!id.isEmpty() && !m_packs.contains(id)) return;
    for (const QString& path : paths) {
        if (m_user.contains(path)) m_user[path].pack = id;
    }
    // A pack nothing is in is gone.
    QSet<QString> used;
    for (const UserData& data : m_user) used.insert(data.pack);
    for (auto it = m_packs.begin(); it != m_packs.end();) {
        it = used.contains(it.key()) ? std::next(it) : m_packs.erase(it);
    }
    changedData();
}

void CaptureLibrary::apply(const QStringList& paths, const Changes& changes) {
    if (!changes.folder.isEmpty() && !m_folders.contains(changes.folder)) m_folders << changes.folder;
    QStringList addTags;
    for (const QString& tag : changes.addTags) {
        const QString t = NamMetadata::normalizeTag(tag);
        if (!t.isEmpty()) addTags << t;
    }
    for (const QString& path : paths) {
        if (!m_user.contains(path)) continue;
        UserData& d = m_user[path];
        if (!changes.folder.isEmpty()) d.folder = changes.folder;
        if (changes.pack && (changes.pack->isEmpty() || m_packs.contains(*changes.pack))) d.pack = *changes.pack;
        if (changes.rating) d.rating = std::clamp(*changes.rating, 0, 5);
        if (changes.type) d.gear.type = *changes.type;
        if (changes.make) d.gear.make = *changes.make;
        if (changes.model) d.gear.model = *changes.model;
        if (changes.tone) d.gear.tone = *changes.tone;
        if (changes.notes) d.notes = *changes.notes;
        if (changes.name) {
            const File* named = file(path);
            const QString trimmed = changes.name->trimmed();
            d.name = named && trimmed == named->originalName ? QString() : trimmed;
        }
        const File* f = file(path);
        for (const QString& tag : changes.removeTags) {
            d.tags.removeAll(tag);
            if (f && f->autoTags.contains(tag) && !d.hiddenTags.contains(tag)) d.hiddenTags << tag;
        }
        for (const QString& tag : addTags) {
            d.hiddenTags.removeAll(tag);
            if (!d.tags.contains(tag)) d.tags << tag;
        }
    }
    if (changes.pack) {
        setPack({}, QString()); // drops packs left empty, and saves
        return;
    }
    changedData();
}

void CaptureLibrary::remove(const QStringList& paths, bool deleteFiles) {
    for (const QString& path : paths) {
        m_user.remove(path);
        // Only RigRoom's own downloads are ever deleted.
        if (deleteFiles && path.startsWith(m_cacheDir + "/")) QFile::remove(path);
    }
    save();
    rescan();
}

void CaptureLibrary::setName(const QString& path, const QString& name) {
    if (!m_user.contains(path)) return;
    const File* f = file(path);
    const QString trimmed = name.trimmed();
    m_user[path].name = f && trimmed == f->originalName ? QString() : trimmed;
    changedData();
}

void CaptureLibrary::setNotes(const QString& path, const QString& notes) {
    if (!m_user.contains(path) || m_user[path].notes == notes) return;
    m_user[path].notes = notes;
    changedData();
}

void CaptureLibrary::setRating(const QStringList& paths, int rating) {
    for (const QString& path : paths) {
        if (m_user.contains(path)) m_user[path].rating = std::clamp(rating, 0, 5);
    }
    changedData();
}

void CaptureLibrary::setGear(const QString& path, const Gear& gear) {
    if (!m_user.contains(path)) return;
    m_user[path].gear = gear;
    changedData();
}

void CaptureLibrary::addTag(const QStringList& paths, const QString& tag) {
    const QString normalized = NamMetadata::normalizeTag(tag);
    if (normalized.isEmpty()) return;
    for (const QString& path : paths) {
        if (!m_user.contains(path)) continue;
        UserData& data = m_user[path];
        data.hiddenTags.removeAll(normalized);
        if (!data.tags.contains(normalized)) data.tags << normalized;
    }
    changedData();
}

void CaptureLibrary::removeTag(const QString& path, const QString& tag) {
    if (!m_user.contains(path)) return;
    UserData& data = m_user[path];
    data.tags.removeAll(tag);
    const File* f = file(path);
    if (f && tags(*f).contains(tag) && !data.hiddenTags.contains(tag)) data.hiddenTags << tag;
    changedData();
}

void CaptureLibrary::markUsed(const QString& path) {
    if (!m_user.contains(path)) return;
    m_user[path].lastUsed = QDateTime::currentMSecsSinceEpoch();
    changedData();
}

void CaptureLibrary::recordDownload(const QJsonObject& tone, const QJsonArray& models, Format format) {
    if (tone.value("id").toInt() == 0) return;
    QJsonObject storedTone;
    for (auto it = tone.begin(); it != tone.end(); ++it) {
        if (it.key() != "models" && !it.key().startsWith("rigroom_")) storedTone.insert(it.key(), it.value());
    }
    QJsonArray storedModels;
    for (const QJsonValue& value : models) {
        QJsonObject model = value.toObject();
        model.remove("local_path");
        storedModels.append(model);
    }
    writeObject(cacheDir(format) + "/" + Tone3000::toneFolderName(tone) + "/tone.json",
                QJsonObject{{"tone", storedTone}, {"models", storedModels}});
    rescan();
}

QStringList CaptureLibrary::captureFilesIn(const QString& folder, Format format) {
    QStringList out;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (!info.fileName().startsWith("preview_") && extensions(format).contains(info.suffix().toLower())) {
            out << info.absoluteFilePath();
        }
    }
    out.sort();
    return out;
}

// ─── Scanning ────────────────────────────────────────────────────────────────

int CaptureLibrary::toneIdFromUrl(const QString& url) {
    // https://www.tone3000.com/tones/<slug>-<id>, or …/tones/<id>
    static const QRegularExpression pattern("/tones/(?:[^/?#]*-)?(\\d+)/?(?:[?#].*)?$");
    const QRegularExpressionMatch match = pattern.match(url);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QHash<QString, int> CaptureLibrary::presetLinks(const QString& presetsDir) {
    QHash<QString, int> links;
    std::function<void(const QJsonValue&)> visit = [&links, &visit](const QJsonValue& value) {
        if (value.isArray()) {
            for (const QJsonValue& v : value.toArray()) visit(v);
            return;
        }
        if (!value.isObject()) return;
        const QJsonObject object = value.toObject();
        const int id = toneIdFromUrl(object.value("model_source_url").toString());
        if (id != 0) {
            const QString path = object.value("model_file_path").toString();
            if (!path.isEmpty()) links.insert(path, id);
            for (const QJsonValue& variant : object.value("model_variants").toArray()) {
                const QString local = variant.toObject().value("local_path").toString();
                if (!local.isEmpty()) links.insert(local, id);
            }
        }
        for (const QJsonValue& child : object) visit(child);
    };
    QDirIterator it(presetsDir, {"*.json"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) visit(QJsonValue(readObject(it.next())));
    return links;
}

CaptureLibrary::ScanResult CaptureLibrary::scan(const ScanInput& input) {
    // What the cache knows: tone details from tone.json, and every download.
    QHash<int, QPair<QJsonObject, QJsonArray>> toneDetails;
    QHash<QString, int> variantFiles[2]; // file name -> tone id, per format
    QStringList cacheFiles;
    for (const Format format : {Format::Nam, Format::Ir}) {
        const QString modeDir = format == Format::Ir ? input.cacheDir + "/ir" : input.cacheDir;
        const QDir root(modeDir);
        for (const QFileInfo& folder : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QRegularExpressionMatch match = toneFolderPattern().match(folder.fileName());
            if (!match.hasMatch()) continue;
            const int id = match.captured(1).toInt();
            const QJsonObject meta = readObject(QDir(folder.absoluteFilePath()).filePath("tone.json"));
            if (!meta.isEmpty()) {
                toneDetails.insert(id, {meta.value("tone").toObject(), meta.value("models").toArray()});
                for (const QJsonValue& model : meta.value("models").toArray()) {
                    variantFiles[int(format)].insert(Tone3000::modelFileName(model.toObject(), format, false), id);
                }
            }
            for (const QFileInfo& info : QDir(folder.absoluteFilePath()).entryInfoList(QDir::Files)) {
                if (!info.fileName().startsWith("preview_") && extensions(format).contains(info.suffix().toLower())) {
                    cacheFiles << info.absoluteFilePath();
                }
            }
        }
        for (const QFileInfo& info : root.entryInfoList(QDir::Files)) {
            if (!info.fileName().startsWith("preview_") && extensions(format).contains(info.suffix().toLower())) {
                cacheFiles << info.absoluteFilePath();
            }
        }
    }

    auto describe = [&](const QString& path) {
        File f;
        f.path = path;
        formatOfPath(path, &f.format);
        const QFileInfo info(path);
        f.missing = !info.exists();
        f.inCache = path.startsWith(input.cacheDir + "/");
        f.modified = f.missing ? 0 : info.lastModified().toMSecsSinceEpoch();
        const QString fileName = info.fileName();

        // Which tone the file belongs to: its folder, a preset that used it,
        // or a known tone listing a variant saved under exactly this name.
        const QRegularExpressionMatch match = toneFolderPattern().match(info.dir().dirName());
        if (f.inCache && match.hasMatch()) f.toneId = match.captured(1).toInt();
        if (f.toneId == 0) f.toneId = input.presetLinks.value(path);
        if (f.toneId == 0) f.toneId = variantFiles[int(f.format)].value(fileName);
        if (f.toneId != 0 && toneDetails.contains(f.toneId)) {
            f.tone = toneDetails.value(f.toneId).first;
            const QString rootDir = f.format == Format::Ir ? input.cacheDir + "/ir" : input.cacheDir;
            const QString folder = rootDir + "/" + Tone3000::toneFolderName(f.tone);
            for (const QJsonValue& value : toneDetails.value(f.toneId).second) {
                QJsonObject model = value.toObject();
                const QString name = Tone3000::modelFileName(model, f.format, false);
                // Where this variant is on disk, if anywhere.
                QString local;
                if (name == fileName) local = path;
                else if (QFile::exists(folder + "/" + name)) local = folder + "/" + name;
                else if (QFile::exists(rootDir + "/" + name)) local = rootDir + "/" + name;
                if (!local.isEmpty()) model["local_path"] = local;
                if (name == fileName) f.variantName = model.value("name").toString();
                f.models.append(model);
            }
        }
        if (f.format == Format::Nam && !f.missing) f.nam = NamMetadata::read(path);
        f.originalName = !f.variantName.isEmpty() ? f.variantName
                       : (f.inCache || f.nam.name.isEmpty() ? prettyName(path) : f.nam.name);
        if (f.variantName.isEmpty()) {
            // Not listed by the tone (or no tone): the file stands alone.
            f.models.append(QJsonObject{{"name", f.originalName}, {"local_path", path}});
        }

        QStringList tags;
        auto add = [&tags](const QString& text) {
            const QString t = NamMetadata::vocabularyTag(text);
            if (!t.isEmpty() && !tags.contains(t)) tags << t;
        };
        const Tone3000::ToneItem tone = Tone3000::ToneItem::fromJson(f.tone);
        const QStringList guessed = f.format == Format::Nam ? NamMetadata::autoTags(f.nam, path)
                                                            : NamMetadata::tagsFromText(prettyName(path));
        for (const QString& t : guessed) add(t);
        // The tone's gear and tags describe the whole upload, which may mix
        // amp-only and full-rig captures; the file's own type wins.
        const bool fileHasType = std::any_of(tags.begin(), tags.end(), [](const QString& t) {
            return NamMetadata::tagCategory(t) == TagCategory::Type;
        });
        auto addFromTone = [&](const QString& text) {
            const QString t = NamMetadata::vocabularyTag(text);
            if (fileHasType && NamMetadata::tagCategory(t) == TagCategory::Type) return;
            add(text);
        };
        addFromTone(tone.gear);
        for (const QString& t : tone.tags) addFromTone(t);
        for (const QString& t : NamMetadata::tagsFromText(tone.title + " " + tone.makes.join(' '))) addFromTone(t);
        // Rooms and reverbs are for impulse responses; "Deluxe Reverb" is an amp.
        if (f.format == Format::Nam) tags.removeAll("space");
        if (f.format == Format::Ir && !tags.contains("space") && !tags.contains("pedal") && !tags.contains("outboard")
            && !tags.contains("cab")) {
            tags.prepend("cab"); // most impulse responses are cabinets
        }
        f.autoTags = tags.mid(0, kMaxAutoTags);
        return f;
    };

    ScanResult result;
    QSet<QString> inLibrary;
    for (const QString& path : input.paths) {
        inLibrary.insert(path);
        result.files << describe(path);
    }
    for (const QString& path : cacheFiles) {
        if (!inLibrary.contains(path)) result.candidates << describe(path);
    }
    return result;
}

void CaptureLibrary::rescan() {
    if (m_scanning) {
        m_rescanQueued = true;
        return;
    }
    m_scanning = true;
    ScanInput input{m_cacheDir, m_user.keys(), {}};
    auto* watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, watcher]() {
        const ScanResult result = watcher->result();
        watcher->deleteLater();
        m_files = result.files;
        m_candidates = result.candidates;
        m_tagCache.clear();
        m_index.clear();
        for (int i = 0; i < m_files.size(); ++i) m_index.insert(m_files[i].path, i);
        m_scanning = false;
        m_scanned = true;
        emit changed();
        if (m_rescanQueued) {
            m_rescanQueued = false;
            rescan();
        }
    });
    watcher->setFuture(QtConcurrent::run([input, presetsDir = m_presetsDir]() mutable {
        input.presetLinks = presetLinks(presetsDir);
        return scan(input);
    }));
}
