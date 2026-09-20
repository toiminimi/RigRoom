#pragma once
#include "PluginSearch.h"
#include <QDialog>
#include <QPixmap>
#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPushButton;
class QToolButton;
class QButtonGroup;
class PluginListModel;

// Images for the browser: a snapshot of the plugin's own GUI if one was made,
// else its LV2/MOD thumbnail, else generated artwork. Decoding happens off the
// GUI thread; results live in a process-wide cache.
namespace PluginArt {
QString snapshotPath(const QString& uri);          // ~/.cache/RigRoom/plugin-previews/<sha1>.png
QString imagePathFor(const PluginEntry& entry);    // best existing file, or empty
QPixmap generated(const PluginEntry& entry, const QSize& size);
QColor categoryColor(const QString& category);
}

class PluginBrowserDialog : public QDialog {
    Q_OBJECT
public:
    // library = true: a window you keep open next to the board. Plugins are
    // dragged onto the signal chain, or "Add" puts one at the end of the chain
    // (pluginChosen), and the window stays open. Otherwise a one-shot picker.
    PluginBrowserDialog(const std::vector<PluginEntry>& plugins, QSet<QString>* favorites,
                        QStringList* recent, std::function<void()> changed, QWidget* parent = nullptr,
                        bool library = false);

    static constexpr const char* kPluginMimeType = "application/x-rigroom-plugin-uri";

    QString selectedUri() const { return m_selectedUri; }

signals:
    void pluginChosen(const QString& uri);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(class QResizeEvent* event) override;
    void keyPressEvent(class QKeyEvent* event) override;

private:
    void refresh();
    void rebuildSidebar();
    void showDetails(int pluginIndex);
    // The preview opens full size over the browser, not in a window of its own.
    void showFullPreview();
    void hideFullPreview();
    void toggleFavorite(int pluginIndex);
    void accept() override;
    int currentPluginIndex() const;

    std::vector<PluginEntry> m_plugins;
    QSet<QString>* m_favorites;
    QStringList* m_recent;
    std::function<void()> m_changed;
    PluginFilter m_filter;
    QString m_selectedUri;
    bool m_library = false;

    PluginListModel* m_model = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_sidebar = nullptr;
    QListView* m_list = nullptr;
    QButtonGroup* m_formatGroup = nullptr;
    QLabel* m_count = nullptr;

    // Info panel
    QLabel* m_preview = nullptr;
    QLabel* m_previewHint = nullptr;
    QWidget* m_fullPreview = nullptr;
    QLabel* m_fullPreviewImage = nullptr;
    QString m_previewPath;      // the screenshot behind the panel image, if any
    QLabel* m_name = nullptr;
    QLabel* m_byline = nullptr;
    QLabel* m_chips = nullptr;
    QLabel* m_description = nullptr;
    QToolButton* m_moreButton = nullptr;
    QLabel* m_facts = nullptr;
    QLabel* m_tags = nullptr;
    QToolButton* m_detailsToggle = nullptr;
    QLabel* m_technical = nullptr;
    QPushButton* m_favoriteButton = nullptr;
    QPushButton* m_addButton = nullptr;
    int m_detailsIndex = -1;
};
