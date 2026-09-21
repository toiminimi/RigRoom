#include "Tone3000Dialog.h"
#include "AddToLibraryDialog.h"
#include "BrowserStyle.h"
#include "CaptureLibrary.h"
#include "CredentialStore.h"
#include "Tone3000Api.h"
#include "Tone3000ImageLoader.h"
#include "Tone3000Library.h"
#include "Tone3000ResultModel.h"

#include <QActionGroup>
#include <QComboBox>
#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMimeData>
#include <QDrag>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <cmath>
#include <iostream>

using Tone3000::Format;
using Tone3000::Query;
using Tone3000::ToneItem;

namespace {
QString configDir() {
    return QDir::homePath() + "/.config/RigRoom";
}

void migrateLegacyTone3000Data() {
    const QString legacyConfigDir = QDir::homePath() + "/.config/PedalBoard";
    QDir().mkpath(configDir());
    for (const QString& filename : {"favorites.json", "browser_settings.json"}) {
        const QString source = legacyConfigDir + "/" + filename;
        const QString destination = configDir() + "/" + filename;
        if (!QFile::exists(destination) && QFile::exists(source)) QFile::copy(source, destination);
    }
}

// A 2 px line above the list that moves while anything is loading. It keeps
// its height when idle so nothing jumps.
class ActivityBar : public QWidget {
public:
    using QWidget::QWidget;
    void setActive(bool active) {
        if (m_active == active) return;
        m_active = active;
        update();
    }
    bool isActive() const { return m_active; }
    void setPhase(qreal phase) {
        m_phase = phase;
        if (m_active) update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_active) return;
        QPainter p(this);
        const int w = width();
        const int segment = std::max(60, w / 4);
        const int x = static_cast<int>((w + segment) * m_phase) - segment;
        QLinearGradient gradient(x, 0, x + segment, 0);
        gradient.setColorAt(0.0, QColor(0, 176, 255, 0));
        gradient.setColorAt(0.5, QColor(0, 176, 255, 230));
        gradient.setColorAt(1.0, QColor(0, 176, 255, 0));
        p.fillRect(QRect(x, 0, segment, height()), gradient);
    }

private:
    bool m_active = false;
    qreal m_phase = 0;
};

// The result list, with a drag of its own for filing library rows into
// folders. It does not use QDrag: the pointer, the small label that follows
// it and the folder highlight are all ours, so they behave the same on X11
// and Wayland. The dialog supplies what happens as the pointer moves.
class ResultListView : public QListView {
public:
    using QListView::QListView;
    std::function<bool()> canDrag;                        // anything draggable selected?
    std::function<void(const QPoint& global)> dragMoved;  // called for the start too
    std::function<void(const QPoint& global, bool drop)> dragEnded;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        m_pressPos = event->button() == Qt::LeftButton ? event->position().toPoint() : QPoint(-1, -1);
        QListView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging) {
            // The button came up somewhere we did not hear about: end there.
            if (!(event->buttons() & Qt::LeftButton)) finish(event->globalPosition().toPoint(), true);
            else if (dragMoved) dragMoved(event->globalPosition().toPoint());
            return;
        }
        if ((event->buttons() & Qt::LeftButton) && m_pressPos.x() >= 0
            && (event->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()
            && canDrag && canDrag()) {
            m_dragging = true; // the viewport keeps getting moves while the button is down
            if (dragMoved) dragMoved(event->globalPosition().toPoint());
            return;
        }
        QListView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_dragging) {
            finish(event->globalPosition().toPoint(), true);
            return;
        }
        QListView::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (m_dragging && event->key() == Qt::Key_Escape) {
            finish(QCursor::pos(), false);
            return;
        }
        QListView::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        if (m_dragging) finish(QCursor::pos(), false); // never leave the pointer stuck
        QListView::focusOutEvent(event);
    }
    // The built-in drag would draw the whole rows; ours runs instead.
    void startDrag(Qt::DropActions) override {}

private:
    void finish(const QPoint& global, bool drop) {
        m_dragging = false;
        m_pressPos = QPoint(-1, -1);
        setState(NoState);
        if (dragEnded) dragEnded(global, drop);
    }
    bool m_dragging = false;
    QPoint m_pressPos{-1, -1};
};

QString modelDetails(const QJsonObject& model) {
    QStringList details;
    const QString size = model.value("size").toString();
    if (!size.isEmpty()) details << size.left(1).toUpper() + size.mid(1);
    const QString architecture = model.value("architecture_version").toString();
    if (!architecture.isEmpty()) details << "A" + architecture;
    return details.join(QString::fromUtf8(" · "));
}

QString chipsHtml(const QStringList& values, const QString& background, const QString& color, bool links = false) {
    QStringList chips;
    for (const QString& value : values) {
        if (value.trimmed().isEmpty()) continue;
        if (chips.size() == 12) {
            chips << QString("<span style='color:#6E6E7A;'>+%1</span>").arg(values.size() - 12);
            break;
        }
        // Non-breaking hyphens and spaces keep each chip on one line.
        QString text = value.trimmed().toHtmlEscaped();
        text.replace('-', QChar(0x2011)).replace(' ', "&nbsp;");
        const QString chip = QString("<span style='color:%2; background:%3;'>&nbsp;%1&nbsp;</span>").arg(text, color, background);
        chips << (links ? QString("<a href='tag:%1' style='text-decoration:none;'>%2</a>")
                              .arg(QString(QUrl::toPercentEncoding(value.trimmed())), chip)
                        : chip);
    }
    return chips.join(" ");
}
} // namespace

// ─── Construction ────────────────────────────────────────────────────────────

Tone3000Dialog::Tone3000Dialog(AudioNode* node, AudioEngine* engine, QWidget* parent, Mode mode,
                               const std::string& irPropertyUri)
    : QDialog(parent), m_node(node), m_engine(engine), m_mode(mode),
      m_format(mode == Mode::Ir ? Format::Ir : Format::Nam), m_irPropertyUri(irPropertyUri) {
    migrateLegacyTone3000Data();
    m_query = Query::defaults(m_format);
    if (m_mode == Mode::Ir) {
        // Remember the IR that was loaded so Cancel can put it back after a preview.
        if (node) {
            for (const auto& fp : node->getFileProperties()) {
                if (fp.uri == irPropertyUri) m_originalModelPath = fp.fileValue;
            }
        }
    } else {
        m_originalModelPath = node ? node->getModelFilePath() : "";
    }

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(300);
    m_modelsTimer = new QTimer(this);
    m_modelsTimer->setSingleShot(true);
    m_modelsTimer->setInterval(150); // arrowing through the list does not fetch every row
    m_animation = new QTimer(this);
    m_animation->setInterval(33);

    buildUi();
    loadSettings();

    connect(m_searchTimer, &QTimer::timeout, this, &Tone3000Dialog::onQueryEdited);
    connect(m_modelsTimer, &QTimer::timeout, this, &Tone3000Dialog::requestModels);
    connect(m_animation, &QTimer::timeout, this, [this]() {
        m_phase = std::fmod(m_phase + 0.025, 1.0);
        static_cast<ActivityBar*>(m_activityBar)->setPhase(m_phase);
        m_delegate->setPhase(m_phase);
        if (m_model->isLoading() || m_model->status() == Tone3000ResultModel::Status::Waiting) {
            m_list->viewport()->update();
        }
    });

    Tone3000ImageLoader* images = Tone3000ImageLoader::instance();
    connect(images, &Tone3000ImageLoader::ready, this, [this]() {
        // Many images arrive together; repaint once for all of them.
        if (m_viewportUpdateQueued) return;
        m_viewportUpdateQueued = true;
        QTimer::singleShot(30, this, [this]() {
            m_viewportUpdateQueued = false;
            m_list->viewport()->update();
        });
    });

    Tone3000Library& library = Tone3000Library::instance();
    connect(&library, &Tone3000Library::favoritesChanged, this, [this]() {
        m_list->viewport()->update();
        // Account favorites arrive after the list opened; unfavoriting keeps
        // the row until the view is opened again, so a misclick can be undone.
        if (m_view == View::Favorites && m_search->text().trimmed().isEmpty()
            && Tone3000Library::instance().favorites(m_format).size() > m_model->itemCount()) {
            runView();
        }
        rebuildSidebar();
        showDetails();
    });
    connect(&library, &Tone3000Library::savedSearchesChanged, this, &Tone3000Dialog::rebuildSidebar);

    CaptureLibrary& captures = CaptureLibrary::instance();
    connect(&captures, &CaptureLibrary::changed, this, [this]() {
        m_list->viewport()->update(); // "on disk" badges
        // A first run with nothing on disk opens on TONE3000.
        if (!m_tabFromSettings && m_tab == Tab::Library && CaptureLibrary::instance().files(m_format).isEmpty() && CaptureLibrary::instance().importCandidates(m_format).isEmpty()) {
            m_tabFromSettings = true;
            setTab(Tab::Online);
            return;
        }
        m_tabFromSettings = true;
        if (m_tab != Tab::Library) return;
        rebuildSidebar();
        if (isLibraryView()) runView();
        showDetails();
    });
    // Details for captures downloaded before tone.json existed come once the scan is done.
    connect(&captures, &CaptureLibrary::changed, &captures, &CaptureLibrary::enrich, Qt::SingleShotConnection);
    captures.rescan();
    Tone3000Api::instance()->refreshFavorites();
    if (Tone3000Api::hasKey()) {
        // TONE3000 takes seconds to list creators; ask early so the Creators view opens at once.
        Query creators;
        creators.source = Query::Source::Creators;
        QUrl url(creators.endpoint());
        url.setQuery(creators.toUrlQuery(1));
        Tone3000Api::instance()->getJson(url, Tone3000Api::instance(), [](const Tone3000Api::Response&) {}, {},
                                         creators.cacheKey(1));
    }

    m_libraryTab->setChecked(m_tab == Tab::Library);
    m_onlineTab->setChecked(m_tab == Tab::Online);
    m_list->setSelectionMode(m_tab == Tab::Library ? QAbstractItemView::ExtendedSelection
                                                   : QAbstractItemView::SingleSelection);
    rebuildSidebar();
    selectSidebarView();
    updateChips();
    updateLibraryChips();
    QMetaObject::invokeMethod(this, [this]() { runView(); }, Qt::QueuedConnection);
    m_search->setFocus();
}

Tone3000Dialog::~Tone3000Dialog() = default;

void Tone3000Dialog::buildUi() {
    setWindowTitle(m_mode == Mode::Ir ? "TONE3000 Impulse Responses" : "TONE3000 NAM Captures");
    setStyleSheet(BrowserStyle::kStyleSheet);
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(std::min(1240, available.width() - 80), std::min(780, available.height() - 80));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(10);
    outer->addWidget(buildApiKeyBanner());

    auto* root = new QHBoxLayout();
    root->setSpacing(12);
    auto* left = new QVBoxLayout();
    left->setSpacing(8);
    left->addWidget(buildTabs());
    left->addWidget(buildSidebar(), 1);
    root->addLayout(left);
    root->addWidget(buildCenter(), 1);
    root->addWidget(buildInfoPanel());
    outer->addLayout(root, 1);

    for (QComboBox* combo : m_infoScroll->findChildren<QComboBox*>()) {
        combo->setFocusPolicy(Qt::StrongFocus);
        combo->installEventFilter(this);
    }
    auto* focusSearch = new QShortcut(QKeySequence::Find, this);
    connect(focusSearch, &QShortcut::activated, this, [this]() {
        m_search->setFocus();
        m_search->selectAll();
    });
    auto* favorite = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(favorite, &QShortcut::activated, this, [this]() {
        if (m_selected.id != 0 && selectedKey().isEmpty()) toggleFavorite(m_selected);
    });
}

