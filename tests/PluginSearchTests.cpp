#include "ui/PluginSearch.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cassert>
#include <iostream>

namespace {
PluginEntry entry(const QString& name, const QString& brand, const QString& category, const QString& format,
                  const QString& uri) {
    PluginEntry e;
    e.name = name;
    e.brand = brand;
    e.category = category;
    e.format = format;
    e.uri = uri;
    e.finalize();
    return e;
}

QStringList names(const std::vector<PluginEntry>& plugins, const std::vector<int>& rows) {
    QStringList out;
    for (int r : rows) out << plugins[r].name;
    return out;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::vector<PluginEntry> plugins = {
        entry("Calf Reverb", "Calf", "Reverbs", "LV2", "urn:calf:reverb"),
        entry("Dragonfly Hall Reverb", "Michael Willis", "Reverbs", "CLAP", "/clap/dfhall.clap:0"),
        entry("GxTuner", "Guitarix", "Utilities", "LV2", "urn:gx:tuner"),
        entry("Neural Amp Modeler", "Mike Oliphant", "Amplifiers", "LV2", "urn:nam"),
        entry("TooB Cab IR", "Two-Play", "Utilities", "LV2", "urn:toob:cab"),
        entry("Reverse Delay", "Zam", "Delays", "VST3", "/vst3/rev.vst3"),
    };

    PluginFilter filter;
    QSet<QString> favorites;
    QStringList recent;

    // Empty query: everything, A-Z.
    auto rows = filterPlugins(plugins, filter, favorites, recent);
    assert(rows.size() == plugins.size());
    assert(names(plugins, rows).first() == "Calf Reverb");

    // Every word must match; name-prefix beats "contains".
    filter.text = "rev";
    auto list = names(plugins, filterPlugins(plugins, filter, favorites, recent));
    assert(list.size() == 3);
    assert(list.first() == "Reverse Delay");
    filter.text = "hall reverb";
    list = names(plugins, filterPlugins(plugins, filter, favorites, recent));
    assert(list == QStringList{"Dragonfly Hall Reverb"});

    // Brand and category are searchable too.
    filter.text = "oliphant";
    assert(names(plugins, filterPlugins(plugins, filter, favorites, recent)) == QStringList{"Neural Amp Modeler"});

    // Favourites rank first among equal matches.
    filter.text = "reverb";
    favorites.insert("/clap/dfhall.clap:0");
    list = names(plugins, filterPlugins(plugins, filter, favorites, recent));
    assert(list.first() == "Dragonfly Hall Reverb");

    // Format and views.
    filter.text.clear();
    filter.format = "LV2";
    assert(filterPlugins(plugins, filter, favorites, recent).size() == 4);
    filter.format.clear();
    filter.view = PluginFilter::View::Favorites;
    assert(names(plugins, filterPlugins(plugins, filter, favorites, recent)) == QStringList{"Dragonfly Hall Reverb"});
    filter.view = PluginFilter::View::Category;
    filter.category = "Reverbs";
    assert(filterPlugins(plugins, filter, favorites, recent).size() == 2);
    filter.view = PluginFilter::View::Recent;
    recent = {"urn:nam", "urn:gx:tuner"};
    assert(names(plugins, filterPlugins(plugins, filter, favorites, recent)) == (QStringList{"Neural Amp Modeler", "GxTuner"}));

    // Speed: ~800 plugins (like a big LV2 install) filter well under 5 ms per keystroke.
    std::vector<PluginEntry> many;
    for (int i = 0; i < 800; ++i) {
        many.push_back(entry(QString("Plugin %1 Reverb Delay").arg(i), "Brand", "Delays", "LV2", QString("urn:%1").arg(i)));
    }
    PluginFilter typing;
    QElapsedTimer timer;
    timer.start();
    for (const QString& text : {"r", "re", "rev", "reve", "rever", "reverb", "reverb 7"}) {
        typing.text = text;
        filterPlugins(many, typing, favorites, recent);
    }
    const double perKey = timer.nsecsElapsed() / 1e6 / 7.0;
    std::cout << "filter 800 plugins: " << perKey << " ms per keystroke" << std::endl;
    assert(perKey < 5.0);

    // Category guesses: tags win, then the name; short words must stand alone.
    assert(inferPluginCategory("Dragonfly Hall Reverb", {"audio-effect", "reverb", "stereo"}) == "Reverbs");
    assert(inferPluginCategory("Something", {"audio-effect", "delay"}) == "Delays");
    assert(inferPluginCategory("ZamEQ2", {}) == "Utilities" || inferPluginCategory("Zam EQ2", {}) == "EQ & Filters");
    assert(inferPluginCategory("Zam EQ 2", {}) == "EQ & Filters");
    assert(inferPluginCategory("Tube Screamer", {}) == "Distortions");
    assert(inferPluginCategory("TONE3000", {}) == "Utilities");
    assert(inferPluginCategory("Sequencer", {}) == "Utilities"); // "eq" inside a word doesn't count
    assert(inferPluginCategory("Stereo Chorus", {}) == "Modulations");

    std::cout << "Plugin search tests passed" << std::endl;
    return 0;
}
