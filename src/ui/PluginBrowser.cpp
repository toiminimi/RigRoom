#include "PluginBrowser.h"
#include <QAbstractListModel>
#include <QApplication>
#include <QButtonGroup>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <map>

// ─── Art ─────────────────────────────────────────────────────────────────────

QString PluginArt::snapshotPath(const QString& uri) {
    const QByteArray hash = QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir::homePath() + "/.cache/RigRoom/plugin-previews/" + QString::fromLatin1(hash) + ".png";
}

QString PluginArt::imagePathFor(const PluginEntry& entry) {
    const QString snapshot = snapshotPath(entry.uri);
    if (QFileInfo::exists(snapshot)) return snapshot;
    if (!entry.thumbnailPath.isEmpty() && QFileInfo::exists(entry.thumbnailPath)) return entry.thumbnailPath;
    return QString();
}

QColor PluginArt::categoryColor(const QString& category) {
    if (category == "Amplifiers") return QColor("#E05252");
    if (category == "Delays") return QColor("#3FA7D6");
    if (category == "Reverbs") return QColor("#7E6BD9");
    if (category == "Delays & Reverbs") return QColor("#5B8BD9");
    if (category == "Distortions") return QColor("#E8913A");
    if (category == "Dynamics") return QColor("#D9A93F");
    if (category == "EQ & Filters") return QColor("#C9C44A");
    if (category == "Modulations") return QColor("#D95BA0");
    return QColor("#4DB6AC");
}

QPixmap PluginArt::generated(const PluginEntry& entry, const QSize& size) {
    const QString key = QString("gen:%1:%2x%3").arg(entry.uri).arg(size.width()).arg(size.height());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) return cached;

    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = categoryColor(entry.category);
    const QRectF r(0.5, 0.5, size.width() - 1.0, size.height() - 1.0);
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0.0, accent.darker(260));
    bg.setColorAt(1.0, QColor("#141418"));
    QPainterPath path;
    path.addRoundedRect(r, size.height() > 80 ? 10 : 6, size.height() > 80 ? 10 : 6);
    p.fillPath(path, bg);
    p.setPen(QPen(accent.darker(140), 1));
    p.drawPath(path);

    const bool large = size.height() > 80;
    QFont glyphFont = p.font();
    glyphFont.setBold(true);
    glyphFont.setPixelSize(large ? size.height() / 3 : size.height() / 3);
    p.setFont(glyphFont);
    p.setPen(accent.lighter(115));
    const QString glyph = pluginCategoryGlyph(entry.category);
    if (large) {
        // A pedal-like card: category glyph, name and maker.
        p.drawText(r.adjusted(16, 12, -16, -r.height() / 2), Qt::AlignLeft | Qt::AlignTop, glyph);
        QFont nameFont = p.font();
        nameFont.setPixelSize(std::max(13, size.height() / 9));
        p.setFont(nameFont);
        p.setPen(QColor("#F2F2F5"));
        const QRectF nameRect = r.adjusted(16, r.height() * 0.52, -16, -r.height() * 0.22);
        p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(nameFont).elidedText(entry.name, Qt::ElideRight, qRound(nameRect.width())));
        QFont smallFont = p.font();
        smallFont.setBold(false);
        smallFont.setPixelSize(std::max(10, size.height() / 14));
        p.setFont(smallFont);
        p.setPen(QColor("#9A9AA6"));
        const QString maker = !entry.brand.isEmpty() ? entry.brand : (!entry.author.isEmpty() ? entry.author : entry.format);
        p.drawText(r.adjusted(16, r.height() * 0.76, -16, -8), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(smallFont).elidedText(maker, Qt::ElideRight, qRound(r.width() - 32)));
        // A knob row hints "effect unit" without pretending to be the real GUI.
        p.setPen(Qt::NoPen);
        for (int k = 0; k < 3; ++k) {
            const qreal d = size.height() / 6.5;
            const QPointF c(r.right() - 20 - d / 2 - k * (d + 10), r.top() + 20 + d / 2);
            p.setBrush(QColor(0, 0, 0, 110));
            p.drawEllipse(c, d / 2, d / 2);
            p.setPen(QPen(accent, 2));
            p.drawLine(c, c + QPointF(0, -d / 2 + 3));
            p.setPen(Qt::NoPen);
        }
    } else {
        p.drawText(r, Qt::AlignCenter, glyph);
    }
    p.end();
    QPixmapCache::insert(key, pix);
    return pix;
}