QWidget* Tone3000Dialog::buildApiKeyBanner() {
    m_apiKeyBanner = new QWidget(this);
    m_apiKeyBanner->setObjectName("apiKeyBanner");
    m_apiKeyBanner->setStyleSheet(
        "QWidget#apiKeyBanner { background-color: #13232C; border: 1px solid #0B4F6C; border-radius: 6px; }"
        "QLabel { color: #D8D8DE; font-size: 12px; }");
    auto* layout = new QHBoxLayout(m_apiKeyBanner);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);
    auto* text = new QLabel(
        "<b>TONE3000 secret key needed.</b> Create one at "
        "<a href='https://tone3000.com/settings' style='color:#00B0FF;'>tone3000.com/settings</a> "
        "→ API &amp; Developer Keys (it starts with <code>t3k_cs_</code>).",
        m_apiKeyBanner);
    text->setOpenExternalLinks(true);
    text->setWordWrap(true);
    auto* keyInput = new QLineEdit(m_apiKeyBanner);
    keyInput->setPlaceholderText("Paste t3k_cs_… secret key");
    keyInput->setEchoMode(QLineEdit::Password);
    keyInput->setFixedWidth(240);
    auto* save = new QPushButton("Save Key", m_apiKeyBanner);
    save->setStyleSheet(BrowserStyle::kPrimaryButton);
    layout->addWidget(text, 1);
    layout->addWidget(keyInput);
    layout->addWidget(save);
    m_apiKeyBanner->setVisible(!Tone3000Api::hasKey());
    auto saveKey = [this, keyInput]() {
        const QString key = keyInput->text().trimmed();
        if (key.isEmpty()) return;
        QString error;
        CredentialStore::setTone3000ApiKey(key, &error);
        keyInput->clear();
        m_apiKeyBanner->hide();
        Tone3000Api::instance()->clearCache();
        Tone3000Api::instance()->refreshFavorites(true);
        runView();
    };
    connect(save, &QPushButton::clicked, this, saveKey);
    connect(keyInput, &QLineEdit::returnPressed, this, saveKey);
    return m_apiKeyBanner;
}

QWidget* Tone3000Dialog::buildSidebar() {
    m_sidebar = new QListWidget(this);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFixedWidth(200);
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebar, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (m_tab == Tab::Library) onLibrarySidebarClicked(item);
    });
    connect(m_sidebar, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (m_tab == Tab::Library || !item || !(item->flags() & Qt::ItemIsSelectable)) return;
        setView(static_cast<View>(item->data(Qt::UserRole).toInt()), item->data(Qt::UserRole + 1).toInt());
    });
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_sidebar->itemAt(pos);
        if (item && m_tab == Tab::Library && item->data(Qt::UserRole).toInt() == static_cast<int>(View::LibraryFolder)) {
            const QString folder = item->data(Qt::UserRole + 1).toString();
            if (folder == CaptureLibrary::kDefaultFolder) return;
            QMenu menu(this);
            QAction* rename = menu.addAction(QString::fromUtf8("Rename…"));
            QAction* remove = menu.addAction("Delete folder (captures go to Imported)");
            QAction* chosen = menu.exec(m_sidebar->viewport()->mapToGlobal(pos));
            if (chosen == rename) {
                bool ok = false;
                const QString name = QInputDialog::getText(this, "Rename Folder", "Folder name:", QLineEdit::Normal, folder, &ok);
                if (ok && !name.trimmed().isEmpty()) {
                    if (m_libraryFolderName == folder) m_libraryFolderName = name.trimmed();
                    CaptureLibrary::instance().renameFolder(folder, name);
                }
            } else if (chosen == remove) {
                if (m_libraryFolderName == folder) m_view = m_libraryView = View::LibraryAll;
                CaptureLibrary::instance().removeFolder(folder);
            }
            return;
        }
        if (!item || m_tab == Tab::Library || static_cast<View>(item->data(Qt::UserRole).toInt()) != View::Saved) return;
        const int index = item->data(Qt::UserRole + 1).toInt();
        QMenu menu(this);
        QAction* remove = menu.addAction("Remove saved search");
        if (menu.exec(m_sidebar->viewport()->mapToGlobal(pos)) == remove) {
            if (m_view == View::Saved && m_savedIndex == index) m_view = View::Catalog;
            Tone3000Library::instance().removeSavedSearch(m_format, index);
        }
    });
    return m_sidebar;
}

void Tone3000Dialog::rebuildSidebar() {
    if (m_tab == Tab::Library) {
        rebuildLibrarySidebar();
        return;
    }
    m_dropTarget = nullptr;
    const QSignalBlocker blocker(m_sidebar);
    m_sidebar->clear();
    auto header = [this](const QString& text) {
        auto* item = new QListWidgetItem(text.toUpper(), m_sidebar);
        item->setFlags(Qt::NoItemFlags);
        QFont font = m_sidebar->font();
        font.setPixelSize(10);
        font.setBold(true);
        item->setFont(font);
        item->setForeground(QColor("#6E6E7A"));
        item->setSizeHint(QSize(10, m_sidebar->count() == 1 ? 22 : 30));
    };
    auto add = [this](const QString& text, View view, int saved = -1) {
        auto* item = new QListWidgetItem(text, m_sidebar);
        item->setData(Qt::UserRole, static_cast<int>(view));
        item->setData(Qt::UserRole + 1, saved);
        return item;
    };
    const Tone3000Library& library = Tone3000Library::instance();
    header("TONE3000");
    add("Trending", View::Trending);
    add("Newest", View::Newest);
    add("Most downloaded", View::MostDownloaded);
    add("Creators", View::Creators);
    QListWidgetItem* favorites = add(QString::fromUtf8("★ TONE3000 favorites  (%1)").arg(library.favorites(m_format).size()),
                                     View::Favorites);
    favorites->setToolTip("Tones saved on your TONE3000 account with the star");
    const auto saved = library.savedSearches(m_format);
    if (!saved.isEmpty()) {
        header("Saved searches");
        for (int i = 0; i < saved.size(); ++i) {
            QListWidgetItem* item = add(saved[i].name, View::Saved, i);
            item->setToolTip(saved[i].query.describe() + "\nRight-click to remove");
        }
    }
    selectSidebarView();
}

void Tone3000Dialog::selectSidebarView() {
    if (m_tab == Tab::Library) {
        rebuildLibrarySidebar();
        return;
    }
    const QSignalBlocker blocker(m_sidebar);
    for (int row = 0; row < m_sidebar->count(); ++row) {
        QListWidgetItem* item = m_sidebar->item(row);
        if (!(item->flags() & Qt::ItemIsSelectable)) continue;
        const View view = static_cast<View>(item->data(Qt::UserRole).toInt());
        if (view == m_view && (view != View::Saved || item->data(Qt::UserRole + 1).toInt() == m_savedIndex)) {
            m_sidebar->setCurrentRow(row);
            return;
        }
    }
    m_sidebar->setCurrentRow(-1);
    m_sidebar->clearSelection();
}

