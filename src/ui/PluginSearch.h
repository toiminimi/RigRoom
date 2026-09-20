#pragma once
#include <QSet>
#include <QString>
#include <QStringList>
#include <vector>

// Plugin browser data and search, free of widgets so it can be unit-tested.

struct PluginEntry {
    QString name;
    QString uri;
    QString category;
    QString brand;
    QString author;
    QString format;       // "LV2", "CLAP", "VST3"
    QString version;
    QString description;
    QString license;
    QString path;
    QString thumbnailPath;
    QStringList features;
    int audioInputs = 2;
    int audioOutputs = 2;
    int controlPorts = 0;
    bool hasNativeGUI = false;
    QString searchable;   // lowercase haystack, filled by finalize()

    void finalize();
};

struct PluginFilter {
    enum class View { All, Favorites, Recent, Category };
    QString text;
    QString format;       // empty = all formats
    View view = View::All;
    QString category;     // for View::Category
};

// Indices into `plugins`, ranked: every word of the query must match
// (name, brand, author, category, format, tags, URI); name-prefix and
// whole-word name matches come first, favourites before others, then A-Z.
// The Recent view keeps most-recent-first order.
std::vector<int> filterPlugins(const std::vector<PluginEntry>& plugins, const PluginFilter& filter,
                               const QSet<QString>& favorites, const QStringList& recent);

// Category from plugin tags (CLAP features, LV2 class words) and, when those
// are generic, from the name. Returns "Utilities" when nothing matches.
QString inferPluginCategory(const QString& name, const QStringList& tags);

// Short text badge for a category, e.g. "AMP", "DLY".
QString pluginCategoryGlyph(const QString& category);