// ─── Async image loading ─────────────────────────────────────────────────────

namespace {
class ImageLoader : public QObject {
public:
    static ImageLoader& instance() {
        static ImageLoader loader;
        return loader;
    }

    // Returns the scaled image if ready; otherwise starts loading it and
    // calls `ready` on the GUI thread when done.
    QPixmap get(const QString& path, const QSize& size, std::function<void()> ready) {
        const QString key = QString("img:%1:%2x%3").arg(path).arg(size.width()).arg(size.height());
        QPixmap pix;
        if (QPixmapCache::find(key, &pix)) return pix;
        if (m_failed.contains(path)) return QPixmap();
        auto& waiters = m_pending[key];
        if (ready) waiters.push_back(std::move(ready));
        if (waiters.size() > 1 || (!ready && m_started.contains(key))) return QPixmap();
        m_started.insert(key);

        auto* watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key, path]() {
            const QImage image = watcher->result();
            watcher->deleteLater();
            m_started.remove(key);
            if (image.isNull()) m_failed.insert(path);
            else QPixmapCache::insert(key, QPixmap::fromImage(image));
            auto waiters = std::move(m_pending[key]);
            m_pending.erase(key);
            for (auto& w : waiters) if (w) w();
        });
        watcher->setFuture(QtConcurrent::run([path, size]() {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            QImage image = reader.read();
            if (image.isNull()) return image;
            return image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }));
        return QPixmap();
    }

private:
    std::map<QString, std::vector<std::function<void()>>> m_pending;
    QSet<QString> m_started;
    QSet<QString> m_failed;
};

constexpr int kIndexRole = Qt::UserRole + 1;
constexpr int kRowHeight = 46;
constexpr int kIconSize = 36;
} // namespace

// ─── Model ───────────────────────────────────────────────────────────────────

class PluginListModel : public QAbstractListModel {
public:
    explicit PluginListModel(const std::vector<PluginEntry>& plugins, QObject* parent)
        : QAbstractListModel(parent), m_plugins(plugins) {}

