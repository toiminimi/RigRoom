#include "PluginSearch.h"
#include <algorithm>

void PluginEntry::finalize() {
    searchable = QStringList{name, brand, author, category, format, features.join(' '), uri}.join(' ').toLower();
}

QString inferPluginCategory(const QString& name, const QStringList& tags) {
    struct Rule { const char* category; QStringList words; };
    static const std::vector<Rule> rules = {
        {"Amplifiers", {"amplifier", "amp-sim", "preamp", "amp ", "nam", "cabinet", "cab ", "cab-", "impulse"}},
        {"Reverbs", {"reverb", "verb", "hall", "plate", "room", "spring"}},
        {"Delays", {"delay", "echo"}},
        {"Distortions", {"distortion", "overdrive", "fuzz", "drive", "saturat", "clip", "screamer", "crusher"}},
        {"Modulations", {"chorus", "flanger", "phaser", "tremolo", "vibrato", "modulation", "rotary", "ring-mod", "ensemble"}},
        {"Dynamics", {"compressor", "limiter", "gate", "expander", "dynamics", "maximi", "comp "}},
        {"EQ & Filters", {"equalizer", "eq", "filter", "wah"}},
    };
    // Tags first (they are deliberate), then the name.
    const QString tagText = ' ' + tags.join(' ').toLower() + ' ';
    const QString nameText = ' ' + name.toLower() + ' ';
    for (const QString& text : {tagText, nameText}) {
        for (const Rule& rule : rules) {
            for (const QString& w : rule.words) {
                // Short words must stand alone ("eq", "amp", "nam"), longer ones may be part of a word.
                const bool whole = w.size() <= 3;
                if (whole ? (text.contains(' ' + w + ' ') || text.contains(' ' + w + '-') || text.contains('-' + w + ' '))
                          : text.contains(w)) {
                    return rule.category;
                }
            }
        }
    }
    return "Utilities";
}

QString pluginCategoryGlyph(const QString& category) {
    if (category == "Amplifiers") return "AMP";
    if (category == "Delays") return "DLY";
    if (category == "Reverbs") return "RVB";
    if (category == "Delays & Reverbs") return "D/R";
    if (category == "Distortions") return "DST";
    if (category == "Dynamics") return "DYN";
    if (category == "EQ & Filters") return "EQ";
    if (category == "Modulations") return "MOD";
    return "FX";
}

std::vector<int> filterPlugins(const std::vector<PluginEntry>& plugins, const PluginFilter& filter,
                               const QSet<QString>& favorites, const QStringList& recent) {
    const QStringList words = filter.text.toLower().split(' ', Qt::SkipEmptyParts);
    const QString first = words.isEmpty() ? QString() : words.first();

    struct Hit { int index; int score; };
    std::vector<Hit> hits;
    hits.reserve(plugins.size());
    for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
        const PluginEntry& p = plugins[i];
        if (!filter.format.isEmpty() && p.format != filter.format) continue;
        switch (filter.view) {
        case PluginFilter::View::Favorites: if (!favorites.contains(p.uri)) continue; break;
        case PluginFilter::View::Recent: if (!recent.contains(p.uri)) continue; break;
        case PluginFilter::View::Category: if (p.category != filter.category) continue; break;
        case PluginFilter::View::All: break;
        }
        bool all = true;
        for (const QString& w : words) {
            if (!p.searchable.contains(w)) { all = false; break; }
        }
        if (!all) continue;

        int score = 0;
        if (!first.isEmpty()) {
            const QString lowerName = p.name.toLower();
            if (lowerName.startsWith(first)) score += 100;
            else if (lowerName.contains(' ' + first) || lowerName.contains('-' + first)) score += 60;
            else if (lowerName.contains(first)) score += 30;
            if (p.brand.toLower().startsWith(first)) score += 20;
        }
        if (favorites.contains(p.uri)) score += 10;
        hits.push_back({i, score});
    }

    if (filter.view == PluginFilter::View::Recent) {
        std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
            return recent.indexOf(plugins[a.index].uri) < recent.indexOf(plugins[b.index].uri);
        });
    } else {
        std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
            if (a.score != b.score) return a.score > b.score;
            return QString::compare(plugins[a.index].name, plugins[b.index].name, Qt::CaseInsensitive) < 0;
        });
    }

    std::vector<int> result;
    result.reserve(hits.size());
    for (const Hit& h : hits) result.push_back(h.index);
    return result;
}