QWidget* Tone3000Dialog::buildCenter() {
    auto* center = new QWidget(this);
    auto* layout = new QVBoxLayout(center);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    m_search = new QLineEdit(center);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textEdited, this, [this]() { m_searchTimer->start(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty() && !m_search->hasFocus()) m_searchTimer->start(); // the clear button
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchTimer->isActive()) {
            onQueryEdited();
            return;
        }
        const int variant = currentVariant();
        if (variant >= 0) loadVariant(variant);
    });
    searchRow->addWidget(m_search, 1);
    m_saveSearchButton = new QPushButton("Save search", center);
    m_saveSearchButton->setToolTip("Keep this search and its filters in the sidebar");
    connect(m_saveSearchButton, &QPushButton::clicked, this, &Tone3000Dialog::saveCurrentSearch);
    searchRow->addWidget(m_saveSearchButton);
    layout->addLayout(searchRow);

    m_chipRow = new QWidget(center);
    auto* chips = new QHBoxLayout(m_chipRow);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    addChoiceChip(chips, "Sort", {{"Trending", "trending"}, {"Best match", "best-match"},
                                  {"Most downloaded", "downloads"}, {"Newest", "newest"}, {"Oldest", "oldest"}},
                  &Query::sort, false);
    if (m_format == Format::Nam) {
        addChoiceChip(chips, "Gear type", {{"Amp + Cab", "amp-cab"}, {"Amp", "amp"}, {"Pedal", "pedal"},
                                           {"Outboard", "outboard"}},
                      &Query::gear, false, true);
        addChoiceChip(chips, "Tags", {{"Clean", "clean"}, {"Crunch", "crunch"}, {"Overdrive", "overdrive"},
                                      {"High gain", "high-gain"}, {"Metal", "metal"}, {"Rock", "rock"},
                                      {"Blues", "blues"}, {"Vintage", "vintage"}, {"Boost", "boost"}, {"Fuzz", "fuzz"},
                                      {"Jazz", "jazz"}, {"Djent", "djent"}, {"Bass", "bass"}},
                      &Query::tags, false, true);
    } else {
        addChoiceChip(chips, "Type", {{"Cab", "cab"}, {"Pedal", "pedal"}, {"Outboard", "outboard"},
                                      {"Space (rooms, reverbs)", "space"}},
                      &Query::gear, false, true);
        addChoiceChip(chips, "Tags", {{"Metal", "metal"}, {"Rock", "rock"}, {"Clean", "clean"}, {"Bass", "bass"},
                                      {"Vintage", "vintage"}, {"Celestion", "celestion"}, {"4x12", "4x12"},
                                      {"2x12", "2x12"}, {"1x12", "1x12"}, {"Delay", "delay"}, {"Spring", "spring"},
                                      {"Acoustic", "acoustic"}},
                      &Query::tags, false, true);
    }
    addChoiceChip(chips, "NAM architecture", {{"A2", "2"}, {"A1", "1"}, {"Custom", "custom"}, {"All architectures", ""}},
                  &Query::architecture, true);
    addChoiceChip(chips, "Model size", {{"All sizes", ""}, {"Standard", "standard"}, {"Lite", "lite"}, {"Feather", "feather"},
                                  {"Nano", "nano"}},
                  &Query::size, true);
    m_calibratedChip = new QToolButton(m_chipRow);
    m_calibratedChip->setText("Calibrated");
    m_calibratedChip->setCheckable(true);
    m_calibratedChip->setProperty("class", "formatChip");
    m_calibratedChip->setCursor(Qt::PointingHandCursor);
    m_calibratedChip->setToolTip("Only captures with calibrated input and output levels");
    connect(m_calibratedChip, &QToolButton::toggled, this, [this](bool on) {
        if (m_query.calibrated == on) return;
        m_query.calibrated = on;
        onQueryEdited();
    });
    chips->addWidget(m_calibratedChip);
    m_resetChip = new QToolButton(m_chipRow);
    m_resetChip->setText(QString::fromUtf8("Reset filters"));
    m_resetChip->setCursor(Qt::PointingHandCursor);
    m_resetChip->setStyleSheet("QToolButton { color: #00B0FF; border: none; background: transparent; font-size: 11px; }");
    connect(m_resetChip, &QToolButton::clicked, this, [this]() {
        m_query.resetFilters();
        onQueryEdited();
    });
    chips->addWidget(m_resetChip);
    chips->addStretch();
    m_count = new QLabel(m_chipRow);
    m_count->setStyleSheet("color: #8A8A96; font-size: 11px;");
    chips->addWidget(m_count);
    layout->addWidget(m_chipRow);
    layout->addWidget(buildLibraryChips());
    layout->addWidget(buildImportBanner());

    // Creator header: shown while browsing one creator's uploads.
    m_creatorHeader = new QWidget(center);
    m_creatorHeader->setObjectName("creatorHeader");
    m_creatorHeader->setStyleSheet("QWidget#creatorHeader { background: #17171B; border: 1px solid #26262C; border-radius: 6px; }");
    auto* creatorLayout = new QHBoxLayout(m_creatorHeader);
    creatorLayout->setContentsMargins(10, 8, 10, 8);
    creatorLayout->setSpacing(12);
    m_creatorAvatar = new QLabel(m_creatorHeader);
    m_creatorAvatar->setFixedSize(40, 40);
    m_creatorAvatar->setAlignment(Qt::AlignCenter);
    m_creatorAvatar->setStyleSheet("background: #22303A; color: #80D8FF; border-radius: 20px; font-size: 16px; font-weight: bold;");
    creatorLayout->addWidget(m_creatorAvatar);
    auto* creatorText = new QVBoxLayout();
    creatorText->setSpacing(2);
    m_creatorName = new QLabel(m_creatorHeader);
    m_creatorName->setTextFormat(Qt::RichText);
    m_creatorName->setOpenExternalLinks(true);
    m_creatorName->setStyleSheet("font-size: 14px; color: #F2F2F5;");
    m_creatorStats = new QLabel(m_creatorHeader);
    m_creatorStats->setStyleSheet("font-size: 11px; color: #9FA8B8;");
    creatorText->addWidget(m_creatorName);
    creatorText->addWidget(m_creatorStats);
    creatorLayout->addLayout(creatorText, 1);
    m_creatorBack = new QPushButton(m_creatorHeader);
    connect(m_creatorBack, &QPushButton::clicked, this, &Tone3000Dialog::goBack);
    creatorLayout->addWidget(m_creatorBack);
    m_creatorHeader->hide();
    layout->addWidget(m_creatorHeader);

    auto* activity = new ActivityBar(center);
    activity->setFixedHeight(2);
    m_activityBar = activity;
    layout->addWidget(activity);
    layout->setSpacing(8);

    m_model = new Tone3000ResultModel(this);
    m_delegate = new Tone3000RowDelegate(m_format, this);
    auto* resultList = new ResultListView(center);
    m_list = resultList;
    resultList->canDrag = [this]() { return m_tab == Tab::Library && !selectedKeys().isEmpty(); };
    resultList->dragMoved = [this](const QPoint& global) { libraryDragMoved(global); };
    resultList->dragEnded = [this](const QPoint& global, bool drop) { libraryDragEnded(global, drop); };
    m_list->setObjectName("results");
    m_list->setModel(m_model);
    m_list->setItemDelegate(m_delegate);
    m_list->setUniformItemSizes(false); // library group headers are shorter
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->verticalScrollBar()->setSingleStep(24);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly); // keeps a multi-selection when a drag starts on it
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);

    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, &Tone3000Dialog::onCurrentChanged);
    connect(m_list->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        updateLibraryChips();
        // Switching between one and several selected changes the panel.
        const bool batch = m_tab == Tab::Library && selectedLibraryRows() > 1;
        if (!m_batchChanges.isEmpty() && selectedKeys() != m_batchKeys) resolvePendingEdits();
        if (batch || m_batchInfo->isVisible()) {
            m_batchChanges = {};
            showDetails();
        }
    });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex& index) {
        const auto kind = static_cast<Tone3000ResultModel::RowKind>(index.data(Tone3000ResultModel::KindRole).toInt());
        if (kind == Tone3000ResultModel::RowKind::Status && m_model->status() == Tone3000ResultModel::Status::Error) {
            if (!Tone3000Api::hasKey()) m_apiKeyBanner->show();
            m_model->retry();
        } else if (kind == Tone3000ResultModel::RowKind::Group) {
            const QString group = m_model->tone(index.row())->raw.value("rigroom_group").toString();
            if (m_collapsedGroups.contains(group)) m_collapsedGroups.remove(group);
            else m_collapsedGroups.insert(group);
            QTimer::singleShot(0, this, [this]() { runView(); });
        } else if (kind == Tone3000ResultModel::RowKind::Creator) {
            if (const auto* creator = m_model->creator(index.row())) {
                const QString username = creator->username;
                // Leave the click handler before the list is replaced.
                QTimer::singleShot(0, this, [this, username]() { showCreator(username); });
            }
        }
    });
    connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (m_model->tone(index.row()) && currentVariant() >= 0) loadVariant(currentVariant());
    });
    connect(m_delegate, &Tone3000RowDelegate::starClicked, this, [this](int row) {
        if (const ToneItem* tone = m_model->tone(row)) toggleFavorite(*tone);
    });
    connect(m_delegate, &Tone3000RowDelegate::diskBadgeClicked, this, [this](int row) {
        if (const ToneItem* tone = m_model->tone(row)) {
            const int id = tone->id;
            // Leave the click handler before the list is replaced.
            QTimer::singleShot(0, this, [this, id]() { openInLibrary(id); });
        }
    });
    // Ask for the next page a little before the end, so scrolling rarely waits.
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar* bar = m_list->verticalScrollBar();
        if (value >= bar->maximum() - 6 * Tone3000RowDelegate::kRowHeight && m_model->canFetchMore({})) {
            m_model->fetchMore({});
        }
    });
    connect(m_model, &Tone3000ResultModel::loadingChanged, this, &Tone3000Dialog::updateActivity);
    connect(m_model, &Tone3000ResultModel::statusChanged, this, &Tone3000Dialog::updateCount);
    connect(m_model, &Tone3000ResultModel::pageLoaded, this, [this](int page) {
        updateCount();
        // The library keeps its selection by key itself; ids are 0 for files outside TONE3000.
        if (page == 1 && !m_restore.active && !isLibraryView()) {
            // Keep the selection if the tone is still in the list.
            const int row = m_model->rowForTone(m_selected.id);
            if (row >= 0) m_list->setCurrentIndex(m_model->index(row));
            m_list->scrollToTop();
        }
        if (m_restore.active) {
            const int row = m_model->rowForTone(m_restore.toneId);
            if (row >= 0 && m_list->currentIndex().row() != row) m_list->setCurrentIndex(m_model->index(row));
            if (page >= m_restore.pages || !m_model->canFetchMore({})) {
                const int scroll = m_restore.scroll;
                m_restore.active = false;
                QTimer::singleShot(0, this, [this, scroll]() { m_list->verticalScrollBar()->setValue(scroll); });
            }
        }
        if (!m_initialToneUrl.isEmpty()) {
            const QString wanted = QUrl(Tone3000::absoluteToneUrl(m_initialToneUrl))
                                       .adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery | QUrl::StripTrailingSlash)
                                       .toString();
            for (int row = 0; row < m_model->itemCount(); ++row) {
                const ToneItem* tone = m_model->tone(row);
                if (!tone) break;
                const QString url = QUrl(tone->url)
                                        .adjusted(QUrl::RemoveFragment | QUrl::RemoveQuery | QUrl::StripTrailingSlash)
                                        .toString();
                if (url == wanted) {
                    m_initialToneUrl.clear();
                    m_list->setCurrentIndex(m_model->index(row));
                    m_list->scrollTo(m_model->index(row));
                    break;
                }
            }
        }
    });

    auto* footer = new QHBoxLayout();
    auto* hint = new QLabel(QString::fromUtf8("↑ ↓ select · click a variant to hear it · Enter or double-click loads · ☆ favorite"), center);
    hint->setStyleSheet("color: #6E6E7A; font-size: 11px;");
    footer->addWidget(hint, 1);
    m_status = new QLabel(center);
    m_status->setStyleSheet("color: #8A8A96; font-size: 11px;");
    m_status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    footer->addWidget(m_status);
    layout->addLayout(footer);
    return center;
}

void Tone3000Dialog::addChoiceChip(QLayout* layout, const QString& name, std::vector<Choice> choices,
                                   QString Query::*field, bool namOnly, bool multi) {
    auto* button = new QToolButton(m_chipRow);
    button->setProperty("class", "formatChip");
    button->setCursor(Qt::PointingHandCursor);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setCheckable(true);
    auto* menu = new QMenu(button);
    // Several values can be picked without the menu closing after each one.
    if (multi) menu->installEventFilter(this);
    menu->setProperty("tone3000Multi", multi);
    button->setMenu(menu);
    layout->addWidget(button);
    m_chips.push_back(FilterChip{button, name, std::move(choices), field, namOnly, multi});
}

void Tone3000Dialog::fillChipMenu(FilterChip& chip) {
    QMenu* menu = chip.button->menu();
    menu->clear();
    const QString value = m_query.*chip.field;
    const QStringList selected = value.split(',', Qt::SkipEmptyParts);
    if (!chip.multi) {
        auto* group = new QActionGroup(menu);
        for (const Choice& choice : chip.choices) {
            QAction* action = menu->addAction(choice.label);
            action->setCheckable(true);
            action->setChecked(choice.value == value);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, field = chip.field, v = choice.value]() {
                if (m_query.*field == v) return;
                m_query.*field = v;
                onQueryEdited();
            });
        }
        return;
    }
    const bool tags = chip.field == &Query::tags;
    QAction* any = menu->addAction(tags ? "Any tag" : (m_format == Format::Ir ? "All types" : "All gear"));
    any->setCheckable(true);
    any->setChecked(selected.isEmpty());
    connect(any, &QAction::triggered, this, [this, field = chip.field]() {
        m_query.*field = QString();
        onQueryEdited();
    });
    menu->addSeparator();
    QStringList known;
    auto addValue = [this, menu, &chip, &selected](const QString& label, const QString& v) {
        QAction* action = menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(selected.contains(v));
        connect(action, &QAction::triggered, this, [this, field = chip.field, v]() { toggleChipValue(field, v); });
    };
    for (const Choice& choice : chip.choices) {
        addValue(choice.label, choice.value);
        known << choice.value;
    }
    // Tags picked from a result or typed in stay listed while they are on.
    for (const QString& v : selected) {
        if (!known.contains(v)) addValue(v, v);
    }
    if (tags) {
        menu->addSeparator();
        QAction* other = menu->addAction(QString::fromUtf8("Other tag…"));
        connect(other, &QAction::triggered, this, [this]() {
            bool ok = false;
            const QString tag = QInputDialog::getText(this, "Filter by Tag", "Tag:", QLineEdit::Normal, {}, &ok);
            if (ok) addTagFilter(tag);
        });
    }
}

void Tone3000Dialog::toggleChipValue(QString Query::*field, const QString& value) {
    QStringList values = (m_query.*field).split(',', Qt::SkipEmptyParts);
    if (values.contains(value)) values.removeAll(value);
    else values << value;
    m_query.*field = values.join(',');
    // Picking several values in a row runs one search.
    updateChips();
    m_searchTimer->start();
}

void Tone3000Dialog::addTagFilter(const QString& tag) {
    const QString value = tag.trimmed().toLower().replace(' ', '-');
    if (value.isEmpty()) return;
    QStringList values = m_query.tags.split(',', Qt::SkipEmptyParts);
    if (values.contains(value)) return;
    values << value;
    m_query.tags = values.join(',');
    if (m_tab == Tab::Library) {
        // In the library a tag filters the files by its category.
        QStringList& picked = m_libraryFilters[static_cast<int>(NamMetadata::tagCategory(value))];
        if (!picked.contains(value)) picked << value;
        updateLibraryChips();
        runView();
        return;
    }
    if (m_view == View::Favorites || m_view == View::Creators) m_view = View::Catalog;
    onQueryEdited();
}