    void setRows(std::vector<int> rows) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size())) return {};
        const int pluginIndex = m_rows[index.row()];
        if (role == kIndexRole) return pluginIndex;
        if (role == Qt::DisplayRole) return m_plugins[pluginIndex].name;
        if (role == Qt::ToolTipRole) {
            const auto& p = m_plugins[pluginIndex];
            return p.name + (p.brand.isEmpty() ? QString() : " — " + p.brand);
        }
        return {};
    }
    void pluginChanged(int pluginIndex) {
        for (int row = 0; row < static_cast<int>(m_rows.size()); ++row) {
            if (m_rows[row] == pluginIndex) emit dataChanged(index(row), index(row));
        }
    }
    void refreshAll() {
        if (!m_rows.empty()) emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
    Qt::ItemFlags flags(const QModelIndex& index) const override {
        return QAbstractListModel::flags(index) | (index.isValid() ? Qt::ItemIsDragEnabled : Qt::NoItemFlags);
    }
    QStringList mimeTypes() const override { return {PluginBrowserDialog::kPluginMimeType}; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override {
        if (indexes.isEmpty()) return nullptr;
        auto* mime = new QMimeData();
        const int pluginIndex = m_rows[indexes.first().row()];
        mime->setData(PluginBrowserDialog::kPluginMimeType, m_plugins[pluginIndex].uri.toUtf8());
        mime->setText(m_plugins[pluginIndex].name);
        return mime;
    }
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

private:
    const std::vector<PluginEntry>& m_plugins;
    std::vector<int> m_rows;
};

// ─── Delegate ────────────────────────────────────────────────────────────────

namespace {
class PluginRowDelegate : public QStyledItemDelegate {
public:
    PluginRowDelegate(const std::vector<PluginEntry>& plugins, const QSet<QString>* favorites,
                      std::function<void(int)> iconReady, std::function<void(int)> starClicked, QObject* parent)
        : QStyledItemDelegate(parent), m_plugins(plugins), m_favorites(favorites),
          m_iconReady(std::move(iconReady)), m_starClicked(std::move(starClicked)) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {300, kRowHeight}; }

    static QRect starRect(const QRect& row) { return QRect(row.right() - 30, row.center().y() - 11, 22, 22); }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const int pluginIndex = index.data(kIndexRole).toInt();
        const PluginEntry& e = m_plugins[pluginIndex];
        const QRect r = option.rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (selected) p->fillRect(r, QColor("#0B4F6C"));
        else if (hovered) p->fillRect(r, QColor("#202026"));

        // Icon: real image when loaded, generated art meanwhile.
        const QRect iconRect(r.left() + 8, r.top() + (r.height() - kIconSize) / 2, kIconSize, kIconSize);
        QPixmap icon;
        const QString path = PluginArt::imagePathFor(e);
        if (!path.isEmpty()) {
            icon = ImageLoader::instance().get(path, QSize(kIconSize * 2, kIconSize * 2),
                                               [cb = m_iconReady, pluginIndex]() { if (cb) cb(pluginIndex); });
        }
        if (icon.isNull()) icon = PluginArt::generated(e, QSize(kIconSize, kIconSize));
        QPainterPath clip;
        clip.addRoundedRect(iconRect, 6, 6);
        p->setClipPath(clip);
        const QPixmap scaled = icon.scaled(iconRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p->drawPixmap(iconRect, scaled, QRect((scaled.width() - iconRect.width()) / 2,
                                              (scaled.height() - iconRect.height()) / 2,
                                              iconRect.width(), iconRect.height()));
        p->setClipping(false);

        // Right side: format chip and star.
        const bool fav = m_favorites && m_favorites->contains(e.uri);
        const QRect star = starRect(r);
        QFont f = option.font;
        f.setPixelSize(15);
        p->setFont(f);
        p->setPen(fav ? QColor("#FFD54F") : QColor(hovered || selected ? "#6E6E7A" : "#3A3A42"));
        p->drawText(star, Qt::AlignCenter, fav ? QString::fromUtf8("★") : QString::fromUtf8("☆"));

        QFont chipFont = option.font;
        chipFont.setPixelSize(9);
        chipFont.setBold(true);
        p->setFont(chipFont);
        const int chipW = QFontMetrics(chipFont).horizontalAdvance(e.format) + 10;
        const QRect chip(star.left() - chipW - 6, r.center().y() - 8, chipW, 16);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#2A2A32"));
        p->drawRoundedRect(chip, 4, 4);
        p->setPen(QColor("#9FA8B8"));
        p->drawText(chip, Qt::AlignCenter, e.format);

        // Text.
        const int textLeft = iconRect.right() + 10;
        const int textWidth = chip.left() - 8 - textLeft;
        QFont nameFont = option.font;
        nameFont.setPixelSize(13);
        nameFont.setBold(true);
        p->setFont(nameFont);
        p->setPen(QColor("#EDEDF2"));
        p->drawText(QRect(textLeft, r.top() + 5, textWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(nameFont).elidedText(e.name, Qt::ElideRight, textWidth));
        QFont subFont = option.font;
        subFont.setPixelSize(11);
        p->setFont(subFont);
        const QString maker = !e.brand.isEmpty() ? e.brand : e.author;
        const QString sub = maker.isEmpty() ? e.category : maker + QString::fromUtf8("  ·  ") + e.category;
        p->setPen(QColor("#8A8A96"));
        p->drawText(QRect(textLeft, r.top() + 24, textWidth, 16), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(subFont).elidedText(sub, Qt::ElideRight, textWidth));
        p->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (starRect(option.rect).contains(me->position().toPoint())) {
                if (m_starClicked) m_starClicked(index.data(kIndexRole).toInt());
                return true;
            }
        }
        return false;
    }

private:
    const std::vector<PluginEntry>& m_plugins;
    const QSet<QString>* m_favorites;
    std::function<void(int)> m_iconReady;
    std::function<void(int)> m_starClicked;
};
} // namespace

// ─── Dialog ──────────────────────────────────────────────────────────────────

