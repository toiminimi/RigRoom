#include "ui/CaptureLibrary.h"
#include "ui/NamMetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <cassert>
#include <functional>
#include <iostream>

using Tone3000::Format;
using NamMetadata::TagCategory;

namespace {

void writeBytes(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    file.write(data);
}

void writeJsonFile(const QString& path, const QJsonDocument& document) {
    writeBytes(path, document.toJson());
}

QString absPath(const QString& path) {
    return QFileInfo(path).absoluteFilePath();
}

bool hasTag(const QStringList& tags, const QString& tag) {
    return tags.contains(tag);
}

bool isVocabularyTag(const QString& tag) {
    return NamMetadata::tagCategory(tag) != TagCategory::Other;
}

CaptureLibrary::File findByPath(const QList<CaptureLibrary::File>& files, const QString& path) {
    for (const CaptureLibrary::File& file : files) {
        if (file.path == path) return file;
    }
    return {};
}

CaptureLibrary::File findByName(const QList<CaptureLibrary::File>& files, const QString& name) {
    for (const CaptureLibrary::File& file : files) {
        if (QFileInfo(file.path).fileName() == name) return file;
    }
    return {};
}

int categoryCount(const QMap<TagCategory, QList<QPair<QString, int>>>& counts, TagCategory category,
                  const QString& tag) {
    for (const auto& pair : counts.value(category)) {
        if (pair.first == tag) return pair.second;
    }
    return 0;
}

bool waitForChanged(CaptureLibrary& library, const std::function<void()>& action) {
    QEventLoop loop;
    bool got = false;
    QObject::connect(&library, &CaptureLibrary::changed, &loop, [&] {
        got = true;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    action();
    loop.exec();
    return got;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── NamMetadata::parseParts ──────────────────────────────────────────────
    {
        const QByteArray small = R"({
            "metadata": {
                "name": "Deluxe Reverb",
                "modeled_by": "Alice",
                "gear_type": "amp_cab",
                "gear_make": "Fender",
                "tone_type": "clean",
                "loudness": -12.5,
                "input_level_dbu": -18.0
            },
            "architecture": "SlimmableContainer"
        })";
        const NamMetadata::Info whole = NamMetadata::parseParts(small);
        assert(whole.valid);
        assert(whole.name == "Deluxe Reverb");
        assert(whole.modeledBy == "Alice");
        assert(whole.gearType == "amp_cab");
        assert(whole.gearMake == "Fender");
        assert(whole.toneType == "clean");
        assert(whole.loudness == -12.5);
        assert(whole.calibrated);
        assert(whole.architecture == "SlimmableContainer");
        assert(NamMetadata::architectureLabel(whole) == "A2");
    }

    {
        const QByteArray nested = R"({
            "architecture": "SlimmableContainer",
            "config": {
                "models": [
                    {
                        "metadata": {
                            "name": "NESTED_SHOULD_NOT_WIN",
                            "gear_type": "pedal",
                            "modeled_by": "Nested"
                        },
                        "architecture": "WaveNet",
                        "config": { "layers": [ { "channels": 12 } ] }
                    }
                ]
            },
            "weights": [1, 2, 3, 4, 5, 6, 7, 8],
            "metadata": {
                "name": "Top Level Amp",
                "modeled_by": "Alice",
                "gear_type": "amp_cab"
            }
        })";
        const int cut = nested.indexOf("\"weights\"");
        assert(cut > 0);
        const QByteArray head = nested.left(cut + 12);
        const QByteArray tail = nested.mid(cut);
        assert(QJsonDocument::fromJson(head).isNull());
        const NamMetadata::Info info = NamMetadata::parseParts(head, tail);
        assert(info.valid);
        assert(info.architecture == "SlimmableContainer");
        assert(info.name == "Top Level Amp");
        assert(info.modeledBy == "Alice");
        assert(info.gearType == "amp_cab");
        assert(info.name != "NESTED_SHOULD_NOT_WIN");
        assert(info.gearType != "pedal");
        assert(NamMetadata::architectureLabel(info) == "A2");
    }

    {
        const QByteArray wavenet = R"({
            "metadata": { "name": "Lite Amp" },
            "architecture": "WaveNet",
            "config": { "layers": [ { "channels": 12 } ] }
        })";
        const NamMetadata::Info info = NamMetadata::parseParts(wavenet);
        assert(info.valid);
        assert(info.architecture == "WaveNet");
        assert(info.size == "lite");
        assert(NamMetadata::architectureLabel(info) == "A1");
    }

    {
        const NamMetadata::Info garbage = NamMetadata::parseParts("not json {{{ garbage");
        assert(!garbage.valid);
    }

    // ── NamMetadata::read ────────────────────────────────────────────────────
    {
        QTemporaryDir dir;
        assert(dir.isValid());

        const QString smallPath = dir.path() + "/small.nam";
        writeBytes(smallPath, R"({
            "metadata": {
                "name": "From File",
                "modeled_by": "Bob",
                "gear_type": "amp",
                "input_level_dbu": 12.0
            },
            "architecture": "WaveNet",
            "config": { "layers": [ { "channels": 8 } ] }
        })");
        const NamMetadata::Info small = NamMetadata::read(smallPath);
        assert(small.valid);
        assert(small.name == "From File");
        assert(small.modeledBy == "Bob");
        assert(small.gearType == "amp");
        assert(small.calibrated);
        assert(small.architecture == "WaveNet");
        assert(small.size == "feather");

        QByteArray large = "{\"architecture\":\"SlimmableContainer\",\"config\":{},\"weights\":[";
        for (int i = 0; i < 20000; ++i) large += "0.1,";
        large += "0],\"metadata\":{\"name\":\"TailMeta\",\"modeled_by\":\"Carol\",\"gear_type\":\"amp_cab\","
                 "\"input_level_dbu\":-6.5}}";
        const QString largePath = dir.path() + "/large.nam";
        writeBytes(largePath, large);
        assert(QFileInfo(largePath).size() > 64 * 1024);
        const NamMetadata::Info tailInfo = NamMetadata::read(largePath);
        assert(tailInfo.valid);
        assert(tailInfo.name == "TailMeta");
        assert(tailInfo.modeledBy == "Carol");
        assert(tailInfo.gearType == "amp_cab");
        assert(tailInfo.architecture == "SlimmableContainer");
        assert(tailInfo.calibrated);
        assert(NamMetadata::architectureLabel(tailInfo) == "A2");
    }

    // ── normalizeTag ─────────────────────────────────────────────────────────
    {
        assert(NamMetadata::normalizeTag("Hi Gain") == "high-gain");
        assert(NamMetadata::normalizeTag("hi_gain") == "high-gain");
        assert(NamMetadata::normalizeTag("OD") == "overdrive");
        assert(NamMetadata::normalizeTag("T3K-Null") == "");
        assert(NamMetadata::normalizeTag("t3k-unset") == "");
        assert(NamMetadata::normalizeTag("Full-Rig") == "amp-cab");
        assert(NamMetadata::normalizeTag("  Clean  ") == "clean");
        assert(NamMetadata::normalizeTag("nam") == "");
        assert(NamMetadata::normalizeTag("cleanboost") == "boost");
        assert(NamMetadata::normalizeTag("clean-boost") == "boost");
        assert(NamMetadata::normalizeTag("distortion") == "overdrive");
        // "Reverb" names amps (Deluxe Reverb, Twin Reverb), so it is not a room.
        assert(NamMetadata::normalizeTag("reverb") == "reverb");
        assert(NamMetadata::vocabularyTag("Reverb").isEmpty());
        assert(NamMetadata::normalizeTag("room") == "space");
    }

    // ── normalizedGear ───────────────────────────────────────────────────────
    {
        assert(NamMetadata::normalizedGear("amp_cab") == "amp-cab");
        assert(NamMetadata::normalizedGear("full-rig") == "amp-cab");
        assert(NamMetadata::normalizedGear("amp") == "amp");
        assert(NamMetadata::normalizedGear("pedal") == "pedal");
        assert(NamMetadata::normalizedGear("") == "");
    }

    // ── tagsFromText ─────────────────────────────────────────────────────────
    {
        const QStringList marshall = NamMetadata::tagsFromText("Marshall_JCM800_Crunch");
        assert(hasTag(marshall, "marshall"));
        assert(hasTag(marshall, "crunch"));

        const QStringList mesa = NamMetadata::tagsFromText("Mesa/Boogie Lead");
        assert(hasTag(mesa, "mesa"));
        assert(hasTag(mesa, "lead"));

        const QStringList voxel = NamMetadata::tagsFromText("voxel test");
        assert(!hasTag(voxel, "vox"));

        const QStringList hughes = NamMetadata::tagsFromText("Hughes & Kettner");
        assert(hasTag(hughes, "hughes-kettner"));
    }

    // ── autoTags ─────────────────────────────────────────────────────────────
    {
        NamMetadata::Info info;
        info.valid = true;
        info.name = "Marshall JCM";
        info.gearType = "amp_cab";
        info.gearMake = "Marshall";
        info.toneType = "Clean";
        info.architecture = "SlimmableContainer";
        info.size = "lite";
        info.calibrated = true;
        const QStringList tags = NamMetadata::autoTags(info, "Some_Crunch.nam");
        assert(hasTag(tags, "amp-cab"));
        assert(hasTag(tags, "clean"));
        assert(hasTag(tags, "a2"));
        assert(hasTag(tags, "lite"));
        assert(hasTag(tags, "calibrated"));
        assert(hasTag(tags, "marshall"));
        assert(hasTag(tags, "crunch"));
        assert(tags.size() == QSet<QString>(tags.begin(), tags.end()).size());
    }

    // ── vocabulary ───────────────────────────────────────────────────────────
    {
        assert(NamMetadata::tagCategory("amp-cab") == TagCategory::Type);
        assert(NamMetadata::tagCategory("clean") == TagCategory::Tone);
        assert(NamMetadata::tagCategory("fender") == TagCategory::Brand);
        assert(NamMetadata::tagCategory("a2") == TagCategory::Tech);
        assert(NamMetadata::tagCategory("my-own") == TagCategory::Other);

        assert(NamMetadata::vocabularyTag("Hi Gain") == "high-gain");
        assert(NamMetadata::vocabularyTag("Full Rig") == "amp-cab");
        assert(NamMetadata::vocabularyTag("Mesa/Boogie") == "mesa");
        assert(NamMetadata::vocabularyTag("1970s") == "");
        assert(NamMetadata::vocabularyTag("fenderish") == "");
        assert(NamMetadata::tagLabel("amp-cab") == "Amp + Cab");

        NamMetadata::Info info;
        info.valid = true;
        info.toneType = "twang-fender-style";
        for (const QString& tag : NamMetadata::autoTags(info, "file.nam")) {
            if (!isVocabularyTag(tag)) {
                std::cout << "SOURCE DISCREPANCY: autoTags returned non-vocabulary tag "
                          << tag.toStdString() << std::endl;
            }
            assert(isVocabularyTag(tag));
        }
        assert(!hasTag(NamMetadata::autoTags(info, "file.nam"), "twang-fender-style"));

        info.toneType = "T3K-Null";
        for (const QString& tag : NamMetadata::autoTags(info, "file.nam")) {
            if (!isVocabularyTag(tag)) {
                std::cout << "SOURCE DISCREPANCY: autoTags returned non-vocabulary tag "
                          << tag.toStdString() << std::endl;
            }
            assert(isVocabularyTag(tag));
        }
        assert(!hasTag(NamMetadata::autoTags(info, "file.nam"), "t3k-null"));
        assert(!hasTag(NamMetadata::autoTags(info, "file.nam"), "T3K-Null"));
    }

    // ── CaptureLibrary::toneIdFromUrl ────────────────────────────────────────
    {
        assert(CaptureLibrary::toneIdFromUrl("https://www.tone3000.com/tones/dual-rectifier-sm7b-32438") == 32438);
        assert(CaptureLibrary::toneIdFromUrl("https://www.tone3000.com/tones/32438") == 32438);
        assert(CaptureLibrary::toneIdFromUrl("https://www.tone3000.com/tones/foo-12/?x=1") == 12);
        assert(CaptureLibrary::toneIdFromUrl("https://example.com") == 0);
    }

    // ── CaptureLibrary::presetLinks ──────────────────────────────────────────
    {
        QTemporaryDir presets;
        assert(presets.isValid());
        writeJsonFile(presets.path() + "/nested/pack.json",
                      QJsonDocument(QJsonObject{
                          {"nodes",
                           QJsonArray{
                               QJsonObject{
                                   {"model_file_path", "/abs/a.nam"},
                                   {"model_source_url", "https://www.tone3000.com/tones/dual-rectifier-sm7b-99"},
                                   {"model_variants",
                                    QJsonArray{
                                        QJsonObject{{"local_path", "/abs/b.nam"}},
                                        QJsonObject{{"local_path", "/abs/c.nam"}},
                                    }},
                               },
                               QJsonObject{
                                   {"model_file_path", "/abs/ignored.nam"},
                                   {"model_variants", QJsonArray{QJsonObject{{"local_path", "/abs/also-ignored.nam"}}}},
                               },
                           }},
                      }));

        const QHash<QString, int> links = CaptureLibrary::presetLinks(presets.path());
        assert(links.value("/abs/a.nam") == 99);
        assert(links.value("/abs/b.nam") == 99);
        assert(links.value("/abs/c.nam") == 99);
        assert(!links.contains("/abs/ignored.nam"));
        assert(!links.contains("/abs/also-ignored.nam"));
    }

    // ── CaptureLibrary::extensions ───────────────────────────────────────────
    {
        assert(CaptureLibrary::extensions(Format::Nam).contains("nam"));
        assert(CaptureLibrary::extensions(Format::Ir).contains("wav"));
        assert(CaptureLibrary::extensions(Format::Ir).contains("flac"));
        assert(CaptureLibrary::extensions(Format::Ir).contains("aif"));
        assert(CaptureLibrary::extensions(Format::Ir).contains("aiff"));
    }

    // ── CaptureLibrary::scan ─────────────────────────────────────────────────
    {
        QTemporaryDir cache;
        assert(cache.isValid());
        const QString root = cache.path();

        const QString tone42 = root + "/tone_42_slug";
        writeJsonFile(tone42 + "/tone.json",
                      QJsonDocument(QJsonObject{
                          {"tone",
                           QJsonObject{
                               {"id", 42},
                               {"title", "T"},
                               {"user", QJsonObject{{"username", "u"}}},
                               {"gear", "amp-cab"},
                               {"tags",
                                QJsonArray{
                                    QJsonObject{{"name", "Clean"}},
                                    QJsonObject{{"name", "1970s"}},
                                }},
                           }},
                          {"models",
                           QJsonArray{
                               QJsonObject{{"name", "Var A"}, {"model_url", "https://x/a.nam"}},
                               QJsonObject{{"name", "Var B"}},
                           }},
                      }));
        const QString varA = absPath(tone42 + "/Var_A.nam");
        writeBytes(varA, "{\"metadata\":{\"name\":\"Var A\"}}");
        const QString varB = absPath(root + "/Var_B.nam");
        // An amp-only capture inside a tone whose upload says amp-cab.
        writeBytes(varB, "{\"metadata\":{\"name\":\"Var B\",\"gear_type\":\"amp\"}}");
        const QString loose = absPath(root + "/Loose_File.nam");
        writeBytes(loose, "{\"metadata\":{\"name\":\"Loose Meta\"}}");
        writeBytes(root + "/preview_x.nam", "preview-should-be-ignored");
        writeBytes(root + "/ir/cab.wav", "RIFF-fake-wav");
        const QString cab = absPath(root + "/ir/cab.wav");

        CaptureLibrary::ScanInput input;
        input.cacheDir = root;
        input.paths = QStringList{varA};

        const CaptureLibrary::ScanResult result = CaptureLibrary::scan(input);
        assert(result.files.size() == 1);
        const CaptureLibrary::File scanned = result.files.first();
        assert(scanned.path == varA);
        assert(scanned.toneId == 42);
        assert(scanned.variantName == "Var A");
        assert(scanned.originalName == "Var A");
        assert(hasTag(scanned.autoTags, "amp-cab"));
        assert(hasTag(scanned.autoTags, "clean"));
        assert(!hasTag(scanned.autoTags, "1970s"));
        for (const CaptureLibrary::File& candidate : result.candidates) {
            if (candidate.path != varB) continue;
            // The file's own type wins over the tone's.
            assert(hasTag(candidate.autoTags, "amp"));
            assert(!hasTag(candidate.autoTags, "amp-cab"));
            assert(hasTag(candidate.autoTags, "clean")); // tone tags still apply
        }

        bool varALocal = false;
        bool varBLocal = false;
        for (const QJsonValue& value : scanned.models) {
            const QJsonObject model = value.toObject();
            const QString name = model.value("name").toString();
            const QString local = model.value("local_path").toString();
            if (name == "Var A") {
                varALocal = local == varA;
                assert(local == varA);
            }
            if (name == "Var B") {
                varBLocal = local == varB;
                assert(local == varB);
            }
        }
        assert(varALocal);
        assert(varBLocal);

        const CaptureLibrary::File candVarB = findByPath(result.candidates, varB);
        if (candVarB.path.isEmpty()) {
            std::cout << "SOURCE DISCREPANCY: scan candidates missing Var_B.nam" << std::endl;
        }
        assert(candVarB.path == varB);
        assert(candVarB.toneId == 42);

        const CaptureLibrary::File candLoose = findByPath(result.candidates, loose);
        assert(candLoose.path == loose);
        assert(candLoose.toneId == 0);
        assert(candLoose.originalName == "Loose File");

        const CaptureLibrary::File candCab = findByPath(result.candidates, cab);
        assert(candCab.path == cab);
        assert(candCab.format == Format::Ir);
        assert(hasTag(candCab.autoTags, "cab"));

        assert(findByName(result.candidates, "preview_x.nam").path.isEmpty());
        assert(findByPath(result.candidates, varA).path.isEmpty());

        {
            QTemporaryDir config;
            QTemporaryDir presets;
            CaptureLibrary names(config.path(), root, presets.path());
            assert(names.displayName(scanned) == "Var A");
            assert(names.creator(scanned) == "u");
        }

        const QString missing = absPath(root + "/does_not_exist.nam");
        CaptureLibrary::ScanInput missingInput;
        missingInput.cacheDir = root;
        missingInput.paths = QStringList{missing};
        const CaptureLibrary::ScanResult missingResult = CaptureLibrary::scan(missingInput);
        assert(missingResult.files.size() == 1);
        assert(missingResult.files.first().missing);

        const QString linked = absPath(root + "/Linked.nam");
        writeBytes(linked, "{\"metadata\":{\"name\":\"Linked\"}}");
        CaptureLibrary::ScanInput linkedInput;
        linkedInput.cacheDir = root;
        linkedInput.paths = QStringList{linked};
        linkedInput.presetLinks.insert(linked, 42);
        const CaptureLibrary::ScanResult linkedResult = CaptureLibrary::scan(linkedInput);
        assert(linkedResult.files.size() == 1);
        assert(linkedResult.files.first().toneId == 42);
    }

    // ── CaptureLibrary persistence ───────────────────────────────────────────
    {
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        assert(config.isValid() && cache.isValid() && presets.isValid());

        const QString path = absPath(cache.path() + "/kept.nam");
        writeBytes(path, R"({"metadata":{"name":"Kept","tone_type":"clean","gear_type":"amp"}})");

        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            library.add({path});
            assert(library.contains(path));
        }

        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            assert(library.contains(path));
        }
    }

    {
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        assert(config.isValid() && cache.isValid() && presets.isValid());

        const QString path = absPath(cache.path() + "/roundtrip.nam");
        writeBytes(path, R"({
            "metadata": {"name": "Round", "tone_type": "clean", "gear_type": "amp_cab", "gear_make": "Fender"},
            "architecture": "SlimmableContainer"
        })");

        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            assert(waitForChanged(library, [&] { library.add({path}); }));
            assert(library.contains(path));

            library.setName(path, "Custom Name");
            library.setNotes(path, "Some notes");
            library.setRating({path}, 9);
            assert(library.userData(path).rating == 5);
            library.setGear(path, CaptureLibrary::Gear{"amp", "Fender", "Twin", "clean"});
            library.addTag({path}, "Hi Gain");
            library.addTag({path}, "my-own");
            library.removeTag(path, "a2");
            library.markUsed(path);

            const CaptureLibrary::UserData data = library.userData(path);
            assert(data.name == "Custom Name");
            assert(data.notes == "Some notes");
            assert(data.rating == 5);
            assert(data.gear.type == "amp");
            assert(data.gear.make == "Fender");
            assert(data.gear.model == "Twin");
            assert(data.gear.tone == "clean");
            assert(data.tags.contains("high-gain"));
            assert(data.tags.contains("my-own"));
            assert(data.hiddenTags.contains("a2"));
            assert(data.lastUsed > 0);
            assert(data.addedAt > 0);
        }

        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            assert(library.contains(path));
            const CaptureLibrary::UserData data = library.userData(path);
            assert(data.name == "Custom Name");
            assert(data.notes == "Some notes");
            assert(data.rating == 5);
            assert(data.gear.type == "amp");
            assert(data.gear.make == "Fender");
            assert(data.gear.model == "Twin");
            assert(data.gear.tone == "clean");
            assert(data.tags.contains("high-gain"));
            assert(data.tags.contains("my-own"));
            assert(data.hiddenTags.contains("a2"));
            assert(data.lastUsed > 0);
            assert(data.addedAt > 0);
        }
    }

    {
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        assert(config.isValid());
        writeJsonFile(config.path() + "/capture-library.json",
                      QJsonDocument(QJsonObject{
                          {"version", 1},
                          {"entries",
                           QJsonObject{
                               {"/tmp/x.nam",
                                QJsonObject{
                                    {"name", "Old"},
                                    {"tags", QJsonArray{"clean"}},
                                }},
                           }},
                      }));
        CaptureLibrary library(config.path(), cache.path(), presets.path());
        assert(!library.contains("/tmp/x.nam"));
        assert(library.userData("/tmp/x.nam").name.isEmpty());
        assert(library.userData("/tmp/x.nam").tags.isEmpty());
    }

    {
        // Library favorites became ratings: an unrated favorite gets five stars,
        // a rated one keeps its rating, and the flag is not written back.
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        writeJsonFile(config.path() + "/capture-library.json",
                      QJsonDocument(QJsonObject{
                          {"version", 2},
                          {"entries",
                           QJsonObject{
                               {"/tmp/fav.nam", QJsonObject{{"favorite", true}}},
                               {"/tmp/rated.nam", QJsonObject{{"favorite", true}, {"rating", 3}}},
                               {"/tmp/plain.nam", QJsonObject{{"name", "Plain"}}},
                           }},
                      }));
        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            assert(library.userData("/tmp/fav.nam").rating == 5);
            assert(library.userData("/tmp/rated.nam").rating == 3);
            assert(library.userData("/tmp/plain.nam").rating == 0);
            library.setName("/tmp/plain.nam", "Plain 2");
        }
        QFile file(config.path() + "/capture-library.json");
        assert(file.open(QIODevice::ReadOnly));
        assert(!file.readAll().contains("\"favorite\""));
    }

    {
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        CaptureLibrary library(config.path(), cache.path(), presets.path());
        const QString ghost = absPath(cache.path() + "/ghost.nam");
        library.setName(ghost, "Nope");
        library.setNotes(ghost, "Nope");
        library.setRating({ghost}, 4);
        library.setGear(ghost, CaptureLibrary::Gear{"amp", "Fender", "Twin", "clean"});
        library.addTag({ghost}, "clean");
        library.removeTag(ghost, "clean");
        library.markUsed(ghost);
        assert(!library.contains(ghost));
        const CaptureLibrary::UserData data = library.userData(ghost);
        assert(data.name.isEmpty());
        assert(data.notes.isEmpty());
        assert(data.rating == 0);
        assert(data.gear.isEmpty());
        assert(data.tags.isEmpty());
        assert(data.lastUsed == 0);
        assert(data.addedAt == 0);
    }

    {
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        QTemporaryDir outside;
        assert(config.isValid() && cache.isValid() && outside.isValid());

        const QString cached = absPath(cache.path() + "/inside.nam");
        const QString cachedKeep = absPath(cache.path() + "/keep.nam");
        const QString external = absPath(outside.path() + "/outside.nam");
        writeBytes(cached, "{\"metadata\":{\"name\":\"Inside\"}}");
        writeBytes(cachedKeep, "{\"metadata\":{\"name\":\"Keep\"}}");
        writeBytes(external, "{\"metadata\":{\"name\":\"Outside\"}}");

        CaptureLibrary library(config.path(), cache.path(), presets.path());
        assert(waitForChanged(library, [&] { library.add({cached, cachedKeep, external}); }));
        assert(library.contains(cached));
        assert(library.contains(cachedKeep));
        assert(library.contains(external));

        assert(waitForChanged(library, [&] { library.remove({cached}, true); }));
        assert(!library.contains(cached));
        assert(!QFile::exists(cached));

        assert(waitForChanged(library, [&] { library.remove({external}, true); }));
        assert(!library.contains(external));
        assert(QFile::exists(external));

        assert(waitForChanged(library, [&] { library.remove({cachedKeep}, false); }));
        assert(!library.contains(cachedKeep));
        assert(QFile::exists(cachedKeep));
    }

    // ── tags() and tagsByCategory with gear overrides ────────────────────────
    {
        QTemporaryDir config;
        QTemporaryDir cache;
        QTemporaryDir presets;
        const QString path = absPath(cache.path() + "/Gear_File.nam");
        writeBytes(path, R"({
            "metadata": {
                "name": "Gear File",
                "gear_type": "amp_cab",
                "gear_make": "Fender",
                "tone_type": "clean"
            },
            "architecture": "SlimmableContainer"
        })");

        CaptureLibrary library(config.path(), cache.path(), presets.path());
        assert(waitForChanged(library, [&] { library.add({path}); }));
        const CaptureLibrary::File* scanned = library.file(path);
        assert(scanned);
        assert(hasTag(scanned->autoTags, "amp-cab"));
        assert(hasTag(scanned->autoTags, "clean"));
        assert(hasTag(scanned->autoTags, "fender"));

        library.addTag({path}, "my-own");
        library.setGear(path, CaptureLibrary::Gear{"pedal", "Marshall", "JCM", "crunch"});
        library.removeTag(path, "a2");

        const CaptureLibrary::File* updated = library.file(path);
        assert(updated);
        const QStringList tags = library.tags(*updated);
        assert(!tags.isEmpty());
        assert(tags.first() == "my-own");
        assert(hasTag(tags, "pedal"));
        assert(!hasTag(tags, "amp-cab"));
        assert(hasTag(tags, "marshall"));
        assert(!hasTag(tags, "fender"));
        assert(hasTag(tags, "crunch"));
        assert(!hasTag(tags, "clean"));
        assert(!hasTag(tags, "a2"));
        assert(library.userData(path).hiddenTags.contains("a2"));

        const auto byCategory = library.tagsByCategory(Format::Nam);
        assert(categoryCount(byCategory, TagCategory::Type, "pedal") == 1);
        assert(categoryCount(byCategory, TagCategory::Type, "amp-cab") == 0);
        assert(categoryCount(byCategory, TagCategory::Brand, "marshall") == 1);
        assert(categoryCount(byCategory, TagCategory::Brand, "fender") == 0);
        assert(categoryCount(byCategory, TagCategory::Tone, "crunch") == 1);
        assert(categoryCount(byCategory, TagCategory::Tone, "clean") == 0);
        assert(categoryCount(byCategory, TagCategory::Other, "my-own") == 1);
        assert(categoryCount(byCategory, TagCategory::Tech, "a2") == 0);
    }

    // ── captureFilesIn ───────────────────────────────────────────────────────
    {
        QTemporaryDir dir;
        assert(dir.isValid());
        const QString nested = dir.path() + "/deep/Nested_Capture.nam";
        writeBytes(nested, "{\"metadata\":{\"name\":\"Nested\"}}");
        writeBytes(dir.path() + "/Root.nam", "{\"metadata\":{\"name\":\"Root\"}}");
        writeBytes(dir.path() + "/preview_skip.nam", "preview");
        writeBytes(dir.path() + "/deep/preview_also.nam", "preview");
        writeBytes(dir.path() + "/notes.txt", "not a capture");

        const QStringList found = CaptureLibrary::captureFilesIn(dir.path(), Format::Nam);
        assert(found.contains(absPath(nested)));
        assert(found.contains(absPath(dir.path() + "/Root.nam")));
        for (const QString& path : found) {
            assert(!QFileInfo(path).fileName().startsWith("preview_"));
        }
        assert(found.size() == 2);
    }

    // ── Library folders ──────────────────────────────────────────────────────
    {
        QTemporaryDir config, cache, presets, files;
        assert(config.isValid() && cache.isValid() && presets.isValid() && files.isValid());
        const QString a = QDir(files.path()).filePath("a.nam");
        const QString b = QDir(files.path()).filePath("b.nam");
        for (const QString& path : {a, b}) {
            QFile f(path);
            assert(f.open(QIODevice::WriteOnly));
            f.write("{}");
        }
        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            assert(library.folders() == QStringList{CaptureLibrary::kDefaultFolder});
            CaptureLibrary::AddOptions options;
            options.folder = "Live";
            options.tags = {"Songs 2026"};
            options.rating = 4;
            options.gear.type = "amp";
            library.add({a}, options);
            library.add({b}); // the folder used last
            assert(library.folderOf(a) == "Live" && library.folderOf(b) == "Live");
            assert(library.userData(a).tags == QStringList{"songs-2026"});
            assert(library.userData(a).rating == 4 && library.userData(a).gear.type == "amp");
            library.addFolder("Metal");
            library.moveToFolder({b}, "Metal");
            library.renameFolder("Live", "Gigs");
            assert(library.folderOf(a) == "Gigs" && library.folderOf(b) == "Metal");
            library.renameFolder(CaptureLibrary::kDefaultFolder, "Nope"); // Imported stays
            assert(library.folders().contains(CaptureLibrary::kDefaultFolder));
        }
        {
            CaptureLibrary reloaded(config.path(), cache.path(), presets.path());
            assert(reloaded.folders().contains("Gigs") && reloaded.folders().contains("Metal"));
            assert(reloaded.folderOf(a) == "Gigs" && reloaded.lastFolder() == "Gigs");
            reloaded.removeFolder("Metal"); // its captures go back to Imported
            assert(reloaded.folderOf(b) == CaptureLibrary::kDefaultFolder);
            assert(!reloaded.folders().contains("Metal"));
        }
    }

    // ── Packs and changes to several files ───────────────────────────────────
    {
        QTemporaryDir config, cache, presets, files;
        assert(config.isValid() && cache.isValid() && presets.isValid() && files.isValid());
        QStringList paths;
        for (const char* name : {"x.nam", "y.nam", "z.nam"}) {
            const QString path = QDir(files.path()).filePath(name);
            QFile f(path);
            assert(f.open(QIODevice::WriteOnly));
            f.write("{}");
            paths << path;
        }
        QString packId;
        {
            CaptureLibrary library(config.path(), cache.path(), presets.path());
            library.add(paths);
            packId = library.createPack("My 5150", "me");
            library.setPack({paths[0], paths[1]}, packId);
            assert(library.packOf(paths[0]) == packId && library.packOf(paths[2]).isEmpty());

            CaptureLibrary::Changes changes;
            changes.rating = 3;
            changes.type = QString("amp");
            changes.make = QString("Peavey");
            changes.addTags = {"Live Set"};
            changes.folder = "Gigs";
            library.apply(paths, changes);
            for (const QString& path : paths) {
                const CaptureLibrary::UserData d = library.userData(path);
                assert(d.rating == 3 && d.gear.type == "amp" && d.gear.make == "Peavey");
                assert(d.gear.model.isEmpty()); // not touched
                assert(d.tags.contains("live-set") && d.folder == "Gigs");
            }
            CaptureLibrary::Changes remove;
            remove.removeTags = {"live-set"};
            remove.pack = QString(); // out of the pack
            library.apply({paths[0], paths[1]}, remove);
            assert(!library.userData(paths[0]).tags.contains("live-set"));
            assert(library.userData(paths[2]).tags.contains("live-set"));
            assert(library.packs().isEmpty()); // nothing left in it, so it is gone
            packId = library.createPack("Kept", "");
            library.setPack({paths[2]}, packId);
        }
        CaptureLibrary reloaded(config.path(), cache.path(), presets.path());
        assert(reloaded.pack(packId).title == "Kept" && reloaded.packOf(paths[2]) == packId);
    }

    std::cout << "CaptureLibrary tests passed" << std::endl;
    return 0;
}
