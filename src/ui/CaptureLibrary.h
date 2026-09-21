#pragma once
#include "NamMetadata.h"
#include "Tone3000Types.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <optional>

// The user's library of captures and impulse responses: files they added
// themselves (from TONE3000, earlier downloads, or their own folders). Each
// entry is one file. A file from TONE3000 knows its tone and creator, and
// takes the tone's picture and description from the tone.json kept next to
// the downloads. Users can name, tag, rate, describe and annotate files.
//
// Everything the user decides lives in ~/.config/RigRoom/capture-library.json.
class CaptureLibrary : public QObject {
    Q_OBJECT
public:
    // Gear details the user can set when the file's own are wrong or missing.
    struct Gear {
        QString type;   // vocabulary Type tag: amp, amp-cab, pedal…
        QString make;
        QString model;
        QString tone;   // vocabulary Tone tag: clean, crunch…
        bool isEmpty() const { return type.isEmpty() && make.isEmpty() && model.isEmpty() && tone.isEmpty(); }
    };
    // What the user decided about one file.
    struct UserData {
        QString name;           // shown instead of the file's own name
        QStringList tags;       // the user's own tags
        QStringList hiddenTags; // auto tags the user removed
        QString notes;
        int rating = 0;         // 0 = not rated, 1–5
        qint64 lastUsed = 0;
        qint64 addedAt = 0;
        Gear gear;
        QString folder;         // the library folder it is filed in
        QString pack;           // id of the user's pack it belongs to, if any
    };
    // A pack the user made to hold captures that belong together, like a
    // TONE3000 tone does for its variants.
    struct Pack {
        QString id;
        QString title;
        QString creator;
        QString description;
    };
    // What to give every file added at once.
    struct AddOptions {
        QString folder;         // empty = the folder used last
        QStringList tags;
        Gear gear;
        int rating = 0;
        QString pack;           // pack id; empty = none
    };
    static constexpr const char* kDefaultFolder = "Imported";
    // What the file itself says, and what is known about its tone.
    struct File {
        QString path;
        Tone3000::Format format = Tone3000::Format::Nam;
        bool missing = false;
        bool inCache = false;       // under the TONE3000 download cache
        int toneId = 0;
        QJsonObject tone;           // TONE3000 details, when known
        QJsonArray models;          // the tone's variants, when known
        QString variantName;        // this file's variant in the tone
        QString originalName;       // variant name, else metadata name, else file name
        NamMetadata::Info nam;
        QStringList autoTags;       // vocabulary tags from the file and its tone
        qint64 modified = 0;
    };

    static CaptureLibrary& instance();
    CaptureLibrary(const QString& configDir, const QString& cacheDir, const QString& presetsDir,
                   QObject* parent = nullptr);

    // Files in the library, as last scanned.
    QList<File> files(Tone3000::Format format) const;
    const File* file(const QString& path) const;
    bool contains(const QString& path) const { return m_user.contains(path); }
    bool isScanned() const { return m_scanned; }
    // True when any file of the tone is in the library.
    bool hasTone(int toneId, Tone3000::Format format) const;
    // Downloads in the cache that are not in the library, for importing.
    QList<File> importCandidates(Tone3000::Format format) const;

    UserData userData(const QString& path) const { return m_user.value(path); }
    QString displayName(const File& file) const;
    QString creator(const File& file) const;
    // Tags shown for a file: the user's, then the auto tags that the user did
    // not remove, with gear the user set replacing what was guessed.
    QStringList tags(const File& file) const;
    // Tags in use for a format, by category, with how many files carry them.
    QMap<NamMetadata::TagCategory, QList<QPair<QString, int>>> tagsByCategory(Tone3000::Format format) const;

    void add(const QStringList& paths);
    void add(const QStringList& paths, const AddOptions& options);