PluginBrowserDialog::PluginBrowserDialog(const std::vector<PluginEntry>& plugins, QSet<QString>* favorites,
                                         QStringList* recent, std::function<void()> changed, QWidget* parent,
                                         bool library)
    : QDialog(parent), m_plugins(plugins), m_favorites(favorites), m_recent(recent), m_changed(std::move(changed)),
      m_library(library) {
    setWindowTitle(library ? "Plugins" : "Add Plugin");
    setModal(!library);
    if (library) setWindowFlag(Qt::Tool, true); // stays above the board, doesn't block it
    resize(library ? 980 : 1080, 680);
    setStyleSheet(
        "QDialog { background-color: #141417; }"
        "QLabel { color: #D8D8DE; background: transparent; }"
        "QLineEdit { background-color: #1F1F24; color: #EDEDF2; border: 1px solid #34343C; border-radius: 6px; padding: 7px 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #00B0FF; }"
        "QListWidget#sidebar { background: #17171B; border: none; color: #C8C8D0; font-size: 12px; outline: none; }"
        "QListWidget#sidebar::item { padding: 6px 10px; border-radius: 5px; }"
        "QListWidget#sidebar::item:selected { background: #0B4F6C; color: white; }"
        "QListWidget#sidebar::item:hover:!selected { background: #202026; }"
        "QListView#results { background: #17171B; border: 1px solid #26262C; border-radius: 6px; outline: none; }"
        "QToolButton.formatChip { background: #1F1F24; color: #B8B8C4; border: 1px solid #34343C; border-radius: 12px; padding: 3px 12px; font-size: 11px; }"
        "QToolButton.formatChip:checked { background: #0B4F6C; color: white; border-color: #00B0FF; }"
        "QPushButton { background: #2A2A30; color: #E0E0E0; border: 1px solid #3A3A42; border-radius: 5px; padding: 6px 14px; }"
        "QPushButton:hover { background: #34343C; }"
        "QPushButton#addButton { background: #00897B; border: none; font-weight: bold; color: white; }"
        "QPushButton#addButton:hover { background: #009688; }"
        "QPushButton#addButton:disabled { background: #2A2A30; color: #666; }");

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // Sidebar
    m_sidebar = new QListWidget(this);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFixedWidth(200);
    root->addWidget(m_sidebar);

    // Center: search, chips, list
    auto* center = new QVBoxLayout();
    center->setSpacing(8);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search plugins by name, maker, type or tag…");
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    center->addWidget(m_search);

    auto* chipRow = new QHBoxLayout();
    chipRow->setSpacing(6);
    m_formatGroup = new QButtonGroup(this);
    m_formatGroup->setExclusive(true);
    for (const QString& format : {QString("All"), QString("LV2"), QString("CLAP"), QString("VST3")}) {
        auto* chip = new QToolButton(this);
        chip->setText(format);
        chip->setCheckable(true);
        chip->setProperty("class", "formatChip");
        chip->setCursor(Qt::PointingHandCursor);
        chip->setChecked(format == "All");
        m_formatGroup->addButton(chip);
        chipRow->addWidget(chip);
        connect(chip, &QToolButton::clicked, this, [this, format]() {
            m_filter.format = format == "All" ? QString() : format;
            refresh();
        });
    }
    chipRow->addStretch();
    m_count = new QLabel(this);
    m_count->setStyleSheet("color: #8A8A96; font-size: 11px;");
    chipRow->addWidget(m_count);
    center->addLayout(chipRow);

    m_model = new PluginListModel(m_plugins, this);
    m_list = new QListView(this);
    m_list->setObjectName("results");
    m_list->setModel(m_model);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Rows can be dragged onto the signal chain.
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    m_list->setItemDelegate(new PluginRowDelegate(
        m_plugins, m_favorites,
        [this](int pluginIndex) { m_model->pluginChanged(pluginIndex); if (pluginIndex == m_detailsIndex) showDetails(pluginIndex); },
        [this](int pluginIndex) { toggleFavorite(pluginIndex); }, m_list));
    center->addWidget(m_list, 1);
    auto* hint = new QLabel(library
        ? "Drag a plugin onto the signal chain (between blocks or onto a free slot) · double-click or Enter adds it at the end · ☆ favourite"
        : "Enter or double-click adds the plugin · you can also drag it onto the chain · ↑ ↓ move while typing · ☆ favourite", this);
    hint->setStyleSheet("color: #6E6E7A; font-size: 11px;");
    center->addWidget(hint);
    root->addLayout(center, 5);

    // Info panel
    auto* infoScroll = new QScrollArea(this);
    infoScroll->setWidgetResizable(true);
    infoScroll->setFrameShape(QFrame::NoFrame);
    infoScroll->setFixedWidth(340);
    infoScroll->setStyleSheet("QScrollArea { background: transparent; }");
    auto* info = new QWidget();
    info->setStyleSheet("background: transparent;");
    auto* infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(0, 0, 4, 0);
    infoLayout->setSpacing(8);
    m_preview = new QLabel(info);
    m_preview->setFixedSize(320, 190);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("background: #1B1B20; border-radius: 10px;");
    infoLayout->addWidget(m_preview);
    m_name = new QLabel(info);
    m_name->setWordWrap(true);
    m_name->setStyleSheet("font-size: 17px; font-weight: bold; color: #F2F2F5;");
    infoLayout->addWidget(m_name);
    m_byline = new QLabel(info);
    m_byline->setStyleSheet("color: #9FA8B8; font-size: 12px;");
    infoLayout->addWidget(m_byline);
    m_chips = new QLabel(info);
    m_chips->setTextFormat(Qt::RichText);
    infoLayout->addWidget(m_chips);
    m_description = new QLabel(info);
    m_description->setWordWrap(true);
    m_description->setStyleSheet("color: #C8C8D0; font-size: 12px;");
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_description);
    m_moreButton = new QToolButton(info);
    m_moreButton->setText("Show more");
    m_moreButton->setCheckable(true);
    m_moreButton->setStyleSheet("QToolButton { color: #00B0FF; border: none; background: transparent; font-size: 11px; }");
    connect(m_moreButton, &QToolButton::toggled, this, [this]() { showDetails(m_detailsIndex); });
    infoLayout->addWidget(m_moreButton, 0, Qt::AlignLeft);
    m_facts = new QLabel(info);
    m_facts->setTextFormat(Qt::RichText);
    m_facts->setStyleSheet("color: #B8B8C4; font-size: 12px;");
    infoLayout->addWidget(m_facts);
    m_tags = new QLabel(info);
    m_tags->setWordWrap(true);
    m_tags->setTextFormat(Qt::RichText);
    infoLayout->addWidget(m_tags);
    m_detailsToggle = new QToolButton(info);
    m_detailsToggle->setText(QString::fromUtf8("▸ Technical details"));
    m_detailsToggle->setCheckable(true);
    m_detailsToggle->setStyleSheet("QToolButton { color: #8A8A96; border: none; background: transparent; font-size: 11px; }");
    infoLayout->addWidget(m_detailsToggle, 0, Qt::AlignLeft);
    m_technical = new QLabel(info);
    m_technical->setWordWrap(true);
    m_technical->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_technical->setStyleSheet("color: #7E7E8A; font-size: 11px;");
    m_technical->setVisible(false);
    connect(m_detailsToggle, &QToolButton::toggled, this, [this](bool on) {
        m_technical->setVisible(on);
        m_detailsToggle->setText(on ? QString::fromUtf8("▾ Technical details") : QString::fromUtf8("▸ Technical details"));
    });
    infoLayout->addWidget(m_technical);
    infoLayout->addStretch();
    auto* buttons = new QHBoxLayout();
    m_favoriteButton = new QPushButton(info);
    connect(m_favoriteButton, &QPushButton::clicked, this, [this]() { toggleFavorite(m_detailsIndex); });
    m_addButton = new QPushButton(library ? "Add at End of Chain" : "Add to Board", info);
    m_addButton->setObjectName("addButton");
    m_addButton->setDefault(true);
    // Set on the button itself so the app-wide button style can't override it.
    m_addButton->setStyleSheet(
        "QPushButton { background: #00897B; border: none; font-weight: bold; color: white; border-radius: 5px; padding: 7px 16px; }"
        "QPushButton:hover { background: #009688; }"
        "QPushButton:disabled { background: #2A2A30; color: #666; }");
    m_favoriteButton->setStyleSheet(
        "QPushButton { background: #2A2A30; color: #E0E0E0; border: 1px solid #3A3A42; border-radius: 5px; padding: 7px 12px; }"
        "QPushButton:hover { background: #34343C; }");
    connect(m_addButton, &QPushButton::clicked, this, &PluginBrowserDialog::accept);
    buttons->addWidget(m_favoriteButton);
    buttons->addStretch();
    buttons->addWidget(m_addButton);
    infoLayout->addLayout(buttons);
    infoScroll->setWidget(info);
    root->addWidget(infoScroll);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filter.text = text;
        refresh();
    });
    connect(m_search, &QLineEdit::returnPressed, this, &PluginBrowserDialog::accept);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() {
        showDetails(currentPluginIndex());
    });
    connect(m_list, &QListView::doubleClicked, this, &PluginBrowserDialog::accept);
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        const QListWidgetItem* item = m_sidebar->item(row);
        m_filter.view = static_cast<PluginFilter::View>(item->data(Qt::UserRole).toInt());
        m_filter.category = item->data(Qt::UserRole + 1).toString();
        refresh();
    });

    rebuildSidebar();
    m_sidebar->setCurrentRow(0); // All plugins
    refresh();
    m_search->setFocus();
}