void Tone3000Dialog::updateChips() {
    const bool remoteTones = m_tab == Tab::Online && m_view != View::Favorites && m_view != View::Creators;
    m_chipRow->setVisible(m_tab == Tab::Online);
    const Query defaults = Query::defaults(m_format);
    for (FilterChip& chip : m_chips) {
        chip.button->setVisible(remoteTones && (!chip.namOnly || m_format == Format::Nam));
        const QString value = m_query.*chip.field;
        QString label;
        if (chip.multi) {
            QStringList labels;
            for (const QString& v : value.split(',', Qt::SkipEmptyParts)) {
                QString l = v;
                for (const Choice& choice : chip.choices) {
                    if (choice.value == v) l = choice.label;
                }
                labels << l;
            }
            if (labels.isEmpty()) label = chip.field == &Query::tags ? "Any tag" : (m_format == Format::Ir ? "All types" : "All gear");
            else if (labels.size() <= 2) label = labels.join(", ");
            else label = QString("%1 +%2").arg(labels.first()).arg(labels.size() - 1);
        } else {
            label = value;
            for (const Choice& choice : chip.choices) {
                if (choice.value == value) label = choice.label;
            }
        }
        chip.button->setText(label + QString::fromUtf8("  ▾"));
        chip.button->setToolTip(chip.multi ? chip.name + " (pick one or more)" : chip.name);
        // A chip is highlighted when it narrows the results.
        chip.button->setChecked(chip.field != &Query::sort && value != defaults.*chip.field);
        if (!chip.button->menu()->isVisible()) fillChipMenu(chip);
    }
    m_calibratedChip->setVisible(remoteTones && m_format == Format::Nam);
    {
        const QSignalBlocker blocker(m_calibratedChip);
        m_calibratedChip->setChecked(m_query.calibrated);
    }
    m_resetChip->setVisible(remoteTones && m_query.hasFilters());
    m_saveSearchButton->setVisible(remoteTones);

    QString placeholder;
    switch (m_view) {
    case View::Creators: placeholder = "Search creators by name…"; break;
    case View::Favorites: placeholder = "Filter your favorites…"; break;
    case View::LibraryAll:
    case View::LibraryRecent:
        placeholder = m_mode == Mode::Ir ? "Filter your IRs by name, creator or tag…"
                                         : "Filter your captures by name, creator or tag…";
        break;
    default:
        placeholder = m_mode == Mode::Ir ? "Search cabinets, speakers, mics…" : "Search amps, pedals, tones…";
    }
    m_search->setPlaceholderText(placeholder);
}

QWidget* Tone3000Dialog::buildInfoPanel() {
    auto* scroll = m_infoScroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedWidth(400);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    auto* panel = new QWidget();
    panel->setStyleSheet("background: transparent;");
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 4, 0);
    panelLayout->setSpacing(0);

    m_infoEmpty = new QLabel(m_mode == Mode::Ir ? "Select an upload to see its details and IR files."
                                                : "Select a capture to see its details and variants.",
                             panel);
    static_cast<QLabel*>(m_infoEmpty)->setWordWrap(true);
    static_cast<QLabel*>(m_infoEmpty)->setAlignment(Qt::AlignCenter);
    m_infoEmpty->setStyleSheet("color: #6E6E7A; font-size: 12px; background: #17171B; border-radius: 10px; padding: 24px;");
    m_infoEmpty->setMinimumHeight(190);
    panelLayout->addWidget(m_infoEmpty);

    m_infoContent = new QWidget(panel);
    auto* info = new QVBoxLayout(m_infoContent);
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(8);
    m_image = new QLabel(m_infoContent);
    m_image->setFixedSize(380, 214);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setStyleSheet("background: #1B1B20; border-radius: 10px; color: #6E6E7A;");
    info->addWidget(m_image);
    m_title = new QLabel(m_infoContent);
    m_title->setWordWrap(true);
    m_title->setStyleSheet("font-size: 17px; font-weight: bold; color: #F2F2F5;");
    info->addWidget(m_title);
    m_byline = new QLabel(m_infoContent);
    m_byline->setTextFormat(Qt::RichText);
    m_byline->setWordWrap(true);
    m_byline->setStyleSheet("color: #9FA8B8; font-size: 12px;");
    connect(m_byline, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link.startsWith("creator:")) showCreator(QUrl::fromPercentEncoding(link.mid(8).toUtf8()));
        else if (link == "tone") openToneOnline();
        else if (link == "pack") editPack(m_selected.raw.value("rigroom_pack").toString());
        else QDesktopServices::openUrl(QUrl(link));
    });
    info->addWidget(m_byline);
    m_tags = new QLabel(m_infoContent);
    m_tags->setTextFormat(Qt::RichText);
    m_tags->setWordWrap(true);
    m_tags->setStyleSheet("font-size: 11px;");
    m_tags->setToolTip("Click a tag to show only uploads with it");
    connect(m_tags, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link.startsWith("tag:")) addTagFilter(QUrl::fromPercentEncoding(link.mid(4).toUtf8()));
    });
    info->addWidget(m_tags);
    m_description = new QLabel(m_infoContent);
    m_description->setWordWrap(true);
    m_description->setTextFormat(Qt::PlainText);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_description->setStyleSheet("color: #C8C8D0; font-size: 12px;");
    info->addWidget(m_description);
    m_moreButton = new QToolButton(m_infoContent);
    m_moreButton->setText("Show more");
    m_moreButton->setCheckable(true);
    m_moreButton->setStyleSheet("QToolButton { color: #00B0FF; border: none; background: transparent; font-size: 11px; }");
    connect(m_moreButton, &QToolButton::toggled, this, [this]() { showDetails(); });
    info->addWidget(m_moreButton, 0, Qt::AlignLeft);

    m_variantsLabel = new QLabel(m_infoContent);
    m_variantsLabel->setStyleSheet("color: #8A8A96; font-size: 10px; font-weight: bold; margin-top: 6px;");
    m_variantsLabel->setTextFormat(Qt::RichText);
    m_variantsLabel->setToolTip("The NAM filter above decides which capture versions are listed");
    connect(m_variantsLabel, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "others") {
            m_otherVariants = !m_otherVariants;
            fillVariants();
        } else {
            showAllVariants();
        }
    });
    info->addWidget(m_variantsLabel);
    m_variants = new QListWidget(m_infoContent);
    m_variants->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_variants->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_variants->setStyleSheet(
        "QListWidget { background: #17171B; border: 1px solid #26262C; border-radius: 6px; color: #D8D8DE; outline: none; font-size: 12px; }"
        "QListWidget::item { padding: 6px 8px; }"
        "QListWidget::item:selected { background: #0B4F6C; color: white; }"
        "QListWidget::item:hover:!selected { background: #202026; }");
    connect(m_variants, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        previewVariant(m_variants->row(item));
    });
    connect(m_variants, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        loadVariant(m_variants->row(item));
    });
    connect(m_variants, &QListWidget::currentRowChanged, this, [this]() {
        updateAddButtons();
        m_loadButton->setEnabled(currentVariant() >= 0 && !m_loadTransfer.reply);
        if (!isLibraryView() || selectedLibraryRows() > 1) return;
        const QString path = activeLibraryPath();
        if (path.isEmpty()) return;
        if (!m_editPath.isEmpty() && m_editPath != path) resolvePendingEdits();
        m_preferredVariantPath = path;
        showLibraryDetails();
    });
    info->addWidget(m_variants);
    // Library fields follow the variant list so a grouped tone's files are
    // picked first; the fields then edit that file.
    info->addWidget(buildLibraryInfo());
    auto* addRow = new QHBoxLayout();
    addRow->setSpacing(6);
    // Click adds to the folder used last; the arrow picks another folder.
    m_addToLibraryButton = new QToolButton(m_infoContent);
    m_addToLibraryButton->setText(QString::fromUtf8("+ Add to Library"));
    m_addToLibraryButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_addToLibraryButton->setCursor(Qt::PointingHandCursor);
    m_addToLibraryButton->setStyleSheet(
        "QToolButton { background: #2A2A30; color: #E0E0E0; border: 1px solid #3A3A42; border-radius: 5px; "
        "padding: 6px 22px 6px 12px; font-size: 13px; } QToolButton:hover { background: #34343C; }"
        "QToolButton:disabled { color: #666; }");
    auto* addMenu = new QMenu(m_addToLibraryButton);
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu]() {
        fillFolderMenu(addMenu, [this](const QString& folder) {
            CaptureLibrary::AddOptions options;
            options.folder = folder;
            if (currentVariant() >= 0) addVariantsToLibrary({currentVariant()}, options);
        });
    });
    m_addToLibraryButton->setMenu(addMenu);
    connect(m_addToLibraryButton, &QToolButton::clicked, this, [this]() {
        if (currentVariant() >= 0) addVariantsToLibrary({currentVariant()}, CaptureLibrary::AddOptions());
    });
    m_addAllButton = new QPushButton(m_infoContent);
    m_addAllButton->setStyleSheet(BrowserStyle::kSecondaryButton);
    m_addAllButton->setAutoDefault(false);
    connect(m_addAllButton, &QPushButton::clicked, this, [this]() {
        // Every variant not in the library yet, to pick from and file together.
        QList<AddToLibraryDialog::Row> rows;
        const QString group = m_selected.raw.value("title").toString(m_selected.title);
        for (int i = 0; i < m_models.size(); ++i) {
            const QJsonObject model = m_models[i].toObject();
            const QString local = localPathFor(model);
            if (!local.isEmpty() && CaptureLibrary::instance().contains(local)) continue;
            QString detail = modelDetails(model);
            if (!local.isEmpty()) detail += detail.isEmpty() ? "downloaded" : QString::fromUtf8(" · downloaded");
            rows.append({QString::number(i), group, model.value("name").toString(), detail});
        }
        AddToLibraryDialog dialog("Add to Library", rows, m_format, this);
        if (dialog.exec() != QDialog::Accepted) return;
        QList<int> indices;
        for (const QString& key : dialog.selectedKeys()) indices << key.toInt();
        CaptureLibrary::AddOptions options = dialog.options();
        if (options.pack.startsWith("new:")) options.pack = CaptureLibrary::instance().createPack(options.pack.mid(4));
        addVariantsToLibrary(indices, options);
    });
    addRow->addWidget(m_addToLibraryButton);
    addRow->addWidget(m_addAllButton);
    addRow->addStretch();
    info->addLayout(addRow);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_favoriteButton = new QPushButton(m_infoContent);
    m_favoriteButton->setStyleSheet(BrowserStyle::kSecondaryButton);
    m_favoriteButton->setToolTip("Favorite (Ctrl+D)");
    connect(m_favoriteButton, &QPushButton::clicked, this, [this]() { toggleFavorite(m_selected); });
    m_webButton = new QPushButton(QString::fromUtf8("Web ↗"), m_infoContent);
    m_webButton->setStyleSheet(BrowserStyle::kSecondaryButton);
    m_webButton->setToolTip("Open this page on tone3000.com");
    connect(m_webButton, &QPushButton::clicked, this, [this]() {
        if (!m_selected.url.isEmpty()) QDesktopServices::openUrl(QUrl(m_selected.url));
    });
    m_loadButton = new QPushButton("Load", m_infoContent);
    m_loadButton->setStyleSheet(BrowserStyle::kPrimaryButton);
    m_loadButton->setDefault(false);
    m_loadButton->setAutoDefault(false);
    connect(m_loadButton, &QPushButton::clicked, this, [this]() { loadVariant(currentVariant()); });
    buttons->addWidget(m_favoriteButton);
    buttons->addWidget(m_webButton);
    buttons->addStretch();
    buttons->addWidget(m_loadButton);
    info->addLayout(buttons);

    m_progress = new QProgressBar(m_infoContent);
    m_progress->setFixedHeight(4);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet("QProgressBar { background: #1F1F24; border: none; border-radius: 2px; }"
                              "QProgressBar::chunk { background: #00B0FF; border-radius: 2px; }");
    m_progress->hide();
    info->addWidget(m_progress);
    info->addStretch();
    panelLayout->addWidget(m_infoContent);
    panelLayout->addWidget(buildBatchInfo());
    panelLayout->addStretch();
    m_infoContent->hide();
    scroll->setWidget(panel);
    return scroll;
}

