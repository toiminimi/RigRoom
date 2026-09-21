#include "ui/Tone3000Library.h"
#include "ui/Tone3000Types.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <cassert>
#include <iostream>

using Tone3000::Format;
using Tone3000::Query;
using Tone3000::ToneItem;

namespace {

QMap<QString, QString> queryMap(const QUrlQuery& query) {
    QMap<QString, QString> out;
    for (const auto& item : query.queryItems(QUrl::FullyDecoded)) out.insert(item.first, item.second);
    return out;
}

void writeBytes(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    file.write(data);
}

void writeJsonFile(const QString& path, const QJsonDocument& document) {
    writeBytes(path, document.toJson());
}

QJsonDocument readJsonFile(const QString& path) {
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll());
}

QList<int> toneIds(const QList<QJsonObject>& tones) {
    QList<int> ids;
    for (const QJsonObject& tone : tones) ids << tone.value("id").toInt();
    return ids;
}

bool hasId(const QList<QJsonObject>& tones, int id) {
    return toneIds(tones).contains(id);
}

QJsonObject toneWithId(int id, const QString& title, const QString& slug = {}) {
    QJsonObject tone{{"id", id}, {"title", title}};
    if (!slug.isEmpty()) tone["slug"] = slug;
    return tone;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── Query::toUrlQuery / endpoint ─────────────────────────────────────────
    {
        Query q;
        const QMap<QString, QString> nam = queryMap(q.toUrlQuery(1));
        assert(nam.value("format") == "nam");
        assert(nam.value("gears") == "amp-cab");
        assert(nam.value("architecture") == "2");
        assert(nam.value("sort") == "trending");
        assert(nam.value("page") == "1");
        assert(nam.value("page_size") == "25");
        assert(!nam.contains("query"));
        assert(!nam.contains("creators"));
        assert(!nam.contains("sizes"));
        assert(!nam.contains("calibrated"));
        assert(nam.size() == 6);
        assert(q.endpoint() == "https://www.tone3000.com/api/v1/tones/search");
        assert(q.pageSize() == Query::kPageSize);

        Query named = q;
        named.text = "fender";
        named.tags = "clean,crunch";
        named.gear = "amp,pedal";
        assert(queryMap(named.toUrlQuery(2)).value("query") == "fender");
        assert(queryMap(named.toUrlQuery(2)).value("tags") == "clean,crunch");
        assert(queryMap(named.toUrlQuery(2)).value("gears") == "amp,pedal");
        assert(queryMap(named.toUrlQuery(2)).value("page") == "2");

        Query tagOnly;
        tagOnly.tags = "clean";
        assert(!queryMap(tagOnly.toUrlQuery(1)).contains("query"));
        assert(queryMap(tagOnly.toUrlQuery(1)).value("tags") == "clean");

        Query downloads = q;
        downloads.sort = "downloads";
        assert(queryMap(downloads.toUrlQuery(1)).value("sort") == "downloads-all-time");

        Query withCreator = q;
        withCreator.creator = "alice";
        assert(queryMap(withCreator.toUrlQuery(1)).value("creators") == "alice");

        Query calibrated = q;
        calibrated.calibrated = true;
        assert(queryMap(calibrated.toUrlQuery(1)).value("calibrated") == "true");

        Query ir;
        ir.format = Format::Ir;
        ir.text = "fender";
        ir.tags = "clean";
        ir.gear = "cab,space";
        ir.architecture = "2";
        ir.size = "lite";
        ir.calibrated = true;
        ir.creator = "alice";
        ir.sort = "newest";
        const QMap<QString, QString> irItems = queryMap(ir.toUrlQuery(3));
        assert(irItems.value("format") == "ir");
        assert(irItems.value("query") == "fender");
        assert(irItems.value("tags") == "clean");   // gear and tags apply to IRs too
        assert(irItems.value("gears") == "cab,space");
        assert(irItems.value("creators") == "alice");
        assert(irItems.value("sort") == "newest");
        assert(irItems.value("page") == "3");
        assert(irItems.value("page_size") == "25");
        assert(!irItems.contains("architecture"));
        assert(!irItems.contains("sizes"));
        assert(!irItems.contains("calibrated"));

        Query creators;
        creators.source = Query::Source::Creators;
        creators.text = "bob";
        creators.sort = "newest";
        creators.gear = "amp";
        creators.tags = "clean";
        creators.architecture = "1";
        creators.size = "lite";
        creators.calibrated = true;
        creators.format = Format::Ir;
        const QUrlQuery creatorsQuery = creators.toUrlQuery(4);
        const QMap<QString, QString> creatorItems = queryMap(creatorsQuery);
        assert(creatorItems.value("query") == "bob");
        assert(creatorItems.value("sort") == "downloads");
        assert(creatorItems.value("page") == "4");
        assert(creatorItems.value("page_size") == "10");
        assert(creatorItems.size() == 4);
        assert(!creatorItems.contains("format"));
        assert(!creatorItems.contains("gears"));
        assert(!creatorItems.contains("architecture"));
        assert(!creatorItems.contains("sizes"));
        assert(!creatorItems.contains("calibrated"));
        assert(!creatorItems.contains("creators"));
        assert(creators.endpoint() == "https://www.tone3000.com/api/v1/users");
        assert(creators.pageSize() == Query::kCreatorPageSize);

        Query creatorsEmpty;
        creatorsEmpty.source = Query::Source::Creators;
        const QMap<QString, QString> emptyCreators = queryMap(creatorsEmpty.toUrlQuery(1));
        assert(!emptyCreators.contains("query"));
        assert(emptyCreators.value("sort") == "downloads");
        assert(emptyCreators.value("page_size") == "10");
    }

    // ── ToneItem::modelCountFor ────────────────────────────────────────────────
    {
        const ToneItem counted = ToneItem::fromJson(QJsonObject{
            {"id", 1}, {"models_count", 2}, {"a1_models_count", 1}, {"a2_models_count", 1}});
        assert(counted.modelCountFor("2") == 1);
        assert(counted.modelCountFor("1") == 1);
        assert(counted.modelCountFor("") == 2);
        const ToneItem unknown = ToneItem::fromJson(QJsonObject{{"id", 2}, {"models_count", 3}});
        assert(unknown.modelCountFor("2") == 3); // no per-architecture counts: fall back to all
    }

    // ── Query::cacheKey ──────────────────────────────────────────────────────
    {
        Query a;
        Query b = a;
        assert(a.cacheKey(1) == b.cacheKey(1));
        assert(a.cacheKey(1) != a.cacheKey(2));

        auto differs = [&](auto mutate, const char* field) {
            Query changed = a;
            mutate(changed);
            if (changed.cacheKey(1) == a.cacheKey(1)) {
                std::cout << "SOURCE DISCREPANCY: cacheKey did not change when " << field
                          << " differed" << std::endl;
                assert(false);
            }
        };
        differs([](Query& q) { q.format = Format::Ir; }, "format");
        differs([](Query& q) { q.source = Query::Source::Creators; }, "source");
        differs([](Query& q) { q.text = "fender"; }, "text");
        differs([](Query& q) { q.creator = "alice"; }, "creator");
        differs([](Query& q) { q.sort = "newest"; }, "sort");
        differs([](Query& q) { q.gear = "amp"; }, "gear");
        differs([](Query& q) { q.tags = "clean"; }, "tags");
        differs([](Query& q) { q.architecture = "1"; }, "architecture");
        differs([](Query& q) { q.size = "lite"; }, "size");
        differs([](Query& q) { q.calibrated = true; }, "calibrated");
    }

    // ── Query JSON, filters, describe ────────────────────────────────────────
    {
        Query original;
        original.source = Query::Source::Creators;
        original.text = "fender";
        original.creator = "alice";
        original.sort = "newest";
        original.gear = "amp";
        original.tags = "clean,metal";
        original.architecture = "1";
        original.size = "lite";
        original.calibrated = true;
        original.format = Format::Nam;
        const Query roundTrip = Query::fromJson(original.toJson(), Format::Nam);
        assert(roundTrip == original);

        Query ir = Query::fromJson(original.toJson(), Format::Ir);
        assert(ir.format == Format::Ir);
        assert(ir.text == original.text);

        Query unknownSort = Query::fromJson(QJsonObject{{"sort", "not-a-real-sort"}, {"text", "x"}}, Format::Nam);
        assert(unknownSort.sort == "trending");

        Query known = Query::fromJson(QJsonObject{{"sort", "downloads"}}, Format::Nam);
        assert(known.sort == "downloads");

        Query filters;
        assert(!filters.hasFilters());
        filters.tags = "clean";
        assert(filters.hasFilters());
        filters.resetFilters();
        assert(!filters.hasFilters());
        assert(filters.gear == "amp-cab");
        assert(filters.tags.isEmpty());
        assert(filters.architecture == "2");
        assert(filters.size.isEmpty());
        assert(!filters.calibrated);

        filters.gear = "pedal";
        assert(filters.hasFilters());
        filters.resetFilters();
        filters.architecture = "1";
        assert(filters.hasFilters());
        filters.resetFilters();
        filters.size = "lite";
        assert(filters.hasFilters());
        filters.resetFilters();
        filters.calibrated = true;
        assert(filters.hasFilters());
        filters.resetFilters();

        Query irFilters = Query::defaults(Format::Ir);
        assert(irFilters.gear.isEmpty());
        assert(!irFilters.hasFilters());
        irFilters.calibrated = true; // NAM-only, does not count for IRs
        assert(!irFilters.hasFilters());
        irFilters.gear = "cab";
        assert(irFilters.hasFilters());
        irFilters.resetFilters();
        assert(irFilters.gear.isEmpty() && !irFilters.hasFilters());
        irFilters.tags = "metal";
        assert(irFilters.hasFilters());

        // Settings from before tags: the single "character" becomes a tag.
        assert(Query::fromJson(QJsonObject{{"character", "high-gain"}}, Format::Nam).tags == "high-gain");
        // Gear types from the other format are dropped.
        assert(Query::fromJson(QJsonObject{{"gear", "amp-cab,cab"}}, Format::Ir).gear == "cab");
        assert(Query::fromJson(QJsonObject{{"gear", "space"}}, Format::Nam).gear.isEmpty());
        assert(Query::fromJson(QJsonObject{}, Format::Nam).gear == "amp-cab");

        Query described;
        described.text = "fender";
        const QString summary = described.describe();
        assert(!summary.isEmpty());
        assert(summary.contains("\"fender\""));
    }

    // ── ToneItem::fromJson ───────────────────────────────────────────────────
    {
        const QJsonObject viaUser{
            {"id", 11},
            {"title", "Deluxe"},
            {"user", QJsonObject{{"username", "nested-user"}}},
            {"username", "top-level-user"},
            {"images", QJsonArray{QJsonObject{{"url", "https://cdn.example/a.jpg"}}}},
            {"url", "/tones/deluxe"},
            {"slug", "deluxe"},
            {"models_count", 4},
            {"model_count", 9},
            {"tags", QJsonArray{"clean", QJsonObject{{"name", "fender"}}}},
            {"makes", QJsonArray{QJsonObject{{"name", "Fender"}}, "Marshall"}},
        };
        const ToneItem nested = ToneItem::fromJson(viaUser);
        assert(nested.id == 11);
        assert(nested.title == "Deluxe");
        assert(nested.creator == "nested-user");
        assert(nested.imageUrl == "https://cdn.example/a.jpg");
        assert(nested.url == "https://www.tone3000.com/tones/deluxe");
        assert(nested.modelCount == 4);
        assert(nested.tags == (QStringList{"clean", "fender"}));
        assert(nested.makes == (QStringList{"Fender", "Marshall"}));

        const QJsonObject viaUsername{
            {"id", 12},
            {"title", "Twin"},
            {"username", "solo-user"},
            {"images", QJsonArray{"https://cdn.example/b.jpg", QJsonObject{{"url", "https://cdn.example/c.jpg"}}}},
            {"url", "https://www.tone3000.com/tones/twin"},
            {"model_count", 2},
        };
        const ToneItem top = ToneItem::fromJson(viaUsername);
        assert(top.creator == "solo-user");
        assert(top.imageUrl == "https://cdn.example/b.jpg");
        assert(top.url == "https://www.tone3000.com/tones/twin");
        assert(top.modelCount == 2);
    }

    // ── matchesText ──────────────────────────────────────────────────────────
    {
        ToneItem tone;
        tone.title = "Fender Deluxe Reverb";
        tone.creator = "Alice";
        tone.tags = {"Clean", "American"};
        assert(Tone3000::matchesText(tone, {}));
        assert(Tone3000::matchesText(tone, "   "));
        assert(Tone3000::matchesText(tone, "fender"));
        assert(Tone3000::matchesText(tone, "ALICE"));
        assert(Tone3000::matchesText(tone, "clean"));
        assert(Tone3000::matchesText(tone, "fender alice"));
        assert(Tone3000::matchesText(tone, "Deluxe CLEAN"));
        assert(!Tone3000::matchesText(tone, "fender marshall"));
        assert(!Tone3000::matchesText(tone, "missing"));
    }

    // ── toneFolderName / modelFileName ───────────────────────────────────────
    {
        assert(Tone3000::toneFolderName(QJsonObject{{"id", 42}, {"slug", "my-amp"}}) == "tone_42_my-amp");
        assert(Tone3000::toneFolderName(QJsonObject{{"id", 7}, {"title", "Hello World!"}}) == "tone_7_hello-world");
        const QString unsafe = Tone3000::toneFolderName(QJsonObject{{"id", 1}, {"slug", "My Amp!"}});
        assert(unsafe == "tone_1_My_Amp_");
        assert(Tone3000::toneFolderName(QJsonObject{}) == "tone_0_profile");

        const QJsonObject named{{"name", "Clean Take"}, {"model_url", "https://cdn.example/models/Clean.nam?x=1"}};
        assert(Tone3000::modelFileName(named, Format::Nam, false) == "Clean_Take.nam");
        assert(Tone3000::modelFileName(named, Format::Nam, true) == "preview_Clean_Take.nam");

        const QJsonObject unknownExt{{"name", "cab"}, {"model_url", "https://cdn.example/file.xyz"}};
        assert(Tone3000::modelFileName(unknownExt, Format::Nam, false) == "cab.nam");
        assert(Tone3000::modelFileName(unknownExt, Format::Ir, false) == "cab.wav");

        const QJsonObject emptyName{{"model_url", "https://cdn.example/a.wav"}};
        assert(Tone3000::modelFileName(emptyName, Format::Ir, true) == "preview_file.wav");

        const QJsonObject unsafeName{{"name", "foo/bar baz"}, {"model_url", "https://cdn.example/x.flac"}};
        assert(Tone3000::modelFileName(unsafeName, Format::Ir, false) == "foo_bar_baz.flac");
    }

    // ── Tone3000Library persistence ──────────────────────────────────────────
    {
        QTemporaryDir config;
        QTemporaryDir cache;
        assert(config.isValid() && cache.isValid());

        const QJsonObject namTone = toneWithId(1, "NAM Tone", "nam-tone");
        const QJsonObject irTone = toneWithId(2, "IR Tone", "ir-tone");
        const QJsonArray namModels{QJsonObject{{"name", "A"}}};

        {
            Tone3000Library library(config.path());
            library.setFavorite(namTone, namModels, Format::Nam, true);
            assert(library.isFavorite(1));
            assert(hasId(library.favorites(Format::Nam), 1));
            assert(!hasId(library.favorites(Format::Ir), 1));
        }

        const QJsonArray savedFavorites = readJsonFile(config.path() + "/favorites.json").array();
        assert(savedFavorites.size() == 1);
        assert(savedFavorites.at(0).toObject().value("id").toInt() == 1);
        assert(savedFavorites.at(0).toObject().value("rigroom_format").toString() == "nam");
        assert(savedFavorites.at(0).toObject().value("models").toArray().size() == 1);

        {
            Tone3000Library library(config.path());
            assert(library.isFavorite(1));
            assert(hasId(library.favorites(Format::Nam), 1));
            library.setFavorite(namTone, {}, Format::Nam, false);
            assert(!library.isFavorite(1));
            assert(library.favorites(Format::Nam).isEmpty());
        }
        {
            Tone3000Library library(config.path());
            assert(!library.isFavorite(1));
            assert(library.favorites(Format::Nam).isEmpty());
        }

        writeJsonFile(config.path() + "/favorites.json",
                      QJsonDocument(QJsonArray{
                          QJsonObject{{"id", 1}, {"title", "NAM"}, {"rigroom_format", "nam"}},
                          QJsonObject{{"id", 2}, {"title", "IR"}, {"rigroom_format", "ir"}},
                          QJsonObject{{"id", 3}, {"title", "Legacy local"}},
                      }));

        Tone3000Library library(config.path());
        assert(library.isFavorite(1) && library.isFavorite(2) && library.isFavorite(3));
        assert(hasId(library.favorites(Format::Nam), 1));
        assert(hasId(library.favorites(Format::Nam), 3));
        assert(!hasId(library.favorites(Format::Nam), 2));
        assert(hasId(library.favorites(Format::Ir), 2));
        assert(!hasId(library.favorites(Format::Ir), 1));
        assert(!hasId(library.favorites(Format::Ir), 3));

        library.setRemoteFavorites(QJsonArray{
            QJsonObject{{"id", 4}, {"title", "Remote no format"}},
            QJsonObject{{"id", 5}, {"title", "Remote NAM"}, {"rigroom_format", "nam"}},
            QJsonObject{{"id", 2}, {"title", "Remote copy of IR"}, {"rigroom_format", "ir"}},
        });
        assert(library.hasRemoteFavorites());
        assert(library.isFavorite(1) && library.isFavorite(2) && library.isFavorite(3));
        assert(library.isFavorite(4) && library.isFavorite(5));
        assert(hasId(library.favorites(Format::Nam), 4));
        assert(hasId(library.favorites(Format::Ir), 4));
        assert(hasId(library.favorites(Format::Nam), 5));
        assert(!hasId(library.favorites(Format::Ir), 5));
        assert(hasId(library.favorites(Format::Nam), 1));
        assert(hasId(library.favorites(Format::Nam), 3));
        assert(hasId(library.favorites(Format::Ir), 2));

        library.setFavorite(QJsonObject{{"id", 4}, {"title", "Remote no format"}}, {}, Format::Nam, false);
        assert(!library.isFavorite(4));
        assert(!hasId(library.favorites(Format::Nam), 4));
        assert(!hasId(library.favorites(Format::Ir), 4));
        assert(library.isFavorite(1) && library.isFavorite(5));

        Query namSearch;
        namSearch.text = "mesa";
        Query irSearch;
        irSearch.format = Format::Ir;
        irSearch.text = "hall";
        library.addSavedSearch("Mesa", namSearch);
        library.addSavedSearch("Hall", irSearch);
        assert(library.savedSearches(Format::Nam).size() == 1);
        assert(library.savedSearches(Format::Ir).size() == 1);
        library.addSavedSearch("Mesa renamed", namSearch);
        assert(library.savedSearches(Format::Nam).size() == 1);
        assert(library.savedSearches(Format::Nam).at(0).name == "Mesa renamed");
        library.removeSavedSearch(Format::Nam, 0);
        assert(library.savedSearches(Format::Nam).isEmpty());
        assert(library.savedSearches(Format::Ir).size() == 1);
    }

    {
        QTemporaryDir config;
        QTemporaryDir cache;
        assert(config.isValid() && cache.isValid());
        {
            Tone3000Library library(config.path());
            Query search;
            search.text = "soldano";
            library.addSavedSearch("Soldano", search);
        }
        Tone3000Library reloaded(config.path());
        assert(reloaded.savedSearches(Format::Nam).size() == 1);
        assert(reloaded.savedSearches(Format::Nam).at(0).name == "Soldano");
        assert(reloaded.savedSearches(Format::Nam).at(0).query.text == "soldano");
    }

    std::cout << "Tone3000 tests passed" << std::endl;
    return 0;
}