void PluginBrowserDialog::rebuildSidebar() {
    const QSignalBlocker blocker(m_sidebar);
    const int current = m_sidebar->currentRow();
    m_sidebar->clear();
    std::map<QString, int> categories;
    int favoriteCount = 0;
    int recentCount = 0;
    for (const auto& p : m_plugins) {
        categories[p.category]++;
        if (m_favorites && m_favorites->contains(p.uri)) ++favoriteCount;
        if (m_recent && m_recent->contains(p.uri)) ++recentCount;
    }
    auto add = [this](const QString& text, PluginFilter::View view, const QString& category = QString()) {
        auto* item = new QListWidgetItem(text, m_sidebar);
        item->setData(Qt::UserRole, static_cast<int>(view));
        item->setData(Qt::UserRole + 1, category);
    };
    add(QString("All plugins  (%1)").arg(m_plugins.size()), PluginFilter::View::All);
    add(QString::fromUtf8("★ Favorites  (%1)").arg(favoriteCount), PluginFilter::View::Favorites);
    add(QString("Recently used  (%1)").arg(recentCount), PluginFilter::View::Recent);
    auto* sep = new QListWidgetItem(m_sidebar);
    sep->setFlags(Qt::NoItemFlags);
    sep->setSizeHint(QSize(10, 8));
    for (const auto& [category, count] : categories) {
        add(QString("%1  (%2)").arg(category).arg(count), PluginFilter::View::Category, category);
    }
    m_sidebar->setCurrentRow(std::max(0, current));
}