// ─── Views and searching ─────────────────────────────────────────────────────

void Tone3000Dialog::setView(View view, int savedIndex) {
    if (view == View::Saved) {
        const auto saved = Tone3000Library::instance().savedSearches(m_format);
        if (savedIndex < 0 || savedIndex >= saved.size()) return;
        m_query = saved[savedIndex].query;
        m_query.format = m_format;
    }
    const bool sameView = view == m_view && savedIndex == m_savedIndex;
    m_view = view;
    m_savedIndex = view == View::Saved ? savedIndex : -1;
    m_backStack.clear();
    m_creatorInfo = QJsonObject();
    switch (view) {
    case View::Trending: m_query.sort = "trending"; break;
    case View::Newest: m_query.sort = "newest"; break;
    case View::MostDownloaded: m_query.sort = "downloads"; break;
    default: break;
    }
    if (view != View::Saved && !sameView) m_query.creator.clear();
    m_query.source = view == View::Creators ? Query::Source::Creators : Query::Source::Catalog;
    {
        const QSignalBlocker blocker(m_search);
        if (view == View::Saved) m_search->setText(m_query.text);
    }
    m_searchTimer->stop();
    selectSidebarView();
    updateChips();
    updateCreatorHeader();
    runView();
}

void Tone3000Dialog::onQueryEdited() {
    m_searchTimer->stop();
    if (isLibraryView()) {
        runView();
        return;
    }
    m_query.text = m_search->text().trimmed();
    // Changing a saved search or a sort shortcut turns it into a plain search.
    View view = m_view;
    if (view == View::Saved || view == View::Trending || view == View::Newest || view == View::MostDownloaded
        || view == View::Catalog) {
        if (m_query.sort == "trending") view = View::Trending;
        else if (m_query.sort == "newest") view = View::Newest;
        else if (m_query.sort == "downloads") view = View::MostDownloaded;
        else view = View::Catalog;
        if (m_view == View::Saved) {
            const auto saved = Tone3000Library::instance().savedSearches(m_format);
            if (m_savedIndex >= 0 && m_savedIndex < saved.size() && saved[m_savedIndex].query == m_query) view = View::Saved;
        }
    }
    if (view != m_view) {
        m_view = view;
        if (view != View::Saved) m_savedIndex = -1;
        selectSidebarView();
    }
    updateChips();
    runView();
}

void Tone3000Dialog::runView(int restorePages) {
    if (isLibraryView()) {
        runLibraryView();
        updateCount();
        updateActivity();
        return;
    }
    m_query.text = m_search->text().trimmed();
    m_query.format = m_format;
    const QString text = m_query.text;
    Tone3000Library& library = Tone3000Library::instance();
    auto localList = [&text](const QList<QJsonObject>& tones, const std::function<QJsonArray(int)>& models = {}) {
        std::vector<ToneItem> items;
        items.reserve(tones.size());
        for (const QJsonObject& tone : tones) {
            ToneItem item = ToneItem::fromJson(tone);
            if (!Tone3000::matchesText(item, text)) continue;
            if (models) item.raw["models"] = models(static_cast<int>(items.size()));
            items.push_back(std::move(item));
        }
        return items;
    };

    switch (m_view) {
    case View::Favorites:
        m_model->setLocal(localList(library.favorites(m_format)),
                          text.isEmpty() ? "No favorites yet. Click ☆ on a result to keep it here." : "No favorites match.");
        break;
    default:
        if (!Tone3000Api::hasKey()) m_apiKeyBanner->show();
        m_model->setQuery(m_query, restorePages);
        break;
    }
    updateCount();
    updateActivity();
}

void Tone3000Dialog::updateCount() {
    if (m_model->isLoading() && (m_model->itemCount() == 0 || m_model->isStale())) {
        m_count->setText("Searching…");
        return;
    }
    const int total = m_model->total();
    QString noun;
    switch (m_view) {
    case View::Creators: noun = total == 1 ? "creator" : "creators"; break;
    case View::Favorites: noun = total == 1 ? "favorite" : "favorites"; break;
    case View::LibraryAll:
    case View::LibraryRecent:
        noun = m_mode == Mode::Ir ? (total == 1 ? "IR" : "IRs") : (total == 1 ? "capture" : "captures");
        break;
    default:
        noun = m_mode == Mode::Ir ? (total == 1 ? "upload" : "uploads") : (total == 1 ? "result" : "results");
    }
    m_count->setText(QString("%1 %2").arg(QLocale().toString(total), noun));
    if (!isLibraryView()) m_libraryCount->setText(m_count->text());
}

void Tone3000Dialog::updateActivity() {
    const bool busy = m_model->isLoading() || m_modelsLoading || m_previewTransfer.reply || m_loadTransfer.reply
        || m_addTransfer.reply;
    static_cast<ActivityBar*>(m_activityBar)->setActive(busy);
    if (busy && !m_animation->isActive()) m_animation->start();
    if (!busy && m_animation->isActive() && m_model->status() != Tone3000ResultModel::Status::Waiting) m_animation->stop();
    updateCount();
}

void Tone3000Dialog::saveCurrentSearch() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Save Search", "Name:", QLineEdit::Normal, m_query.describe(), &ok);
    if (!ok) return;
    Tone3000Library::instance().addSavedSearch(name.trimmed(), m_query);
    const auto saved = Tone3000Library::instance().savedSearches(m_format);
    for (int i = 0; i < saved.size(); ++i) {
        if (saved[i].query == m_query) {
            m_view = View::Saved;
            m_savedIndex = i;
        }
    }
    selectSidebarView();
    setStatus("Search saved to the sidebar.");
}

// ─── Settings ────────────────────────────────────────────────────────────────

void Tone3000Dialog::loadSettings() {
    QFile file(configDir() + "/browser_settings.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject settings = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject mine = settings.value(Tone3000::formatKey(m_format)).toObject();
    const QString tab = mine.value("tab").toString();
    if (!tab.isEmpty()) m_tabFromSettings = true;
    m_tab = tab == "online" ? Tab::Online : Tab::Library;
    const QString libraryView = mine.value("libraryView").toString();
    m_libraryView = libraryView == "favorites" ? View::LibraryTopRated
                  : libraryView == "recent" ? View::LibraryRecent
                  : libraryView == "top" ? View::LibraryTopRated : View::LibraryAll;
    m_librarySort = mine.value("librarySort").toString(m_librarySort);
    m_libraryGroup = mine.value("libraryGroup").toString(m_libraryGroup);
    for (const QJsonValue& group : mine.value("collapsedGroups").toArray()) m_collapsedGroups.insert(group.toString());
    if (!mine.isEmpty()) {
        m_query = Query::fromJson(mine.value("query").toObject(), m_format);
        m_query.source = Query::Source::Catalog;
        m_query.creator.clear();
        const QString view = mine.value("view").toString();
        if (view == "favorites") m_view = View::Favorites;
        else if (view == "creators") m_view = View::Creators;
        else if (m_query.sort == "newest") m_view = View::Newest;
        else if (m_query.sort == "downloads") m_view = View::MostDownloaded;
        else if (m_query.sort == "trending") m_view = View::Trending;
        else m_view = View::Catalog;
        if (m_view == View::Creators) m_query.source = Query::Source::Creators;
    } else if (m_format == Format::Nam && settings.contains("gearValue")) {
        // Settings from the previous browser.
        m_query.text = settings.value("search").toString();
        m_query.gear = settings.value("gearValue").toString(m_query.gear);
        m_query.tags = settings.value("characterValue").toString();
        m_query.architecture = settings.value("architectureValue").toString(m_query.architecture);
        m_query.size = settings.value("sizeValue").toString();
        m_query.calibrated = settings.value("calibrated").toBool();
        if (settings.value("favorites_only").toBool()) m_view = View::Favorites;
    }
    m_onlineView = m_view;
    m_view = m_tab == Tab::Library ? m_libraryView : m_onlineView;
    {
        // The search box shows the open tab's text; the other waits for its turn.
        const QSignalBlocker blocker(m_search);
        m_search->setText(m_tab == Tab::Online ? m_query.text : QString());
        m_otherTabSearch = m_tab == Tab::Online ? QString() : m_query.text;
    }
    const QByteArray geometry = QByteArray::fromBase64(settings.value("windowGeometry").toString().toLatin1());
    if (!geometry.isEmpty()) restoreGeometry(geometry);
}

void Tone3000Dialog::saveSettings() {
    const QString path = configDir() + "/browser_settings.json";
    QJsonObject settings;
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) settings = QJsonDocument::fromJson(file.readAll()).object();
    }
    if (m_tab == Tab::Library) m_libraryView = m_view;
    else m_onlineView = m_view;
    QString view = "catalog";
    switch (m_onlineView) {
    case View::Favorites: view = "favorites"; break;
    case View::Creators: view = "creators"; break;
    default: break;
    }
    Query query = m_query;
    query.text = (m_tab == Tab::Online ? m_search->text() : m_otherTabSearch).trimmed();
    settings[Tone3000::formatKey(m_format)] = QJsonObject{
        {"query", query.toJson()},
        {"view", view},
        {"tab", m_tab == Tab::Online ? "online" : "library"},
        {"libraryView", m_libraryView == View::LibraryRecent ? "recent"
                        : m_libraryView == View::LibraryTopRated ? "top" : "all"},
        {"librarySort", m_librarySort},
        {"libraryGroup", m_libraryGroup},
        {"collapsedGroups", QJsonArray::fromStringList(QStringList(m_collapsedGroups.begin(), m_collapsedGroups.end()))}};
    settings["windowGeometry"] = QString::fromLatin1(saveGeometry().toBase64());
    QDir().mkpath(configDir());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(settings).toJson());
        file.commit();
    }
}

void Tone3000Dialog::done(int result) {
    resolvePendingEdits();
    saveSettings();
    cancelTransfer(m_previewTransfer);
    cancelTransfer(m_loadTransfer);
    cancelTransfer(m_addTransfer);
    QDialog::done(result);
}

void Tone3000Dialog::reject() {
    if (m_isPreviewing && m_node && m_engine) applyFileToNode(m_originalModelPath);
    m_isPreviewing = false;
    QDialog::reject();
}

// ─── Public entry points ─────────────────────────────────────────────────────