    // Library folders: the user's own way to file captures, one folder per
    // file. "Imported" always exists and takes files whose folder is removed.
    QStringList folders() const { return m_folders; }
    QString lastFolder() const { return m_lastFolder; }
    QString folderOf(const QString& path) const;
    void addFolder(const QString& name);
    void renameFolder(const QString& from, const QString& to);
    void removeFolder(const QString& name);
    void moveToFolder(const QStringList& paths, const QString& folder);

    QList<Pack> packs() const { return m_packs.values(); }
    Pack pack(const QString& id) const { return m_packs.value(id); }
    QString packOf(const QString& path) const { return m_user.value(path).pack; }
    // Creates a pack and returns its id.
    QString createPack(const QString& title, const QString& creator = {});
    void updatePack(const Pack& pack);
    // Puts files in a pack; an empty id takes them out. Empty packs are removed.
    void setPack(const QStringList& paths, const QString& id);

    // Changes to several files at once. Gear fields that are null (QString())
    // are left alone; an empty string clears the field.
    struct Changes {
        QString folder;
        std::optional<QString> pack;
        std::optional<int> rating;
        std::optional<QString> type, make, model, tone;
        QStringList addTags;
        QStringList removeTags;
        std::optional<QString> name, notes;
        bool isEmpty() const {
            return folder.isEmpty() && !pack && !rating && !type && !make && !model && !tone && addTags.isEmpty()
                && removeTags.isEmpty() && !name && !notes;
        }
    };
    void apply(const QStringList& paths, const Changes& changes);
    // Takes files out of the library; with deleteFiles, downloaded files are
    // deleted too (files outside the download cache never are).
    void remove(const QStringList& paths, bool deleteFiles);
    void setName(const QString& path, const QString& name);
    void setNotes(const QString& path, const QString& notes);
    void setRating(const QStringList& paths, int rating);
    void setGear(const QString& path, const Gear& gear);
    void addTag(const QStringList& paths, const QString& tag);
    void removeTag(const QString& path, const QString& tag);
    void markUsed(const QString& path);

    // Writes tone.json next to a fresh download (it does not add the file).
    void recordDownload(const QJsonObject& tone, const QJsonArray& models, Tone3000::Format format);
    QString cacheDir(Tone3000::Format format) const {
        return format == Tone3000::Format::Ir ? m_cacheDir + "/ir" : m_cacheDir;
    }
    static QStringList extensions(Tone3000::Format format);
    // Every capture file under a folder, for adding a whole folder.
    static QStringList captureFilesIn(const QString& folder, Tone3000::Format format);

    // Scans in the background; changed() follows.
    void rescan();
    // Fetches missing TONE3000 details (images, descriptions) for library
    // files whose tone is known, and writes them to tone.json.
    void enrich();

    // The scan, run on a worker thread: describes the given files, and every
    // download in the cache that is not among them.
    struct ScanInput {
        QString cacheDir;
        QStringList paths;               // files in the library
        QHash<QString, int> presetLinks; // file path -> tone id, from presets
    };
    struct ScanResult {
        QList<File> files;
        QList<File> candidates;
    };
    static ScanResult scan(const ScanInput& input);
    static QHash<QString, int> presetLinks(const QString& presetsDir);
    static int toneIdFromUrl(const QString& url);

signals:
    void changed();

private:
    void load();
    void save() const;
    void changedData();
    void enrichNext();

    QString m_configDir;
    QString m_cacheDir;
    QString m_presetsDir;
    QList<File> m_files;
    QList<File> m_candidates;
    QHash<QString, int> m_index; // path -> position in m_files
    QHash<QString, UserData> m_user;
    QStringList m_folders{QString(kDefaultFolder)};
    QMap<QString, Pack> m_packs;
    QString m_lastFolder = kDefaultFolder;
    mutable QHash<int, QMap<NamMetadata::TagCategory, QList<QPair<QString, int>>>> m_tagCache; // by format
    QSet<int> m_enrichAttempted;
    QList<int> m_enrichQueue;
    bool m_scanning = false;
    bool m_rescanQueued = false;
    bool m_scanned = false;
    int m_enriching = 0;
};