void PluginBrowserDialog::refresh() {
    const int keep = currentPluginIndex();
    auto rows = filterPlugins(m_plugins, m_filter, m_favorites ? *m_favorites : QSet<QString>(),
                              m_recent ? *m_recent : QStringList());
    const int count = static_cast<int>(rows.size());
    int keepRow = 0;
    for (int i = 0; i < count; ++i) if (rows[i] == keep) { keepRow = i; break; }
    m_model->setRows(std::move(rows));
    m_count->setText(count == 1 ? QString("1 plugin") : QString("%1 plugins").arg(count));
    if (count > 0) {
        m_list->setCurrentIndex(m_model->index(keepRow));
        m_list->scrollTo(m_model->index(keepRow));
    }
    showDetails(currentPluginIndex());
}

int PluginBrowserDialog::currentPluginIndex() const {
    const QModelIndex index = m_list ? m_list->currentIndex() : QModelIndex();
    return index.isValid() ? index.data(kIndexRole).toInt() : -1;
}

void PluginBrowserDialog::showDetails(int pluginIndex) {
    const bool changed = pluginIndex != m_detailsIndex;
    m_detailsIndex = pluginIndex;
    const bool valid = pluginIndex >= 0 && pluginIndex < static_cast<int>(m_plugins.size());
    m_addButton->setEnabled(valid);
    m_favoriteButton->setEnabled(valid);
    if (!valid) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText("No plugin selected");
        for (QLabel* l : {m_name, m_byline, m_chips, m_description, m_facts, m_tags, m_technical}) l->clear();
        m_moreButton->hide();
        return;
    }
    if (changed && m_moreButton->isChecked()) {
        const QSignalBlocker blocker(m_moreButton);
        m_moreButton->setChecked(false);
    }
    const PluginEntry& e = m_plugins[pluginIndex];

    const QSize previewSize = m_preview->size();
    QPixmap image;
    const QString path = PluginArt::imagePathFor(e);
    if (!path.isEmpty()) {
        image = ImageLoader::instance().get(path, previewSize, [this, pluginIndex]() {
            if (m_detailsIndex == pluginIndex) showDetails(pluginIndex);
        });
    }
    if (image.isNull()) image = PluginArt::generated(e, previewSize);
    m_preview->setPixmap(image);

    m_name->setText(e.name);
    QStringList by;
    if (!e.brand.isEmpty()) by << e.brand;
    if (!e.author.isEmpty() && e.author != e.brand) by << e.author;
    m_byline->setText(by.isEmpty() ? QString("Unknown maker") : "by " + by.join(" · "));

    auto chip = [](const QString& text, const QString& color) {
        return QString("<span style='background:%2; color:#101012; font-weight:bold; font-size:10px;'>&nbsp;%1&nbsp;</span>")
            .arg(text.toHtmlEscaped(), color);
    };
    QStringList chips{chip(e.format, "#9FA8B8"), chip(e.category, PluginArt::categoryColor(e.category).name())};
    if (!e.version.isEmpty()) chips << chip("v" + e.version, "#6E6E7A");
    if (!e.license.isEmpty()) chips << chip(e.license, "#6E6E7A");
    m_chips->setText(chips.join("&nbsp;"));

    QString description = e.description.trimmed();
    if (description.isEmpty()) description = "No description provided by the plugin.";
    const bool longText = description.size() > 260;
    m_moreButton->setVisible(longText);
    m_moreButton->setText(m_moreButton->isChecked() ? "Show less" : "Show more");
    m_description->setText(longText && !m_moreButton->isChecked() ? description.left(250).trimmed() + QString::fromUtf8("…") : description);

    m_facts->setText(QString("<b>Audio</b> %1 in · %2 out &nbsp;&nbsp; <b>Controls</b> %3<br><b>Own GUI</b> %4")
        .arg(e.audioInputs).arg(e.audioOutputs)
        .arg(e.controlPorts > 0 ? QString::number(e.controlPorts) : QString("—"))
        .arg(e.hasNativeGUI ? "yes" : "no (RigRoom's controls)"));

    QStringList tags;
    for (const QString& t : e.features) {
        tags << QString("<span style='color:#9FA8B8; background:#24242A;'>&nbsp;%1&nbsp;</span>").arg(t.toHtmlEscaped());
    }
    m_tags->setText(tags.join(" "));
    m_tags->setVisible(!tags.isEmpty());
    m_technical->setText(QString("URI: %1\nPath: %2").arg(e.uri, e.path));

    const bool fav = m_favorites && m_favorites->contains(e.uri);
    m_favoriteButton->setText(fav ? QString::fromUtf8("★ Favorite") : QString::fromUtf8("☆ Add to Favorites"));
}