void Tone3000Dialog::setInitialSearchQuery(const QString& query) {
    m_tabFromSettings = true;
    if (m_tab != Tab::Online) setTab(Tab::Online);
    {
        const QSignalBlocker blocker(m_search);
        m_search->setText(query);
    }
    if (m_view == View::Favorites || m_view == View::Creators || m_view == View::Saved) {
        m_view = m_query.sort == "trending" ? View::Trending : View::Catalog;
        m_query.source = Query::Source::Catalog;
        selectSidebarView();
        updateChips();
    }
    m_query.creator.clear();
    runView();
}

void Tone3000Dialog::setInitialTone(const QString& sourceUrl, const QString& captureName) {
    m_initialToneUrl = sourceUrl;
    setInitialSearchQuery(captureName);
}

// ─── Selection and details ───────────────────────────────────────────────────

void Tone3000Dialog::onCurrentChanged() {
    const QModelIndex index = m_list->currentIndex();
    const ToneItem* tone = index.isValid() ? m_model->tone(index.row()) : nullptr;
    if (!tone || tone->raw.contains("rigroom_group")) return;
    const QString key = tone->raw.value("rigroom_key").toString();
    if (!m_editPath.isEmpty() && key != m_editPath) {
        // Saving rebuilds the list, so find the row picked again afterwards.
        resolvePendingEdits();
        for (int row = 0; row < m_model->itemCount(); ++row) {
            if (m_model->tone(row)->raw.value("rigroom_key").toString() != key) continue;
            if (m_list->currentIndex().row() != row) m_list->setCurrentIndex(m_model->index(row));
            else onCurrentChanged();
            return;
        }
        return;
    }
    if (!key.isEmpty() && key == selectedKey() && tone->title == m_selected.title) {
        // The same library entry after a refresh: take its new tags and files,
        // keep what is playing.
        m_selected = *tone;
        m_models = tone->raw.value("models").toArray();
        showDetails();
        return;
    }
    if (key.isEmpty() && tone->id == m_selected.id && tone->id != 0 && tone->title == m_selected.title) return;
    selectTone(*tone);
}

void Tone3000Dialog::selectTone(const ToneItem& tone) {
    m_selected = tone;
    m_preferredVariantPath = tone.raw.value("rigroom_key").toString();
    m_models = QJsonArray();
    m_modelsToneId = 0;
    m_previewingIndex = -1;
    m_moreButton->setChecked(false);
    m_modelsTimer->stop();
    delete m_modelsRequest;
    m_modelsLoading = false;
    m_allVariants = false;
    m_otherVariants = false;

    // Models we already have: stored with a download or favorite, or cached.
    QJsonArray known = tone.raw.value("models").toArray();
    if (known.isEmpty() && tone.id != 0) known = Tone3000Library::instance().storedModels(tone.id);
    QJsonObject cached;
    const QString arch = variantArchitecture();
    const QString key = QString("models|%1|%2").arg(tone.id).arg(arch);
    if (tone.id != 0 && Tone3000Api::instance()->cached(key, &cached)) {
        setModels(tone.id, cached.value("data").toArray());
    } else if (!known.isEmpty() && (isLibraryView() || !Tone3000Api::hasKey() || tone.id == 0)) {
        setModels(tone.id, known);
    } else if (tone.id != 0) {
        if (!known.isEmpty()) m_models = known; // shown until the fresh list arrives
        m_modelsLoading = true;
        m_modelsTimer->start();
    }
    showDetails();
    updateActivity();
}

void Tone3000Dialog::clearDetails() {
    m_selected = ToneItem();
    m_models = QJsonArray();
    showDetails();
}

void Tone3000Dialog::showDetails() {
    // Several library files selected: one editor for all of them.
    const bool batch = m_tab == Tab::Library && selectedLibraryRows() > 1;
    m_batchInfo->setVisible(batch);
    if (batch) {
        m_infoEmpty->hide();
        m_infoContent->hide();
        showBatchDetails();
        return;
    }
    const bool hasTone = !m_selected.title.isEmpty() || m_selected.id != 0;
    m_infoEmpty->setVisible(!hasTone);
    m_infoContent->setVisible(hasTone);
    if (!hasTone) return;

    if (m_image->property("tone3000ImageUrl").toString() != m_selected.imageUrl || m_image->pixmap().isNull()) {
        m_image->clear();
        m_image->setText(m_selected.imageUrl.isEmpty() ? (m_mode == Mode::Ir ? "IR" : "NAM") : "Loading image...");
        Tone3000ImageLoader::instance()->load(m_image, m_selected.imageUrl);
    }
    m_title->setText(m_selected.title);
    QStringList by;
    const QString toneTitle = m_selected.raw.value("title").toString();
    if (const QString pack = m_selected.raw.value("rigroom_pack_title").toString(); !pack.isEmpty()) {
        by << QString("in pack <a href='pack' style='color:#80D8FF; text-decoration:none;'>%1</a>").arg(pack.toHtmlEscaped());
    } else if (!selectedKey().isEmpty() && m_selected.id != 0 && !toneTitle.isEmpty() && toneTitle != m_selected.title) {
        // A library file from TONE3000: say which tone, and open it there.
        by << QString("from <a href='tone' style='color:#80D8FF; text-decoration:none;'>%1</a>").arg(toneTitle.toHtmlEscaped());
    }
    if (!m_selected.creator.isEmpty()) {
        by << QString("by <a href='creator:%1' style='color:#80D8FF; text-decoration:none;'>%2</a>")
                  .arg(QString(QUrl::toPercentEncoding(m_selected.creator)), m_selected.creator.toHtmlEscaped());
    }
    if (m_selected.downloads > 0 || m_selected.favorites > 0) {
        by << QString::fromUtf8("↓ %1 &nbsp; ★ %2")
                  .arg(tone3000ShortCount(m_selected.downloads), tone3000ShortCount(m_selected.favorites));
    }
    m_byline->setText(by.join(" &nbsp;·&nbsp; "));
    m_byline->setToolTip(m_selected.creator.isEmpty() ? QString() : "Show everything by " + m_selected.creator);

    QStringList facts;
    if (!m_selected.gear.isEmpty()) facts << m_selected.gear;
    facts << m_selected.makes;
    QString chips = chipsHtml(facts, "#0B4F6C", "#E0F4FF");
    const QString tagChips = chipsHtml(m_selected.tags, "#24242A", "#9FA8B8", true);
    if (!tagChips.isEmpty()) chips += (chips.isEmpty() ? "" : "<br>") + tagChips;
    m_tags->setText(chips);
    m_tags->setVisible(!chips.isEmpty());

    const QString description = m_selected.description.trimmed();
    constexpr int kShortLength = 280;
    const bool longText = description.size() > kShortLength + 40;
    m_description->setText(description.isEmpty() ? "No description."
                           : (longText && !m_moreButton->isChecked() ? description.left(kShortLength).trimmed() + QString::fromUtf8("…")
                                                                    : description));
    m_moreButton->setVisible(longText);
    m_moreButton->setText(m_moreButton->isChecked() ? "Show less" : "Show more");

    m_webButton->setEnabled(!m_selected.url.isEmpty());
    fillVariants();
    // The star saves a tone on the TONE3000 account; library files are rated instead.
    const bool favorite = Tone3000Library::instance().isFavorite(m_selected.id);
    m_favoriteButton->setText(favorite ? QString::fromUtf8("★ Saved") : QString::fromUtf8("☆ Save"));
    m_favoriteButton->setToolTip("Save on your TONE3000 account (Ctrl+D)");
    m_favoriteButton->setVisible(selectedKey().isEmpty());
    m_favoriteButton->setEnabled(m_selected.id != 0);
    showLibraryDetails();
}

void Tone3000Dialog::fillVariants() {
    const bool libraryRow = !selectedKey().isEmpty();
    const QString noun = libraryRow ? "IN YOUR LIBRARY" : (m_mode == Mode::Ir ? "IR FILES" : "CAPTURE VARIANTS");
    const int keep = currentVariant();
    const QSignalBlocker blocker(m_variants);
    m_variants->clear();
    if (m_models.isEmpty()) {
        m_variantsLabel->setText(noun);
        auto* item = new QListWidgetItem(m_modelsLoading ? QString::fromUtf8("Loading…")
                                                         : (m_mode == Mode::Ir ? "No IR files in this upload."
                                                            : variantArchitecture().isEmpty() ? "No captures found for this tone."
                                                                                             : "No captures match the NAM filter."),
                                         m_variants);
        item->setFlags(Qt::NoItemFlags);
        m_variants->setFixedHeight(36);
        m_loadButton->setEnabled(false);
        return;
    }
    // A library row lists only the files the user added; the tone's other
    // variants stay behind a link, so adding one capture shows one capture.
    const CaptureLibrary& captures = CaptureLibrary::instance();
    QList<bool> added;
    int addedCount = 0;
    for (const QJsonValue& value : m_models) {
        const QString local = localPathFor(value.toObject());
        added << (!local.isEmpty() && captures.contains(local));
        addedCount += added.last();
    }
    const int others = libraryRow ? static_cast<int>(m_models.size()) - addedCount : 0;
    QString label = QString("%1 (%2)").arg(noun).arg(libraryRow ? addedCount : m_models.size());
    if (others > 0) {
        label += QString("  · <a href='others' style='color:#00B0FF; text-decoration:none;'>%1</a>")
                     .arg(m_otherVariants ? "Hide the others" : QString("Show %1 more from this tone").arg(others));
    }
    // Say when the NAM filter hides some of this tone's captures, and offer them.
    const QString arch = variantArchitecture();
    const int hidden = arch.isEmpty() ? 0 : m_selected.modelCount - static_cast<int>(m_models.size());
    if (m_modelsLoading) {
        label += QString::fromUtf8("  · updating…");
    } else if (hidden > 0) {
        label += QString::fromUtf8("  · A%1 only · <a href='all' style='color:#00B0FF; text-decoration:none;'>"
                                   "show %2 more</a>").arg(arch).arg(hidden);
    }
    m_variantsLabel->setText(label);
    for (int i = 0; i < m_models.size(); ++i) {
        const QJsonObject model = m_models[i].toObject();
        const QString details = modelDetails(model);
        QString text = model.value("name").toString();
        if (!details.isEmpty()) text += "   " + details;
        QString prefix = "    ";
        QString tip = "Click to hear it; double-click to load it";
        if (i == m_previewingIndex && m_previewTransfer.reply) {
            prefix = QString::fromUtf8("…  ");
            tip = "Downloading preview…";
        } else if (i == m_previewingIndex) {
            prefix = QString::fromUtf8("▶  ");
        } else if (libraryRow) {
            if (!added[i]) tip = "Not in your library; select it and click + Add. " + tip;
        } else if (const QString local = localPathFor(model); !local.isEmpty()) {
            const bool inLibrary = added[i];
            prefix = inLibrary ? QString::fromUtf8("✓  ") : QString::fromUtf8("•  ");
            tip = (inLibrary ? "In your library. " : "Downloaded, not in your library. ") + tip;
        }
        auto* item = new QListWidgetItem(prefix + text, m_variants);
        item->setToolTip(tip);
        if (libraryRow && !added[i]) {
            // Hidden rather than left out, so list rows keep matching m_models.
            item->setForeground(QColor("#6E6E7A"));
            item->setHidden(!m_otherVariants);
        }
    }
    const int visible = libraryRow && !m_otherVariants ? addedCount : static_cast<int>(m_models.size());
    const int rows = std::clamp(visible, 1, 12);
    const int rowHeight = std::max(28, m_variants->sizeHintForRow(0));
    m_variants->setFixedHeight(rows * rowHeight + 2 * m_variants->frameWidth() + 2);
    if (keep >= 0 && keep < m_variants->count()) m_variants->setCurrentRow(keep);
    else if (m_previewingIndex >= 0 && m_previewingIndex < m_variants->count()) m_variants->setCurrentRow(m_previewingIndex);
    else if (m_models.size() == 1) m_variants->setCurrentRow(0);
    else if (!m_preferredVariantPath.isEmpty()) {
        // A library row shows its tone's variants with its own file picked.
        for (int i = 0; i < m_models.size(); ++i) {
            if (m_models[i].toObject().value("local_path").toString() == m_preferredVariantPath) m_variants->setCurrentRow(i);
        }
    }
    m_loadButton->setEnabled(currentVariant() >= 0 && !m_loadTransfer.reply);
    updateAddButtons();
}