void PluginBrowserDialog::toggleFavorite(int pluginIndex) {
    if (!m_favorites || pluginIndex < 0 || pluginIndex >= static_cast<int>(m_plugins.size())) return;
    const QString& uri = m_plugins[pluginIndex].uri;
    if (m_favorites->contains(uri)) m_favorites->remove(uri);
    else m_favorites->insert(uri);
    if (m_changed) m_changed();
    rebuildSidebar();
    if (m_filter.view == PluginFilter::View::Favorites) refresh();
    else m_model->pluginChanged(pluginIndex);
    if (pluginIndex == m_detailsIndex) showDetails(pluginIndex);
}

void PluginBrowserDialog::accept() {
    const int pluginIndex = currentPluginIndex();
    if (pluginIndex < 0) return;
    m_selectedUri = m_plugins[pluginIndex].uri;
    if (m_recent) {
        m_recent->removeAll(m_selectedUri);
        m_recent->prepend(m_selectedUri);
        while (m_recent->size() > 20) m_recent->removeLast();
        if (m_changed) m_changed();
    }
    if (m_library) {
        emit pluginChosen(m_selectedUri); // stay open for the next one
        return;
    }
    QDialog::accept();
}

bool PluginBrowserDialog::eventFilter(QObject* watched, QEvent* event) {
    // Arrow keys move through results while the search box keeps focus.
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int k = key->key();
        if (k == Qt::Key_Down || k == Qt::Key_Up || k == Qt::Key_PageDown || k == Qt::Key_PageUp) {
            QApplication::sendEvent(m_list, event);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