int Tone3000Dialog::currentVariant() const {
    const int row = m_variants ? m_variants->currentRow() : -1;
    return row >= 0 && row < m_models.size() ? row : -1;
}

void Tone3000Dialog::requestModels() {
    const int toneId = m_selected.id;
    if (toneId == 0) return;
    const QString arch = variantArchitecture();
    auto done = [this, toneId](bool ok, const QJsonArray& models, const QString& error) {
        if (toneId != m_selected.id) return;
        m_modelsLoading = false;
        if (ok) {
            setModels(toneId, models);
        } else {
            const QJsonArray stored = Tone3000Library::instance().storedModels(toneId);
            if (!stored.isEmpty()) setModels(toneId, stored);
            else fillVariants();
            if (!Tone3000Api::hasKey()) m_apiKeyBanner->show();
            setStatus("Couldn't load the variants: " + error, true);
        }
        updateActivity();
    };
    delete m_modelsRequest;
    if (arch.isEmpty()) {
        // Every architecture: the API lists only A2 unless asked.
        m_modelsRequest = new QObject(this);
        Tone3000Api::instance()->fetchAllModels(toneId, m_modelsRequest, done);
        return;
    }
    QUrl url("https://www.tone3000.com/api/v1/models");
    QUrlQuery q;
    q.addQueryItem("tone_id", QString::number(toneId));
    q.addQueryItem("page", "1");
    q.addQueryItem("page_size", "300");
    q.addQueryItem("architecture", arch);
    url.setQuery(q);
    m_modelsRequest = Tone3000Api::instance()->getJson(
        url, this,
        [done](const Tone3000Api::Response& response) {
            done(response.ok, response.object.value("data").toArray(), response.errorString);
        },
        [this](int seconds) { setStatus(QString("TONE3000 is busy. Trying again in %1 s…").arg(seconds)); },
        QString("models|%1|%2").arg(toneId).arg(arch));
}

QString Tone3000Dialog::variantArchitecture() const {
    // Library views hide the filter chips, so they show every capture.
    return m_allVariants ? QString() : m_model->architectureFilter();
}

void Tone3000Dialog::showAllVariants() {
    if (m_selected.id == 0 || m_allVariants) return;
    m_allVariants = true;
    QJsonObject cached;
    if (Tone3000Api::instance()->cached(QString("models|%1|").arg(m_selected.id), &cached)) {
        setModels(m_selected.id, cached.value("data").toArray());
        return;
    }
    m_modelsLoading = true;
    fillVariants();
    updateActivity();
    requestModels();
}

void Tone3000Dialog::setModels(int toneId, const QJsonArray& models) {
    m_modelsToneId = toneId;
    m_models = models;
    m_modelsLoading = false;
    fillVariants();
}

QString Tone3000Dialog::modeCacheDir() const {
    return CaptureLibrary::instance().cacheDir(m_format);
}

QString Tone3000Dialog::localPathFor(const QJsonObject& model) const {
    const QString stored = model.value("local_path").toString();
    if (!stored.isEmpty() && QFile::exists(stored)) return stored;
    if (m_selected.id == 0) return {};
    const QString path = modeCacheDir() + "/" + Tone3000::toneFolderName(m_selected.raw) + "/"
        + Tone3000::modelFileName(model, m_format, false);
    return QFileInfo(path).size() > 0 ? path : QString();
}

// ─── Preview and load ────────────────────────────────────────────────────────

void Tone3000Dialog::previewVariant(int index) {
    if (index < 0 || index >= m_models.size()) return;
    const QJsonObject model = m_models[index].toObject();
    cancelTransfer(m_previewTransfer);
    m_previewingIndex = index;

    // Files already on disk play at once.
    const QString local = localPathFor(model);
    if (!local.isEmpty()) {
        applyFileToNode(local.toStdString());
        m_isPreviewing = true;
        setStatus("Previewing " + model.value("name").toString() + ". Play your guitar.");
        fillVariants();
        return;
    }
    const QString url = model.value("model_url").toString();
    if (url.isEmpty()) {
        setStatus("This file has no download link.", true);
        return;
    }
    const QString previewDir = modeCacheDir() + "/tone_preview";
    QDir().mkpath(previewDir);
    m_previewTransfer.path = previewDir + "/" + Tone3000::modelFileName(model, m_format, true);
    m_previewTransfer.tone = m_selected.raw;
    m_previewTransfer.models = m_models;
    m_previewTransfer.modelIndex = index;
    m_previewTransfer.redirects = 0;
    startTransfer(m_previewTransfer, QUrl(url), TransferKind::Preview);
    setStatus("Downloading preview…");
    fillVariants();
}

void Tone3000Dialog::loadVariant(int index) {
    if (index < 0 || index >= m_models.size() || m_loadTransfer.reply) return;
    const QJsonObject model = m_models[index].toObject();
    Transfer transfer;
    transfer.tone = m_selected.raw;
    transfer.models = m_models;
    transfer.modelIndex = index;

    const QString local = localPathFor(model);
    if (!local.isEmpty()) {
        finishLoad(transfer, local);
        return;
    }
    const QString url = model.value("model_url").toString();
    if (url.isEmpty()) {
        setStatus("This file has no download link.", true);
        return;
    }
    const QString folder = modeCacheDir() + "/" + Tone3000::toneFolderName(transfer.tone);
    QDir().mkpath(folder);
    transfer.path = folder + "/" + Tone3000::modelFileName(model, m_format, false);
    m_loadTransfer = transfer;
    m_progress->setRange(0, 0);
    m_progress->show();
    m_loadButton->setEnabled(false);
    m_loadButton->setText("Downloading…");
    startTransfer(m_loadTransfer, QUrl(url), TransferKind::Load);
    setStatus("Downloading " + model.value("name").toString() + QString::fromUtf8("… you can keep browsing."));
}

Tone3000Dialog::Transfer& Tone3000Dialog::transferFor(TransferKind kind) {
    switch (kind) {
    case TransferKind::Preview: return m_previewTransfer;
    case TransferKind::Load: return m_loadTransfer;
    case TransferKind::Add: return m_addTransfer;
    }
    return m_loadTransfer;
}

void Tone3000Dialog::startTransfer(Transfer& transfer, const QUrl& url, TransferKind kind) {
    const bool preview = kind == TransferKind::Preview;
    QNetworkRequest request = transfer.redirects == 0 && url.host().endsWith("tone3000.com")
        ? Tone3000Api::authorized(url) : QNetworkRequest(url);
    // Redirects are followed by hand so the key is never sent to the file host.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = Tone3000Api::instance()->network()->get(request);
    transfer.reply = reply;
    if (!preview) {
        connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
            if ((reply != m_loadTransfer.reply && reply != m_addTransfer.reply) || total <= 0) return;
            m_progress->setRange(0, 100);
            m_progress->setValue(static_cast<int>(received * 100 / total));
        });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, kind, preview]() {
        Transfer& t = transferFor(kind);
        reply->deleteLater();
        if (reply != t.reply) return;
        t.reply = nullptr;

        const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirect.isValid() && reply->error() == QNetworkReply::NoError && t.redirects < 5) {
            ++t.redirects;
            startTransfer(t, reply->url().resolved(redirect.toUrl()), kind);
            return;
        }
        auto fail = [this, kind, preview](const QString& message) {
            if (kind == TransferKind::Add) {
                m_progress->hide();
                m_addQueue.clear();
                updateAddButtons();
            } else if (!preview) {
                m_progress->hide();
                m_loadButton->setText("Load");
                m_loadButton->setEnabled(currentVariant() >= 0);
            } else {
                m_previewingIndex = -1;
                fillVariants();
            }
            setStatus(message, true);
            updateActivity();
        };
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError) {
                fail("Download failed: " + reply->errorString());
            }
            updateActivity();
            return;
        }
        QSaveFile file(t.path);
        if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0 || !file.commit()) {
            fail("Couldn't save " + QFileInfo(t.path).fileName());
            return;
        }
        if (preview) {
            if (m_node && m_engine) {
                applyFileToNode(t.path.toStdString());
                m_isPreviewing = true;
            }
            const bool stillShown = t.tone.value("id").toInt() == m_selected.id;
            if (stillShown) {
                const QString name = t.models[t.modelIndex].toObject().value("name").toString();
                setStatus("Previewing " + name + ". Play your guitar.");
                fillVariants();
            }
        } else if (kind == TransferKind::Add) {
            m_progress->hide();
            // Downloaded for the library: keep the tone's details next to it and add it.
            CaptureLibrary& captures = CaptureLibrary::instance();
            captures.recordDownload(t.tone, t.models, m_format);
            captures.add({t.path}, m_addOptions);
            setStatus(QString("Added %1 to your library.").arg(QFileInfo(t.path).completeBaseName()));
            addNextQueued();
        } else {
            m_progress->hide();
            finishLoad(t, t.path);
        }
        updateActivity();
    });
    updateActivity();
}

void Tone3000Dialog::cancelTransfer(Transfer& transfer) {
    if (QNetworkReply* reply = transfer.reply) {
        transfer.reply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

void Tone3000Dialog::finishLoad(const Transfer& transfer, const QString& path) {
    const QJsonObject tone = transfer.tone;
    const QJsonObject model = transfer.models[transfer.modelIndex].toObject();
    const ToneItem item = ToneItem::fromJson(tone);
    const QString name = model.value("name").toString();

    m_downloadedModelPath = path.toStdString();
    m_downloadedToneName = !item.title.isEmpty() && item.title != name ? item.title + " - " + name : name;
    QString slug = item.slug;
    if (slug.isEmpty()) {
        slug = item.title.toLower();
        slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
        slug.remove(QRegularExpression("^-|-$"));
    }
    m_downloadedToneUrl = item.url;
    if (m_downloadedToneUrl.isEmpty() && item.id > 0) {
        m_downloadedToneUrl = "https://www.tone3000.com/tones/" + (slug.isEmpty() ? QString::number(item.id) : slug);
    }
    m_downloadedMetadata = AudioNode::ModelMetadata{};
    m_downloadedMetadata.toneId = item.id > 0 ? QString::number(item.id).toStdString() : "";
    m_downloadedMetadata.toneTitle = item.title.toStdString();
    m_downloadedMetadata.toneSlug = slug.toStdString();
    m_downloadedMetadata.author = item.creator.toStdString();
    m_downloadedMetadata.gearType = item.gear.toStdString();
    m_downloadedMetadata.imageUrl = item.imageUrl.toStdString();
    m_downloadedMetadata.tags = item.tags.join(", ").toStdString();
    m_downloadedMetadata.description = item.description.toStdString();

    // A NAM block keeps the tone's other variants so they can be switched later.
    m_downloadedVariants.clear();
    if (m_mode == Mode::Nam) {
        for (int i = 0; i < transfer.models.size(); ++i) {
            const QJsonObject variant = transfer.models[i].toObject();
            AudioNode::ModelVariant var;
            var.name = variant.value("name").toString().toStdString();
            var.url = variant.value("model_url").toString().toStdString();
            if (i == transfer.modelIndex) {
                var.localPath = m_downloadedModelPath;
            } else {
                QString local = variant.value("local_path").toString();
                if (local.isEmpty() && item.id != 0) {
                    local = modeCacheDir() + "/" + Tone3000::toneFolderName(tone) + "/"
                        + Tone3000::modelFileName(variant, m_format, false);
                }
                if (!local.isEmpty() && QFile::exists(local)) var.localPath = local.toStdString();
            }
            m_downloadedVariants.push_back(var);
        }
        if (m_node) m_node->setModelVariants(m_downloadedVariants);
    }

    // Downloads join the library with their TONE3000 details, and what is
    // loaded shows up under Recently used.
    CaptureLibrary& captures = CaptureLibrary::instance();
    if (item.id != 0 && !QFile::exists(captures.cacheDir(m_format) + "/" + Tone3000::toneFolderName(tone) + "/tone.json")) {
        captures.recordDownload(tone, transfer.models, m_format);
    } else {
        captures.rescan();
    }
    captures.markUsed(path); // only counts for files in the library
    m_isPreviewing = false;
    accept();
}

void Tone3000Dialog::applyFileToNode(const std::string& path) {
    if (!m_node || !m_engine) return;
    m_engine->suspendProcessing();
    if (m_mode == Mode::Ir) m_node->setFileProperty(m_irPropertyUri, path);
    else m_node->loadModelFile(path);
    m_engine->resumeProcessing();
}

// ─── Favorites ───────────────────────────────────────────────────────────────

void Tone3000Dialog::toggleFavorite(const ToneItem& tone) {
    // Only TONE3000 tones have a star (saved on the account); library files are rated.
    if (tone.raw.contains("rigroom_key")) return;
    if (tone.id == 0) return;
    Tone3000Library& library = Tone3000Library::instance();
    const bool favorite = !library.isFavorite(tone.id);
    QJsonArray models = tone.id == m_selected.id ? m_models : library.storedModels(tone.id);
    for (int i = 0; i < models.size(); ++i) {
        QJsonObject model = models[i].toObject();
        model.remove("local_path");
        models[i] = model;
    }
    // Show the change at once; undo it if TONE3000 refuses.
    library.setFavorite(tone.raw, models, m_format, favorite);
    setStatus(favorite ? "Added to favorites." : "Removed from favorites.");
    if (!Tone3000Api::hasKey()) return;
    QNetworkRequest request = Tone3000Api::authorized(QUrl(QString("https://www.tone3000.com/api/v1/tones/%1/favorite").arg(tone.id)));
    QNetworkAccessManager* network = Tone3000Api::instance()->network();
    QNetworkReply* reply = favorite ? network->put(request, QByteArray()) : network->deleteResource(request);
    const QJsonObject raw = tone.raw;
    connect(reply, &QNetworkReply::finished, this, [this, reply, favorite, raw, models]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) return;
        Tone3000Library::instance().setFavorite(raw, models, m_format, !favorite);
        setStatus("Couldn't update your TONE3000 favorites: " + reply->errorString(), true);
    });
}

// ─── Creators ────────────────────────────────────────────────────────────────

void Tone3000Dialog::showCreator(const QString& name) {
    const QString username = name.trimmed();
    m_tabFromSettings = true;
    if (m_tab != Tab::Online) {
        // From the library, a creator opens on TONE3000; there is nothing to go back to there.
        setTab(Tab::Online);
        m_backStack.clear();
        m_query.creator.clear();
    }
    if (username.isEmpty() || (username == m_query.creator && m_query.source == Query::Source::Catalog)) return;
    BackEntry entry;
    entry.view = m_view;
    entry.savedIndex = m_savedIndex;
    entry.query = m_query;
    entry.query.text = m_search->text().trimmed();
    entry.pages = std::max(1, m_model->loadedPages());
    entry.scroll = m_list->verticalScrollBar()->value();
    entry.toneId = m_selected.id;
    m_backStack.push_back(entry);

    m_query.creator = username;
    m_query.source = Query::Source::Catalog;
    m_query.text.clear();
    {
        // Show all of the creator's uploads, not only those matching the old words.
        const QSignalBlocker blocker(m_search);
        m_search->clear();
    }
    m_view = m_query.sort == "trending" ? View::Trending
        : m_query.sort == "newest" ? View::Newest
        : m_query.sort == "downloads" ? View::MostDownloaded : View::Catalog;
    m_savedIndex = -1;
    m_creatorInfo = QJsonObject();
    selectSidebarView();
    updateChips();
    updateCreatorHeader();
    fetchCreatorInfo(username);
    runView();
}

void Tone3000Dialog::fetchCreatorInfo(const QString& username) {
    delete m_creatorRequest;
    QUrl url("https://www.tone3000.com/api/v1/users");
    QUrlQuery q;
    q.addQueryItem("query", username);
    q.addQueryItem("page_size", "10");
    url.setQuery(q);
    m_creatorRequest = Tone3000Api::instance()->getJson(
        url, this,
        [this, username](const Tone3000Api::Response& response) {
            if (!response.ok || username != m_query.creator) return;
            for (const QJsonValue& value : response.object.value("data").toArray()) {
                const QJsonObject user = value.toObject();
                if (user.value("username").toString().compare(username, Qt::CaseInsensitive) == 0) {
                    m_creatorInfo = user;
                    break;
                }
            }
            updateCreatorHeader();
        },
        {}, "users|" + username.toLower());
}

void Tone3000Dialog::updateCreatorHeader() {
    const QString username = m_query.creator;
    const bool visible = m_tab == Tab::Online && !username.isEmpty() && m_query.source == Query::Source::Catalog;
    m_creatorHeader->setVisible(visible);
    if (!visible) return;
    m_creatorBack->setText(m_backStack.empty() ? QString::fromUtf8("×  Everyone") : QString::fromUtf8("←  Back"));
    m_creatorBack->setToolTip(m_backStack.empty() ? "Show uploads by everyone"
                                                  : "Back to where you were, with the same search and results");
    const Tone3000::CreatorItem creator = Tone3000::CreatorItem::fromJson(
        m_creatorInfo.isEmpty() ? QJsonObject{{"username", username}} : m_creatorInfo);
    m_creatorName->setText(QString("<b>%1</b>%2 &nbsp;<span style='color:#8A8A96;'>@%3</span> &nbsp;"
                                   "<a href='%4' style='color:#6F8796; text-decoration:none;'>web ↗</a>")
                               .arg(creator.displayName.toHtmlEscaped(),
                                    creator.verified ? QString(" <span style='color:#00B0FF;'>✓</span>") : QString(),
                                    username.toHtmlEscaped(), creator.url.toHtmlEscaped()));
    m_creatorStats->setText(m_creatorInfo.isEmpty()
        ? (m_mode == Mode::Ir ? "Impulse responses by this creator" : "Captures by this creator")
        : QString::fromUtf8("%1 uploads  ·  ↓ %2  ·  ★ %3")
              .arg(creator.tones)
              .arg(tone3000ShortCount(creator.downloads), tone3000ShortCount(creator.favorites)));
    m_creatorAvatar->setText(username.left(1).toUpper());
    m_creatorAvatar->setPixmap(QPixmap());
    Tone3000ImageLoader::instance()->load(m_creatorAvatar, creator.avatarUrl);
}

void Tone3000Dialog::goBack() {
    if (m_backStack.empty()) {
        m_query.creator.clear();
        updateCreatorHeader();
        runView();
        return;
    }
    const BackEntry entry = m_backStack.back();
    m_backStack.pop_back();
    m_view = entry.view;
    m_savedIndex = entry.savedIndex;
    m_query = entry.query;
    {
        const QSignalBlocker blocker(m_search);
        m_search->setText(entry.query.text);
    }
    if (!m_query.creator.isEmpty()) fetchCreatorInfo(m_query.creator);
    else m_creatorInfo = QJsonObject();
    selectSidebarView();
    updateChips();
    updateCreatorHeader();
    m_restore = PendingRestore{entry.pages, entry.scroll, entry.toneId, true};
    runView(entry.pages);
    if (!m_model->isRemote()) {
        // Library lists are here at once.
        const int row = m_model->rowForTone(entry.toneId);
        if (row >= 0) m_list->setCurrentIndex(m_model->index(row));
        m_restore.active = false;
        QTimer::singleShot(0, this, [this, scroll = entry.scroll]() { m_list->verticalScrollBar()->setValue(scroll); });
    }
}

// ─── Misc ────────────────────────────────────────────────────────────────────

void Tone3000Dialog::setStatus(const QString& text, bool error) {
    m_status->setStyleSheet(error ? "color: #F08080; font-size: 11px;" : "color: #8A8A96; font-size: 11px;");
    m_status->setText(QFontMetrics(m_status->font()).elidedText(text, Qt::ElideRight, 520));
    m_status->setToolTip(text);
}

bool Tone3000Dialog::eventFilter(QObject* watched, QEvent* event) {
    // Scrolling the panel must not flip through a drop-down under the pointer;
    // a drop-down takes the wheel only once it has been clicked.
    if (event->type() == QEvent::Wheel && qobject_cast<QComboBox*>(watched)
        && !static_cast<QWidget*>(watched)->hasFocus()) {
        QCoreApplication::sendEvent(m_infoScroll->verticalScrollBar(), event);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        if (auto* menu = qobject_cast<QMenu*>(watched); menu && menu->property("tone3000Multi").toBool()) {
            QAction* action = menu->actionAt(static_cast<QMouseEvent*>(event)->position().toPoint());
            // Toggle in place; "All" and "Other tag…" close the menu as usual.
            if (action && action->isCheckable() && !action->text().startsWith("All") && !action->text().startsWith("Any")) {
                action->trigger();
                return true;
            }
        }
    }
    if (event->type() == QEvent::KeyPress && (watched == m_search || watched == m_list)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (watched == m_search && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up
                                    || key->key() == Qt::Key_PageDown || key->key() == Qt::Key_PageUp)) {
            // Arrow keys move through the results while typing.
            const int count = m_model->itemCount();
            if (count == 0) return true;
            const int step = key->key() == Qt::Key_Down ? 1 : key->key() == Qt::Key_Up ? -1
                           : key->key() == Qt::Key_PageDown ? 8 : -8;
            const int current = m_list->currentIndex().isValid() ? m_list->currentIndex().row() : -1;
            int next = std::clamp(current < 0 ? (step > 0 ? 0 : count - 1) : current + step, 0, count - 1);
            // Group headers are not captures; step over them.
            const int direction = step > 0 ? 1 : -1;
            while (next >= 0 && next < count && m_model->tone(next) && m_model->tone(next)->raw.contains("rigroom_group")) {
                next += direction;
            }
            if (next < 0 || next >= count) return true;
            m_list->setCurrentIndex(m_model->index(next));
            m_list->scrollTo(m_model->index(next));
            return true;
        }
        if (watched == m_list && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            const QModelIndex index = m_list->currentIndex();
            if (index.isValid() && m_model->creator(index.row())) {
                const QString username = m_model->creator(index.row())->username;
                QTimer::singleShot(0, this, [this, username]() { showCreator(username); });
            } else if (currentVariant() >= 0) {
                loadVariant(currentVariant());
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
