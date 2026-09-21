// The Library tab of the TONE3000 browser: the captures and impulse responses
// the user added. Group: Tone shows one row per tone or pack (files live in
// the panel); other groupings keep a header plus one row per file.
#include "Tone3000Dialog.h"
#include "BrowserStyle.h"
#include "CaptureLibrary.h"
#include "AddToLibraryDialog.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include "Tone3000ResultModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using NamMetadata::TagCategory;
using Tone3000::ToneItem;

namespace {
const QList<QPair<TagCategory, QString>>& filterCategories() {
    static const QList<QPair<TagCategory, QString>> list{
        {TagCategory::Type, "Type"}, {TagCategory::Tone, "Tone"}, {TagCategory::Brand, "Brand"},
        {TagCategory::Other, "My tags"}, {TagCategory::Tech, "Tech"}};
    return list;
}

QString segmentStyle() {
    return "QPushButton { background: #1F1F24; color: #B8B8C4; border: 1px solid #34343C; padding: 6px 0; "
           "font-size: 12px; font-weight: bold; }"
           "QPushButton:checked { background: #0B4F6C; color: white; border-color: #00B0FF; }"
           "QPushButton:hover:!checked { background: #26262C; }";
}

QString shownTag(const QString& tag) {
    return NamMetadata::tagCategory(tag) == TagCategory::Other ? tag : NamMetadata::tagLabel(tag);
}

QToolButton* makeChip(QWidget* parent) {
    auto* chip = new QToolButton(parent);
    chip->setProperty("class", "formatChip");
    chip->setCursor(Qt::PointingHandCursor);
    chip->setPopupMode(QToolButton::InstantPopup);
    chip->setCheckable(true);
    chip->setMenu(new QMenu(chip));
    return chip;
}
} // namespace

// ─── Tabs ────────────────────────────────────────────────────────────────────

QWidget* Tone3000Dialog::buildTabs() {
    auto* tabs = new QWidget(this);
    tabs->setFixedWidth(200);
    auto* layout = new QHBoxLayout(tabs);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_libraryTab = new QPushButton("Library", tabs);
    m_libraryTab->setToolTip(m_mode == Mode::Ir ? "Your impulse responses" : "Your captures");
    m_onlineTab = new QPushButton("TONE3000", tabs);
    m_onlineTab->setToolTip("Search and download from tone3000.com");
    for (QPushButton* button : {m_libraryTab, m_onlineTab}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoDefault(false);
        layout->addWidget(button);
    }
    m_libraryTab->setStyleSheet(segmentStyle() + "QPushButton { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }");
    m_onlineTab->setStyleSheet(segmentStyle() + "QPushButton { border-top-right-radius: 6px; border-bottom-right-radius: 6px; "
                                                "border-left: none; }");
    connect(m_libraryTab, &QPushButton::clicked, this, [this]() {
        m_tabFromSettings = true;
        setTab(Tab::Library);
    });
    connect(m_onlineTab, &QPushButton::clicked, this, [this]() {
        m_tabFromSettings = true;
        setTab(Tab::Online);
    });
    return tabs;
}

void Tone3000Dialog::setTab(Tab tab) {
    if (tab != m_tab) resolvePendingEdits();
    m_libraryTab->setChecked(tab == Tab::Library);
    m_onlineTab->setChecked(tab == Tab::Online);
    if (tab == m_tab && (tab == Tab::Library) == isLibraryView()) return;
    if (m_tab == Tab::Library && isLibraryView()) m_libraryView = m_view;
    else if (m_tab == Tab::Online && !isLibraryView()) m_onlineView = m_view;
    if (tab != m_tab) {
        const QSignalBlocker blocker(m_search);
        const QString current = m_search->text();
        m_search->setText(m_otherTabSearch);
        m_otherTabSearch = current;
    }
    m_tab = tab;
    m_view = tab == Tab::Library ? m_libraryView : m_onlineView;
    m_searchTimer->stop();
    // Several files can be tagged, rated or removed at once in the library.
    m_list->setSelectionMode(tab == Tab::Library ? QAbstractItemView::ExtendedSelection
                                                 : QAbstractItemView::SingleSelection);
    rebuildSidebar();
    updateChips();
    updateLibraryChips();
    updateCreatorHeader();
    runView();
}

// ─── Sidebar ─────────────────────────────────────────────────────────────────

void Tone3000Dialog::rebuildLibrarySidebar() {
    const QSignalBlocker blocker(m_sidebar);
    m_dropTarget = nullptr;
    m_sidebar->clear();
    const CaptureLibrary& library = CaptureLibrary::instance();
    int used = 0, top = 0, missing = 0;
    const QList<CaptureLibrary::File> files = library.files(m_format);
    for (const CaptureLibrary::File& f : files) {
        const CaptureLibrary::UserData d = library.userData(f.path);
        used += d.lastUsed > 0;
        top += d.rating >= 4;
        missing += f.missing;
    }
    auto* header = new QListWidgetItem(m_mode == Mode::Ir ? "YOUR IRS" : "YOUR CAPTURES", m_sidebar);
    header->setFlags(Qt::NoItemFlags);
    QFont font = m_sidebar->font();
    font.setPixelSize(10);
    font.setBold(true);
    header->setFont(font);
    header->setForeground(QColor("#6E6E7A"));
    header->setSizeHint(QSize(10, 22));
    QListWidgetItem* current = nullptr;
    auto view = [&](const QString& text, View v) {
        auto* item = new QListWidgetItem(text, m_sidebar);
        item->setData(Qt::UserRole, static_cast<int>(v));
        if (m_view == v) current = item;
    };
    view(QString("%1  (%2)").arg(m_mode == Mode::Ir ? "All IRs" : "All captures").arg(files.size()), View::LibraryAll);
    view(QString("Recently used  (%1)").arg(used), View::LibraryRecent);
    view(QString::fromUtf8("★ Top rated  (%1)").arg(top), View::LibraryTopRated);
    if (missing > 0 || m_view == View::LibraryMissing) view(QString("Missing files  (%1)").arg(missing), View::LibraryMissing);

    // The user's folders: every capture is filed in one of them.
    auto* folders = new QListWidgetItem("FOLDERS", m_sidebar);
    folders->setFlags(Qt::NoItemFlags);
    folders->setFont(font);
    folders->setForeground(QColor("#6E6E7A"));
    folders->setSizeHint(QSize(10, 30));
    QHash<QString, int> counts;
    for (const CaptureLibrary::File& f : files) ++counts[library.folderOf(f.path)];
    for (const QString& folder : library.folders()) {
        auto* item = new QListWidgetItem(QString::fromUtf8("▸  %1  (%2)").arg(folder).arg(counts.value(folder)), m_sidebar);
        item->setData(Qt::UserRole, static_cast<int>(View::LibraryFolder));
        item->setData(Qt::UserRole + 1, folder);
        item->setToolTip(folder == CaptureLibrary::kDefaultFolder
                             ? "Where captures go unless you pick another folder. Drop captures on a folder to move them."
                             : "Drop captures here to move them; right-click to rename or delete");
        if (m_view == View::LibraryFolder && m_libraryFolderName == folder) current = item;
    }
    auto* add = new QListWidgetItem(QString::fromUtf8("+  New folder…"), m_sidebar);
    add->setData(Qt::UserRole, -1);
    add->setForeground(QColor("#8A8A96"));
    m_sidebar->setCurrentItem(current);
}

QString Tone3000Dialog::askNewFolder() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return {};
    CaptureLibrary::instance().addFolder(name);
    return name;
}

void Tone3000Dialog::fillFolderMenu(QMenu* menu, const std::function<void(const QString&)>& pick) {
    menu->clear();
    for (const QString& folder : CaptureLibrary::instance().folders()) {
        connect(menu->addAction(folder), &QAction::triggered, this, [pick, folder]() { pick(folder); });
    }
    menu->addSeparator();
    connect(menu->addAction(QString::fromUtf8("New folder…")), &QAction::triggered, this, [this, pick]() {
        const QString folder = askNewFolder();
        if (!folder.isEmpty()) pick(folder);
    });
}

void Tone3000Dialog::onLibrarySidebarClicked(QListWidgetItem* item) {
    if (!item || !(item->flags() & Qt::ItemIsSelectable)) return;
    resolvePendingEdits();
    if (item->data(Qt::UserRole).toInt() == -1) {
        QTimer::singleShot(0, this, [this]() { askNewFolder(); });
        return;
    }
    m_view = static_cast<View>(item->data(Qt::UserRole).toInt());
    if (m_view == View::LibraryFolder) m_libraryFolderName = item->data(Qt::UserRole + 1).toString();
    m_libraryView = m_view;
    QTimer::singleShot(0, this, [this]() {
        rebuildSidebar();
        updateLibraryChips();
        runView();
    });
}

// ─── Chips row ───────────────────────────────────────────────────────────────

QWidget* Tone3000Dialog::buildLibraryChips() {
    m_libraryChipRow = new QWidget(this);
    auto* row = new QHBoxLayout(m_libraryChipRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    for (const auto& [category, name] : filterCategories()) {
        QToolButton* chip = makeChip(m_libraryChipRow);
        chip->setToolTip(name + " (pick one or more)");
        chip->menu()->setProperty("tone3000Multi", true);
        chip->menu()->installEventFilter(this);
        m_libraryFilterChips << chip;
        row->addWidget(chip);
    }
    row->addSpacing(8);
    m_libraryGroupChip = makeChip(m_libraryChipRow);
    m_libraryGroupChip->setCheckable(false);
    m_libraryGroupChip->setToolTip("Tone: one row per tone or pack, files in the panel. "
                                   "Creator and Folder keep a header above each section.");
    for (const auto& [label, value] : {std::pair{"No groups", "none"}, {"Tone", "tone"}, {"Creator", "creator"},
                                       {"Folder", "folder"}}) {
        QAction* action = m_libraryGroupChip->menu()->addAction(label);
        action->setCheckable(true);
        action->setData(QString(value));
        connect(action, &QAction::triggered, this, [this, v = QString(value)]() {
            m_libraryGroup = v;
            updateLibraryChips();
            runView();
        });
    }
    row->addWidget(m_libraryGroupChip);
    m_librarySortChip = makeChip(m_libraryChipRow);
    m_librarySortChip->setCheckable(false);
    m_librarySortChip->setToolTip("Order");
    for (const auto& [label, value] : {std::pair{"Name", "name"}, {"Rating", "rating"}, {"Recently used", "used"},
                                       {"Recently added", "added"}}) {
        QAction* action = m_librarySortChip->menu()->addAction(label);
        action->setCheckable(true);
        action->setData(QString(value));
        connect(action, &QAction::triggered, this, [this, v = QString(value)]() {
            m_librarySort = v;
            updateLibraryChips();
            runView();
        });
    }
    row->addWidget(m_librarySortChip);
    row->addStretch();

    m_selectionButton = makeChip(m_libraryChipRow);
    m_selectionButton->setCheckable(false);
    QMenu* selection = m_selectionButton->menu();
    connect(selection->addAction(QString::fromUtf8("Add tag…")), &QAction::triggered, this, &Tone3000Dialog::tagSelected);
    QMenu* rate = selection->addMenu("Rate");
    for (int stars = 5; stars >= 0; --stars) {
        QAction* action = rate->addAction(stars == 0 ? QString("No rating") : QString(stars, QChar(0x2605)));
        connect(action, &QAction::triggered, this, [this, stars]() {
            CaptureLibrary::instance().setRating(selectedKeys(), stars);
        });
    }
    QMenu* move = selection->addMenu("Move to folder");
    connect(move, &QMenu::aboutToShow, this, [this, move]() {
        fillFolderMenu(move, [this](const QString& folder) { CaptureLibrary::instance().moveToFolder(selectedKeys(), folder); });
    });
    selection->addSeparator();
    connect(selection->addAction(QString::fromUtf8("Remove from library…")), &QAction::triggered, this,
            [this]() { removeFromLibrary(selectedKeys()); });
    m_selectionButton->hide();
    row->addWidget(m_selectionButton);

    m_libraryCount = new QLabel(m_libraryChipRow);
    m_libraryCount->setStyleSheet("color: #8A8A96; font-size: 11px;");
    row->addWidget(m_libraryCount);

    m_libraryAddButton = new QToolButton(m_libraryChipRow);
    m_libraryAddButton->setText(QString::fromUtf8("+ Add  ▾"));
    m_libraryAddButton->setPopupMode(QToolButton::InstantPopup);
    m_libraryAddButton->setCursor(Qt::PointingHandCursor);
    m_libraryAddButton->setStyleSheet(
        "QToolButton { background: #00897B; color: white; font-weight: bold; border: none; border-radius: 12px; "
        "padding: 4px 14px; font-size: 11px; } QToolButton:hover { background: #009688; }"
        "QToolButton::menu-indicator { image: none; width: 0; }");
    auto* add = new QMenu(m_libraryAddButton);
    connect(add->addAction(QString::fromUtf8("Files…")), &QAction::triggered, this, &Tone3000Dialog::addFilesToLibrary);
    connect(add->addAction(QString::fromUtf8("Folder…")), &QAction::triggered, this, &Tone3000Dialog::addFolderToLibrary);
    connect(add->addAction(QString::fromUtf8("From earlier downloads…")), &QAction::triggered, this,
            &Tone3000Dialog::importEarlierDownloads);
    add->addSeparator();
    connect(add->addAction(QString::fromUtf8("Browse TONE3000")), &QAction::triggered, this, [this]() { setTab(Tab::Online); });
    m_libraryAddButton->setMenu(add);
    row->addWidget(m_libraryAddButton);
    m_libraryChipRow->hide();
    return m_libraryChipRow;
}

void Tone3000Dialog::fillLibraryFilterMenu(int index) {
    const TagCategory category = filterCategories()[index].first;
    const QString name = filterCategories()[index].second;
    QToolButton* chip = m_libraryFilterChips[index];
    QMenu* menu = chip->menu();
    const QStringList picked = m_libraryFilters.value(static_cast<int>(category));
    const QList<QPair<QString, int>> values = CaptureLibrary::instance().tagsByCategory(m_format).value(category);
    // A category nobody uses has no chip, unless something in it is picked.
    chip->setVisible(m_tab == Tab::Library && (!values.isEmpty() || !picked.isEmpty()));
    if (menu->isVisible()) return;
    menu->clear();
    QAction* any = menu->addAction(QString("Any %1").arg(name.toLower()));
    any->setCheckable(true);
    any->setChecked(picked.isEmpty());
    connect(any, &QAction::triggered, this, [this, category]() {
        m_libraryFilters.remove(static_cast<int>(category));
        updateLibraryChips();
        runView();
    });
    menu->addSeparator();
    QStringList shown;
    auto addValue = [this, menu, category, &picked, &shown](const QString& tag, int count) {
        QAction* action = menu->addAction(count > 0 ? QString("%1   (%2)").arg(shownTag(tag)).arg(count) : shownTag(tag));
        action->setCheckable(true);
        action->setChecked(picked.contains(tag));
        connect(action, &QAction::triggered, this, [this, category, tag]() {
            QStringList& list = m_libraryFilters[static_cast<int>(category)];
            if (list.contains(tag)) list.removeAll(tag);
            else list << tag;
            if (list.isEmpty()) m_libraryFilters.remove(static_cast<int>(category));
            updateLibraryChips();
            runView();
        });
        shown << tag;
    };
    for (const auto& [tag, count] : values) addValue(tag, count);
    for (const QString& tag : picked) {
        if (!shown.contains(tag)) addValue(tag, 0);
    }
}

void Tone3000Dialog::updateLibraryChips() {
    if (!m_libraryChipRow) return;
    const bool library = m_tab == Tab::Library;
    m_libraryChipRow->setVisible(library);
    for (int i = 0; i < m_libraryFilterChips.size(); ++i) {
        const TagCategory category = filterCategories()[i].first;
        const QStringList picked = m_libraryFilters.value(static_cast<int>(category));
        QStringList labels;
        for (const QString& tag : picked) labels << shownTag(tag);
        QString text = filterCategories()[i].second;
        if (!labels.isEmpty()) {
            text = labels.size() <= 2 ? labels.join(", ") : QString("%1 +%2").arg(labels.first()).arg(labels.size() - 1);
        }
        m_libraryFilterChips[i]->setText(text + QString::fromUtf8("  ▾"));
        m_libraryFilterChips[i]->setChecked(!picked.isEmpty());
        fillLibraryFilterMenu(i);
    }
    QString groupLabel = "No groups";
    for (QAction* action : m_libraryGroupChip->menu()->actions()) {
        action->setChecked(action->data().toString() == m_libraryGroup);
        if (action->isChecked() && m_libraryGroup != "none") groupLabel = "Group: " + action->text();
    }
    m_libraryGroupChip->setText(groupLabel + QString::fromUtf8("  ▾"));
    QString sortLabel = "Name";
    for (QAction* action : m_librarySortChip->menu()->actions()) {
        action->setChecked(action->data().toString() == m_librarySort);
        if (action->isChecked()) sortLabel = action->text();
    }
    m_librarySortChip->setText(sortLabel + QString::fromUtf8("  ▾"));
    m_librarySortChip->setEnabled(m_view != View::LibraryRecent);
    const int selected = selectedLibraryRows();
    m_selectionButton->setVisible(library && selected > 1);
    m_selectionButton->setText(QString::fromUtf8("%1 selected  ▾").arg(selected));

    // Offer earlier downloads while the library is still empty.
    const CaptureLibrary& captures = CaptureLibrary::instance();
    const int candidates = static_cast<int>(captures.importCandidates(m_format).size());
    m_importBanner->setVisible(library && captures.isScanned() && captures.files(m_format).isEmpty() && candidates > 0);
    m_importBanner->setText(QString::fromUtf8("You have %1 earlier downloads on this computer. Add them to your library…")
                                .arg(candidates));
}

QWidget* Tone3000Dialog::buildImportBanner() {
    m_importBanner = new QPushButton(this);
    m_importBanner->setCursor(Qt::PointingHandCursor);
    m_importBanner->setAutoDefault(false);
    m_importBanner->setStyleSheet(
        "QPushButton { background: #13232C; color: #D8E8F0; border: 1px solid #0B4F6C; border-radius: 6px; "
        "padding: 10px 14px; text-align: left; font-size: 12px; } QPushButton:hover { border-color: #00B0FF; }");
    connect(m_importBanner, &QPushButton::clicked, this, &Tone3000Dialog::importEarlierDownloads);
    m_importBanner->hide();
    return m_importBanner;
}

// ─── The list ────────────────────────────────────────────────────────────────

void Tone3000Dialog::runLibraryView() {
    const CaptureLibrary& library = CaptureLibrary::instance();
    const QString keep = !activeLibraryPath().isEmpty() ? activeLibraryPath() : selectedKey();
    const QStringList keepSelected = selectedLibraryRows() > 1 ? selectedKeys() : QStringList();
    const int scroll = m_list->verticalScrollBar()->value();
    QStringList words = m_search->text().split(' ', Qt::SkipEmptyParts);
    for (QString& word : words) {
        if (word.startsWith('#')) word.remove(0, 1);
    }

    struct Row {
        ToneItem item;
        QString groupKey;
        QString groupTitle;
        QString groupSubtitle;
        QString groupImage;
        int rating = 0;
        qint64 used = 0;
        qint64 added = 0;
    };
    std::vector<Row> rows;
    const QList<CaptureLibrary::File> files = library.files(m_format);
    // A pack's captures, listed like a tone's variants.
    QHash<QString, QJsonArray> packMembers;
    for (const CaptureLibrary::File& f : files) {
        const QString pack = library.packOf(f.path);
        if (!pack.isEmpty()) packMembers[pack].append(QJsonObject{{"name", library.displayName(f)}, {"local_path", f.path}});
    }
    for (const CaptureLibrary::File& f : files) {
        const CaptureLibrary::UserData d = library.userData(f.path);
        if (m_view == View::LibraryRecent && d.lastUsed == 0) continue;
        if (m_view == View::LibraryTopRated && d.rating < 4) continue;
        if (m_view == View::LibraryMissing && !f.missing) continue;
        if (m_view == View::LibraryFolder && library.folderOf(f.path) != m_libraryFolderName) continue;
        const QStringList tags = library.tags(f);
        // Any of the picked tags within a category; every category must match.
        bool match = true;
        for (auto it = m_libraryFilters.begin(); it != m_libraryFilters.end() && match; ++it) {
            match = std::any_of(it.value().begin(), it.value().end(), [&tags](const QString& t) { return tags.contains(t); });
        }
        if (!match) continue;
        const QString name = library.displayName(f);
        const CaptureLibrary::Pack filePack = library.pack(library.packOf(f.path));
        const QString creator = !filePack.creator.isEmpty() && f.tone.isEmpty() ? filePack.creator : library.creator(f);
        const Tone3000::ToneItem tone = Tone3000::ToneItem::fromJson(f.tone);
        if (!words.isEmpty()) {
            const QString text = QStringList{name, f.originalName, creator, tone.title, tags.join(' '), d.notes,
                                             d.gear.make, d.gear.model, f.nam.gearMake, f.nam.gearModel}.join(' ');
            if (!std::all_of(words.begin(), words.end(), [&text](const QString& w) { return text.contains(w, Qt::CaseInsensitive); })) {
                continue;
            }
        }
        QJsonObject raw = f.tone.isEmpty() ? QJsonObject{{"id", f.toneId}} : f.tone;
        raw["models"] = f.models;
        const CaptureLibrary::Pack pack = library.pack(library.packOf(f.path));
        if (!pack.id.isEmpty()) {
            // In the user's pack: its captures are the variants, its words the description.
            raw["models"] = packMembers.value(pack.id);
            raw["rigroom_pack"] = pack.id;
            raw["rigroom_pack_title"] = pack.title;
            if (!pack.description.isEmpty() || f.tone.isEmpty()) raw["description"] = pack.description;
        }
        raw["rigroom_key"] = f.path;
        raw["rigroom_tags"] = QJsonArray::fromStringList(tags.mid(0, 6));
        raw["rigroom_rating"] = d.rating;
        raw["rigroom_missing"] = f.missing;
        // Second line: where it comes from, or what the file says it is.
        QStringList sub;
        if (!pack.id.isEmpty() && m_libraryGroup != "tone") sub << pack.title;
        else if (!tone.title.isEmpty() && m_libraryGroup != "tone") sub << tone.title;
        if (!creator.isEmpty() && m_libraryGroup != "creator") sub << creator;
        if (tone.title.isEmpty()) {
            const QString make = !d.gear.make.isEmpty() ? QStringList{d.gear.make, d.gear.model}.join(' ').trimmed()
                                                        : f.nam.gearMake.trimmed();
            if (!make.isEmpty()) sub.prepend(make);
        }
        if (!d.notes.isEmpty()) sub << QString::fromUtf8("✎ ") + d.notes.section('\n', 0, 0).left(60);
        raw["rigroom_sub"] = sub.join(QString::fromUtf8("  ·  "));

        Row row;
        row.item = ToneItem::fromJson(raw);
        row.item.title = name;
        row.item.creator = creator;
        row.rating = d.rating;
        row.used = d.lastUsed;
        row.added = d.addedAt;
        if (m_libraryGroup == "tone" && !pack.id.isEmpty()) {
            row.groupKey = "pack:" + pack.id;
            row.groupTitle = pack.title;
            row.groupSubtitle = !pack.creator.isEmpty() ? pack.creator : creator;
            row.groupImage = tone.imageUrl;
        } else if (m_libraryGroup == "tone") {
            row.groupKey = f.toneId != 0 ? QString("tone:%1").arg(f.toneId) : (f.inCache ? "other" : "own");
            row.groupTitle = f.toneId != 0 && !tone.title.isEmpty() ? tone.title
                           : (f.inCache ? "Other downloads" : "Your own files");
            row.groupSubtitle = f.toneId != 0 ? creator : QString();
            row.groupImage = tone.imageUrl;
        } else if (m_libraryGroup == "creator") {
            row.groupKey = "creator:" + creator.toLower();
            row.groupTitle = creator.isEmpty() ? "Unknown creator" : creator;
        } else if (m_libraryGroup == "folder") {
            row.groupKey = "folder:" + library.folderOf(f.path);
            row.groupTitle = library.folderOf(f.path);
        }
        rows.push_back(std::move(row));
    }

    const QString sort = m_view == View::LibraryRecent ? QStringLiteral("used") : m_librarySort;
    std::stable_sort(rows.begin(), rows.end(), [&sort](const Row& a, const Row& b) {
        if (sort == "rating" && a.rating != b.rating) return a.rating > b.rating;
        if (sort == "used" && a.used != b.used) return a.used > b.used;
        if (sort == "added" && a.added != b.added) return a.added > b.added;
        return a.item.title.compare(b.item.title, Qt::CaseInsensitive) < 0;
    });

    std::vector<ToneItem> items;
    if (m_libraryGroup == "none") {
        for (Row& row : rows) items.push_back(std::move(row.item));
    } else {
        // Groups in name order, the user's own files last; rows keep their order inside.
        QStringList order;
        QHash<QString, std::vector<Row*>> groups;
        for (Row& row : rows) {
            if (!groups.contains(row.groupKey)) order << row.groupKey;
            groups[row.groupKey].push_back(&row);
        }
        std::stable_sort(order.begin(), order.end(), [&groups](const QString& a, const QString& b) {
            const bool lastA = a == "own" || a == "other";
            const bool lastB = b == "own" || b == "other";
            if (lastA != lastB) return lastB;
            return groups[a].front()->groupTitle.compare(groups[b].front()->groupTitle, Qt::CaseInsensitive) < 0;
        });
        auto groupedItem = [](const std::vector<Row*>& members) {
            // The row stands for the tone or pack; the preferred file is the one
            // used last, else the highest rated, else the first in the sort.
            Row* best = members.front();
            for (Row* row : members) {
                if (row->used != best->used) {
                    if (row->used > best->used) best = row;
                } else if (row->rating > best->rating) {
                    best = row;
                }
            }
            ToneItem item = best->item;
            item.title = members.front()->groupTitle;
            item.creator = members.front()->groupSubtitle;
            if (item.imageUrl.isEmpty()) item.imageUrl = members.front()->groupImage;
            item.modelCount = static_cast<int>(members.size());
            QJsonArray keys;
            QJsonArray tags;
            QStringList seenTags;
            int rating = 0;
            bool missing = false;
            for (Row* row : members) {
                const QString path = row->item.raw.value("rigroom_key").toString();
                if (!path.isEmpty()) keys.append(path);
                rating = std::max(rating, row->rating);
                missing = missing || row->item.raw.value("rigroom_missing").toBool();
                for (const QJsonValue& tag : row->item.raw.value("rigroom_tags").toArray()) {
                    const QString t = tag.toString();
                    if (t.isEmpty() || seenTags.contains(t) || tags.size() >= 6) continue;
                    seenTags << t;
                    tags.append(t);
                }
            }
            item.raw["rigroom_key"] = best->item.raw.value("rigroom_key");
            item.raw["rigroom_keys"] = keys;
            item.raw["rigroom_tags"] = tags;
            item.raw["rigroom_rating"] = rating;
            item.raw["rigroom_missing"] = missing;
            item.raw["rigroom_grouped"] = true;
            QStringList sub;
            if (!item.creator.isEmpty()) sub << item.creator;
            item.raw["rigroom_sub"] = sub.join(QString::fromUtf8("  ·  "));
            return item;
        };
        for (const QString& key : order) {
            const std::vector<Row*>& members = groups[key];
            const bool toneOrPack = key.startsWith("tone:") || key.startsWith("pack:");
            if (m_libraryGroup == "tone" && toneOrPack) {
                // One row per tone or pack, like the TONE3000 tab. Own files
                // that do not share a tone still get a section header below.
                if (members.size() > 1) items.push_back(groupedItem(members));
                else items.push_back(std::move(members.front()->item));
                continue;
            }
            const bool collapsed = m_collapsedGroups.contains(key);
            ToneItem header;
            header.title = members.front()->groupTitle;
            header.creator = members.front()->groupSubtitle;
            header.imageUrl = members.front()->groupImage;
            header.modelCount = static_cast<int>(members.size());
            header.raw = QJsonObject{{"rigroom_group", key}, {"rigroom_collapsed", collapsed}};
            items.push_back(header);
            if (collapsed) continue;
            for (Row* row : members) items.push_back(std::move(row->item));
        }
    }

    QString empty;
    if (!library.isScanned()) empty = QString::fromUtf8("Opening your library…");
    else if (files.isEmpty())
        empty = QString::fromUtf8("Your library is empty. Add captures with “+ Add”, or from the TONE3000 tab with "
                                  "“Add to Library”.");
    else if (m_libraryFilters.isEmpty() && words.isEmpty() && m_view == View::LibraryRecent)
        empty = "Nothing used yet. Captures you load from the library show up here.";
    else if (m_libraryFilters.isEmpty() && words.isEmpty() && m_view == View::LibraryTopRated)
        empty = QString::fromUtf8("Nothing rated four stars or more yet. Rate captures in the panel on the right.");
    else empty = "Nothing matches. Try other words or fewer filters.";
    const int shown = static_cast<int>(rows.size());
    int listed = 0;
    for (const ToneItem& item : items) {
        if (!item.raw.contains("rigroom_group")) ++listed;
    }
    m_model->setLocal(std::move(items), empty);
    const QString noun = m_mode == Mode::Ir ? (shown == 1 ? "IR" : "IRs")
                                            : (shown == 1 ? "capture" : "captures");
    if (m_libraryGroup == "tone" && listed > 0 && listed != shown) {
        m_libraryCount->setText(QString("%1 %2 · %3 rows").arg(shown).arg(noun).arg(listed));
    } else {
        m_libraryCount->setText(QString("%1 %2").arg(shown).arg(noun));
    }

    auto rowHasPath = [](const ToneItem* tone, const QString& path) {
        if (!tone || path.isEmpty()) return false;
        if (tone->raw.value("rigroom_key").toString() == path) return true;
        for (const QJsonValue& value : tone->raw.value("rigroom_keys").toArray()) {
            if (value.toString() == path) return true;
        }
        return false;
    };
    auto rowPaths = [](const ToneItem* tone) {
        QStringList paths;
        if (!tone) return paths;
        const QJsonArray grouped = tone->raw.value("rigroom_keys").toArray();
        if (!grouped.isEmpty()) {
            for (const QJsonValue& value : grouped) {
                const QString path = value.toString();
                if (!path.isEmpty()) paths << path;
            }
            return paths;
        }
        const QString path = tone->raw.value("rigroom_key").toString();
        if (!path.isEmpty()) paths << path;
        return paths;
    };

    // Keep the selection (one or several) and the place in the list across refreshes.
    if (!keep.isEmpty()) {
        for (int row = 0; row < m_model->itemCount(); ++row) {
            if (rowHasPath(m_model->tone(row), keep) || rowHasPath(m_model->tone(row), m_preferredVariantPath)) {
                m_list->setCurrentIndex(m_model->index(row));
                break;
            }
        }
    }
    if (keepSelected.size() > 1) {
        QItemSelection selection;
        for (int row = 0; row < m_model->itemCount(); ++row) {
            const QStringList paths = rowPaths(m_model->tone(row));
            if (std::any_of(paths.begin(), paths.end(),
                            [&keepSelected](const QString& path) { return keepSelected.contains(path); })) {
                selection.select(m_model->index(row), m_model->index(row));
            }
        }
        m_list->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    }
    m_list->verticalScrollBar()->setValue(scroll);
    updateLibraryChips();
}

QString Tone3000Dialog::selectedKey() const {
    return m_selected.raw.value("rigroom_key").toString();
}

QStringList Tone3000Dialog::selectedKeys() const {
    QStringList keys;
    if (!m_list || !m_list->selectionModel()) return keys;
    for (const QModelIndex& index : m_list->selectionModel()->selectedRows()) {
        if (const ToneItem* tone = m_model->tone(index.row())) {
            const QJsonArray grouped = tone->raw.value("rigroom_keys").toArray();
            if (!grouped.isEmpty()) {
                for (const QJsonValue& value : grouped) {
                    const QString key = value.toString();
                    if (!key.isEmpty() && !keys.contains(key)) keys << key;
                }
                continue;
            }
            const QString key = tone->raw.value("rigroom_key").toString();
            if (!key.isEmpty()) keys << key;
        }
    }
    return keys;
}

QString Tone3000Dialog::activeLibraryPath() const {
    const int row = currentVariant();
    if (row >= 0 && row < m_models.size()) {
        const QString local = m_models[row].toObject().value("local_path").toString();
        if (!local.isEmpty() && CaptureLibrary::instance().contains(local)) return local;
    }
    return selectedKey();
}

int Tone3000Dialog::selectedLibraryRows() const {
    if (!m_list || !m_list->selectionModel()) return 0;
    int count = 0;
    for (const QModelIndex& index : m_list->selectionModel()->selectedRows()) {
        const ToneItem* tone = m_model->tone(index.row());
        if (tone && !tone->raw.contains("rigroom_group") && !tone->raw.value("rigroom_key").toString().isEmpty()) ++count;
    }
    return count;
}

void Tone3000Dialog::tagSelected() {
    const QStringList keys = selectedKeys();
    if (keys.isEmpty()) return;
    QStringList known;
    const auto categories = CaptureLibrary::instance().tagsByCategory(m_format);
    for (const auto& list : categories) {
        for (const auto& [tag, count] : list) known << tag;
    }
    known.sort();
    bool ok = false;
    const QString tag = QInputDialog::getItem(this, "Tag Captures", QString("Tag for %1 captures:").arg(keys.size()),
                                              known, -1, true, &ok);
    if (ok && !tag.trimmed().isEmpty()) CaptureLibrary::instance().addTag(keys, tag);
}

void Tone3000Dialog::removeFromLibrary(const QStringList& paths) {
    if (paths.isEmpty()) return;
    CaptureLibrary& library = CaptureLibrary::instance();
    bool anyDownloaded = false;
    for (const QString& path : paths) {
        if (const CaptureLibrary::File* f = library.file(path); f && f->inCache) anyDownloaded = true;
    }
    const CaptureLibrary::File* first = library.file(paths.first());
    QMessageBox box(QMessageBox::Question, "Remove from Library",
                    paths.size() == 1 ? QString("Remove \"%1\" from your library?")
                                            .arg(first ? library.displayName(*first) : QFileInfo(paths.first()).fileName())
                                      : QString("Remove %1 captures from your library?").arg(paths.size()),
                    QMessageBox::Yes | QMessageBox::Cancel, this);
    box.setInformativeText("Your own files stay where they are.");
    QCheckBox* deleteFiles = nullptr;
    if (anyDownloaded) {
        deleteFiles = new QCheckBox("Also delete downloaded files from this computer", &box);
        box.setCheckBox(deleteFiles);
    }
    if (box.exec() != QMessageBox::Yes) return;
    library.remove(paths, deleteFiles && deleteFiles->isChecked());
    if (paths.contains(selectedKey())) {
        m_selected = ToneItem();
        m_models = QJsonArray();
        showDetails();
    }
}

void Tone3000Dialog::openInLibrary(int toneId) {
    m_tabFromSettings = true;
    m_libraryFilters.clear();
    m_libraryView = View::LibraryAll;
    {
        // Show the capture itself, not whatever the library was filtered by.
        const QSignalBlocker blocker(m_search);
        if (m_tab == Tab::Library) m_search->clear();
        else m_otherTabSearch.clear();
    }
    m_collapsedGroups.remove(QString("tone:%1").arg(toneId));
    setTab(Tab::Library);
    if (!isLibraryView()) {
        m_view = View::LibraryAll;
        runView();
    }
    for (int row = 0; row < m_model->itemCount(); ++row) {
        const ToneItem* tone = m_model->tone(row);
        if (tone->id == toneId && tone->raw.contains("rigroom_key")) {
            m_list->setCurrentIndex(m_model->index(row));
            m_list->scrollTo(m_model->index(row), QAbstractItemView::PositionAtCenter);
            break;
        }
    }
}

void Tone3000Dialog::openToneOnline() {
    const QString creator = m_selected.creator;
    const QString url = m_selected.raw.value("url").toString();
    if (creator.isEmpty() || url.isEmpty()) {
        if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(Tone3000::absoluteToneUrl(url)));
        return;
    }
    // The creator's uploads, with this tone picked once it shows up.
    m_initialToneUrl = url;
    showCreator(creator);
}

// ─── Adding ──────────────────────────────────────────────────────────────────

void Tone3000Dialog::addFilesToLibrary() {
    const QString filter = m_mode == Mode::Ir ? "Impulse responses (*.wav *.flac *.aif *.aiff)" : "NAM captures (*.nam)";
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Add to Library", QDir::homePath(), filter);
    if (!paths.isEmpty()) askToAdd(paths, "Add Files to Library");
}

void Tone3000Dialog::addFolderToLibrary() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Add a Folder to the Library", QDir::homePath());
    if (folder.isEmpty()) return;
    const QStringList paths = CaptureLibrary::captureFilesIn(folder, m_format);
    if (paths.isEmpty()) {
        setStatus(m_mode == Mode::Ir ? "No impulse responses in that folder." : "No .nam files in that folder.", true);
        return;
    }
    askToAdd(paths, "Add " + QFileInfo(folder).fileName() + " to Library");
}

void Tone3000Dialog::askToAdd(const QStringList& paths, const QString& title) {
    // Reading the files' metadata takes a moment for big folders; not on the GUI thread.
    setStatus(QString::fromUtf8("Reading %1 files…").arg(paths.size()));
    auto* watcher = new QFutureWatcher<CaptureLibrary::ScanResult>(this);
    connect(watcher, &QFutureWatcher<CaptureLibrary::ScanResult>::finished, this, [this, watcher, title]() {
        const CaptureLibrary::ScanResult result = watcher->result();
        watcher->deleteLater();
        setStatus({});
        QList<CaptureLibrary::File> files;
        for (const CaptureLibrary::File& f : result.files) {
            if (!CaptureLibrary::instance().contains(f.path)) files << f;
        }
        if (files.isEmpty()) {
            setStatus("They are all in your library already.");
            return;
        }
        showAddDialog(files, title);
    });
    const QString cacheDir = QFileInfo(CaptureLibrary::instance().cacheDir(Tone3000::Format::Nam)).absoluteFilePath();
    watcher->setFuture(QtConcurrent::run([paths, cacheDir]() {
        return CaptureLibrary::scan({cacheDir, paths, {}});
    }));
}

void Tone3000Dialog::showAddDialog(const QList<CaptureLibrary::File>& files, const QString& title) {
    QList<AddToLibraryDialog::Row> rows;
    for (const CaptureLibrary::File& f : files) {
        const QString tone = f.tone.value("title").toString();
        const QString creator = CaptureLibrary::instance().creator(f);
        QString group = !tone.isEmpty() ? tone + (creator.isEmpty() ? QString() : QString::fromUtf8("  ·  ") + creator)
                                        : QFileInfo(f.path).absoluteDir().dirName();
        QStringList detail;
        for (const QString& tag : f.autoTags.mid(0, 4)) detail << NamMetadata::tagLabel(tag);
        rows.append({f.path, group, f.originalName, detail.join(", ")});
    }
    AddToLibraryDialog dialog(title, rows, m_format, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QStringList paths = dialog.selectedKeys();
    CaptureLibrary::AddOptions options = dialog.options();
    if (options.pack.startsWith("new:")) options.pack = CaptureLibrary::instance().createPack(options.pack.mid(4));
    CaptureLibrary::instance().add(paths, options);
    setStatus(QString("Added %1 capture%2 to %3.").arg(paths.size()).arg(paths.size() == 1 ? "" : "s", options.folder));
}

void Tone3000Dialog::importEarlierDownloads() {
    const QList<CaptureLibrary::File> candidates = CaptureLibrary::instance().importCandidates(m_format);
    if (candidates.isEmpty()) {
        setStatus("Every earlier download is already in your library.");
        return;
    }
    showAddDialog(candidates, "Add Earlier Downloads");
}

void Tone3000Dialog::addVariantsToLibrary(const QList<int>& indices, const CaptureLibrary::AddOptions& options) {
    for (const int index : indices) {
        if (index < 0 || index >= m_models.size()) continue;
        m_addQueue.append({m_selected.raw, index});
    }
    m_addQueueModels = m_models;
    m_addOptions = options;
    if (!m_addTransfer.reply) addNextQueued();
}

void Tone3000Dialog::addNextQueued() {
    CaptureLibrary& library = CaptureLibrary::instance();
    while (!m_addQueue.isEmpty()) {
        const auto [tone, index] = m_addQueue.takeFirst();
        const QJsonObject model = m_addQueueModels[index].toObject();
        // Already on disk: just add it (keeping the tone's details next to it).
        QString local = model.value("local_path").toString();
        if (local.isEmpty() && tone.value("id").toInt() != 0) {
            const QString path = library.cacheDir(m_format) + "/" + Tone3000::toneFolderName(tone) + "/"
                + Tone3000::modelFileName(model, m_format, false);
            if (QFileInfo(path).size() > 0) local = path;
        }
        if (!local.isEmpty()) {
            if (library.contains(local)) continue;
            if (tone.value("id").toInt() != 0) library.recordDownload(tone, m_addQueueModels, m_format);
            library.add({local}, m_addOptions);
            setStatus(QString("Added %1 to %2.").arg(model.value("name").toString(), library.lastFolder()));
            continue;
        }
        const QString url = model.value("model_url").toString();
        if (url.isEmpty()) continue;
        const QString folder = library.cacheDir(m_format) + "/" + Tone3000::toneFolderName(tone);
        QDir().mkpath(folder);
        m_addTransfer = Transfer{};
        m_addTransfer.path = folder + "/" + Tone3000::modelFileName(model, m_format, false);
        m_addTransfer.tone = tone;
        m_addTransfer.models = m_addQueueModels;
        m_addTransfer.modelIndex = index;
        m_progress->setRange(0, 0);
        m_progress->show();
        setStatus(QString::fromUtf8("Downloading %1 for your library…").arg(model.value("name").toString()));
        startTransfer(m_addTransfer, QUrl(url), TransferKind::Add);
        updateAddButtons();
        return;
    }
    updateAddButtons();
}

void Tone3000Dialog::updateAddButtons() {
    if (!m_addToLibraryButton) return;
    const CaptureLibrary& library = CaptureLibrary::instance();
    int missing = 0;
    for (int i = 0; i < m_models.size(); ++i) {
        const QString local = localPathFor(m_models[i].toObject());
        if (local.isEmpty() || !library.contains(local)) ++missing;
    }
    const int current = currentVariant();
    const QString local = current >= 0 ? localPathFor(m_models[current].toObject()) : QString();
    const bool currentIn = !local.isEmpty() && library.contains(local);
    const bool busy = m_addTransfer.reply || !m_addQueue.isEmpty();
    m_addToLibraryButton->setVisible(current >= 0 && !currentIn);
    m_addToLibraryButton->setEnabled(!busy);
    // On a library row the other files only count once the user shows them.
    m_addAllButton->setVisible(missing > 1 && (selectedKey().isEmpty() || m_otherVariants));
    m_addAllButton->setEnabled(!busy);
    m_addAllButton->setText(QString("Add all %1").arg(missing));
}

// ─── Details ─────────────────────────────────────────────────────────────────

QWidget* Tone3000Dialog::buildLibraryInfo() {
    // Read-only summary by default; "Edit" swaps in the form below it.
    m_libraryInfo = new QWidget(this);
    auto* outer = new QVBoxLayout(m_libraryInfo);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    m_librarySummary = new QWidget(m_libraryInfo);
    outer->addWidget(m_librarySummary);
    m_libraryForm = new QWidget(m_libraryInfo);
    outer->addWidget(m_libraryForm);
    auto* layout = new QVBoxLayout(m_libraryForm);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(6);
    auto sectionLabel = [this](const QString& text) {
        auto* label = new QLabel(text, m_libraryInfo);
        label->setStyleSheet("color: #8A8A96; font-size: 10px; font-weight: bold; margin-top: 4px;");
        return label;
    };

    // Edits wait here until saved; leaving the capture asks what to do with them.
    m_editBar = new QWidget(m_libraryInfo);
    auto* bar = new QHBoxLayout(m_editBar);
    bar->setContentsMargins(0, 8, 0, 0);
    auto* unsaved = new QLabel(m_editBar);
    unsaved->setObjectName("unsaved");
    unsaved->setStyleSheet("color: #8A8A96; font-size: 11px;");
    auto* discard = new QPushButton("Cancel", m_editBar);
    auto* save = new QPushButton("Save", m_editBar);
    discard->setStyleSheet(BrowserStyle::kSecondaryButton);
    save->setStyleSheet(BrowserStyle::kPrimaryButton);
    discard->setAutoDefault(false);
    save->setAutoDefault(false);
    connect(discard, &QPushButton::clicked, this, [this]() {
        m_edit = {};
        m_editPath.clear();
        m_editing = false;
        showLibraryDetails();
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const CaptureLibrary::Changes changes = m_edit;
        const QString path = m_editPath;
        m_edit = {};
        m_editPath.clear();
        m_editing = false;
        if (!changes.isEmpty()) CaptureLibrary::instance().apply({path}, changes);
        showLibraryDetails();
        setStatus("Saved.");
    });
    bar->addWidget(unsaved, 1);
    bar->addWidget(discard);
    bar->addWidget(save);

    // Rating: five stars; clicking the lit one again clears it.
    auto* ratingRow = new QHBoxLayout();
    ratingRow->setSpacing(0);
    for (int i = 1; i <= 5; ++i) {
        auto* star = new QToolButton(m_libraryInfo);
        star->setCursor(Qt::PointingHandCursor);
        star->setAutoRaise(true);
        star->setToolTip(QString("Rate %1").arg(i));
        connect(star, &QToolButton::clicked, this, [this, i]() {
            const int current = m_edit.rating.value_or(CaptureLibrary::instance().userData(activeLibraryPath()).rating);
            m_edit.rating = current == i ? 0 : i;
            editChanged();
        });
        m_ratingButtons << star;
        ratingRow->addWidget(star);
    }
    ratingRow->addStretch();
    layout->addLayout(ratingRow);

    layout->addWidget(sectionLabel("FOLDER"));
    m_folderCombo = new QComboBox(m_libraryInfo);
    m_folderCombo->setStyleSheet("font-size: 12px;");
    connect(m_folderCombo, &QComboBox::activated, this, [this](int index) {
        QString folder = m_folderCombo->itemData(index).toString();
        if (folder.isEmpty()) folder = askNewFolder();
        if (!folder.isEmpty()) m_edit.folder = folder;
        editChanged();
    });
    layout->addWidget(m_folderCombo);

    layout->addWidget(sectionLabel("PACK"));
    auto* packRow = new QHBoxLayout();
    m_packCombo = new QComboBox(m_libraryInfo);
    m_packCombo->setStyleSheet("font-size: 12px;");
    m_packCombo->setToolTip("Captures that belong together, like the variants of a TONE3000 tone");
    connect(m_packCombo, &QComboBox::activated, this, [this](int index) {
        QString id = m_packCombo->itemData(index).toString();
        if (id == "__new") id = choosePack(m_selected.title);
        if (id != "__new") m_edit.pack = id;
        editChanged();
    });
    auto* editPackButton = new QToolButton(m_libraryInfo);
    editPackButton->setText(QString::fromUtf8("Edit…"));
    editPackButton->setToolTip("Name, creator and description of the pack");
    editPackButton->setStyleSheet("QToolButton { color: #00B0FF; border: none; background: transparent; font-size: 11px; }");
    connect(editPackButton, &QToolButton::clicked, this, [this]() {
        editPack(CaptureLibrary::instance().packOf(activeLibraryPath()));
    });
    packRow->addWidget(m_packCombo, 1);
    packRow->addWidget(editPackButton);
    layout->addLayout(packRow);

    layout->addWidget(sectionLabel("NAME"));
    m_nameEdit = new QLineEdit(m_libraryInfo);
    m_nameEdit->setStyleSheet("QLineEdit { padding: 4px 8px; font-size: 12px; }");
    m_nameEdit->setToolTip("Your name for this file; clear it to use the original");
    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_edit.name = text;
        editChanged();
    });
    layout->addWidget(m_nameEdit);

    layout->addWidget(sectionLabel("TAGS"));
    m_libraryTagsLabel = new QLabel(m_libraryInfo);
    m_libraryTagsLabel->setTextFormat(Qt::RichText);
    m_libraryTagsLabel->setWordWrap(true);
    m_libraryTagsLabel->setStyleSheet("font-size: 11px;");
    m_libraryTagsLabel->setToolTip("Click a tag to filter by it, × to remove it from this file");
    connect(m_libraryTagsLabel, &QLabel::linkActivated, this, [this](const QString& link) {
        const QString tag = QUrl::fromPercentEncoding(link.mid(link.indexOf(':') + 1).toUtf8());
        if (link.startsWith("filter:")) addTagFilter(tag);
        else if (link.startsWith("remove:")) {
            if (m_edit.addTags.contains(tag)) m_edit.addTags.removeAll(tag);
            else if (!m_edit.removeTags.contains(tag)) m_edit.removeTags << tag;
            editChanged();
        }
    });
    layout->addWidget(m_libraryTagsLabel);
    m_addTagEdit = new QLineEdit(m_libraryInfo);
    m_addTagEdit->setPlaceholderText(QString::fromUtf8("+ Add a tag…"));
    m_addTagEdit->setStyleSheet("QLineEdit { padding: 4px 8px; font-size: 12px; }");
    auto* completer = new QCompleter(new QStringListModel(m_addTagEdit), m_addTagEdit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_addTagEdit->setCompleter(completer);
    connect(m_addTagEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString tag = m_addTagEdit->text().trimmed();
        if (tag.isEmpty()) return;
        m_addTagEdit->clear();
        const QString normalized = NamMetadata::normalizeTag(tag);
        if (normalized.isEmpty()) return;
        m_edit.removeTags.removeAll(normalized);
        if (!m_edit.addTags.contains(normalized)) m_edit.addTags << normalized;
        editChanged();
    });
    layout->addWidget(m_addTagEdit);

    layout->addWidget(sectionLabel("GEAR"));
    auto* gear = new QFormLayout();
    gear->setContentsMargins(0, 0, 0, 0);
    gear->setHorizontalSpacing(8);
    gear->setVerticalSpacing(4);
    m_gearType = new QComboBox(m_libraryInfo);
    m_gearType->addItem("From the file", QString());
    const QStringList types = m_mode == Mode::Ir ? QStringList{"cab", "space", "pedal", "outboard"}
                                                 : QStringList{"amp-cab", "amp", "pedal", "outboard"};
    for (const QString& type : types) m_gearType->addItem(NamMetadata::tagLabel(type), type);
    m_gearMake = new QLineEdit(m_libraryInfo);
    m_gearModel = new QLineEdit(m_libraryInfo);
    m_gearTone = new QComboBox(m_libraryInfo);
    m_gearTone->addItem("From the file", QString());
    for (const QString& tone : NamMetadata::vocabulary(TagCategory::Tone)) m_gearTone->addItem(NamMetadata::tagLabel(tone), tone);
    for (QWidget* w : std::initializer_list<QWidget*>{m_gearType, m_gearMake, m_gearModel, m_gearTone}) {
        w->setStyleSheet("font-size: 12px;");
    }
    auto formLabel = [this](const QString& text) {
        auto* label = new QLabel(text, m_libraryInfo);
        label->setStyleSheet("color: #9FA8B8; font-size: 12px;");
        return label;
    };
    gear->addRow(formLabel("Type"), m_gearType);
    gear->addRow(formLabel("Make"), m_gearMake);
    gear->addRow(formLabel("Model"), m_gearModel);
    gear->addRow(formLabel("Tone"), m_gearTone);
    for (QComboBox* combo : {m_gearType, m_gearTone}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(10);
    }
    connect(m_gearType, &QComboBox::activated, this, &Tone3000Dialog::saveGearEdits);
    connect(m_gearTone, &QComboBox::activated, this, &Tone3000Dialog::saveGearEdits);
    connect(m_gearMake, &QLineEdit::textEdited, this, &Tone3000Dialog::saveGearEdits);
    connect(m_gearModel, &QLineEdit::textEdited, this, &Tone3000Dialog::saveGearEdits);
    layout->addLayout(gear);

    layout->addWidget(sectionLabel("NOTES"));
    m_notesEdit = new QPlainTextEdit(m_libraryInfo);
    m_notesEdit->setPlaceholderText(QString::fromUtf8("Anything worth remembering: settings, songs, what it pairs with…"));
    m_notesEdit->setFixedHeight(70);
    m_notesEdit->setStyleSheet("QPlainTextEdit { background: #1F1F24; color: #EDEDF2; border: 1px solid #34343C; "
                               "border-radius: 6px; font-size: 12px; } QPlainTextEdit:focus { border-color: #00B0FF; }");
    connect(m_notesEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_notesEdit->signalsBlocked()) return;
        m_edit.notes = m_notesEdit->toPlainText();
        editChanged();
    });
    layout->addWidget(m_notesEdit);

    layout->addWidget(m_editBar);

    // The summary: what a capture is, in a few quiet lines.
    auto* summary = new QVBoxLayout(m_librarySummary);
    summary->setContentsMargins(0, 6, 0, 4);
    summary->setSpacing(8);
    m_summaryTags = new QLabel(m_librarySummary);
    m_summaryTags->setTextFormat(Qt::RichText);
    m_summaryTags->setWordWrap(true);
    m_summaryTags->setStyleSheet("font-size: 11px;");
    connect(m_summaryTags, &QLabel::linkActivated, this, [this](const QString& link) {
        addTagFilter(QUrl::fromPercentEncoding(link.mid(link.indexOf(':') + 1).toUtf8()));
    });
    summary->addWidget(m_summaryTags);
    m_factsLabel = new QLabel(m_librarySummary);
    m_factsLabel->setWordWrap(true);
    m_factsLabel->setTextFormat(Qt::RichText);
    m_factsLabel->setStyleSheet("color: #B8B8C4; font-size: 12px;");
    m_factsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->addWidget(m_factsLabel);
    m_summaryNotes = new QLabel(m_librarySummary);
    m_summaryNotes->setWordWrap(true);
    m_summaryNotes->setStyleSheet("color: #C8C8D0; font-size: 12px; font-style: italic;");
    m_summaryNotes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->addWidget(m_summaryNotes);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    auto* edit = new QPushButton(QString::fromUtf8("Edit…"), m_librarySummary);
    edit->setToolTip("Name, rating, tags, gear, notes, folder and pack of this capture");
    edit->setStyleSheet(BrowserStyle::kSecondaryButton);
    edit->setAutoDefault(false);
    connect(edit, &QPushButton::clicked, this, [this]() {
        m_editing = true;
        m_editingPath = activeLibraryPath();
        showLibraryDetails();
    });
    m_showFolderButton = new QPushButton(QString::fromUtf8("More  ▾"), m_librarySummary);
    m_showFolderButton->setStyleSheet(BrowserStyle::kSecondaryButton);
    m_showFolderButton->setAutoDefault(false);
    auto* more = new QMenu(m_showFolderButton);
    connect(more->addAction("Show in Folder"), &QAction::triggered, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(activeLibraryPath()).absolutePath()));
    });
    connect(more->addAction(QString::fromUtf8("Edit Pack…")), &QAction::triggered, this, [this]() {
        editPack(CaptureLibrary::instance().packOf(activeLibraryPath()));
    });
    more->addSeparator();
    connect(more->addAction(QString::fromUtf8("Remove from Library…")), &QAction::triggered, this, [this]() {
        // One grouped row: remove the variant on the panel. Several rows: all their files.
        QStringList keys = selectedLibraryRows() > 1 ? selectedKeys() : QStringList{activeLibraryPath()};
        keys.removeAll(QString());
        if (keys.isEmpty()) keys = selectedKeys();
        removeFromLibrary(keys);
    });
    m_showFolderButton->setMenu(more);
    m_removeButton = nullptr;
    buttons->addWidget(edit);
    buttons->addWidget(m_showFolderButton);
    buttons->addStretch();
    summary->addLayout(buttons);
    m_libraryInfo->hide();
    return m_libraryInfo;
}

void Tone3000Dialog::saveGearEdits() {
    if (activeLibraryPath().isEmpty()) return;
    // Only fields that differ from what is saved count as changes.
    const CaptureLibrary::Gear saved = CaptureLibrary::instance().userData(activeLibraryPath()).gear;
    auto set = [](std::optional<QString>& change, const QString& value, const QString& savedValue) {
        if (value == savedValue) change.reset();
        else change = value;
    };
    set(m_edit.type, m_gearType->currentData().toString(), saved.type);
    set(m_edit.make, m_gearMake->text().trimmed(), saved.make);
    set(m_edit.model, m_gearModel->text().trimmed(), saved.model);
    set(m_edit.tone, m_gearTone->currentData().toString(), saved.tone);
    editChanged();
}

void Tone3000Dialog::editChanged() {
    m_editPath = activeLibraryPath();
    showLibraryDetails();
}

void Tone3000Dialog::resolvePendingEdits() {
    const bool single = !m_edit.isEmpty() && !m_editPath.isEmpty();
    const bool batch = !m_batchChanges.isEmpty() && !m_batchKeys.isEmpty();
    if (!single && !batch) return;
    const QString what = single ? QString("\"%1\"").arg(QFileInfo(m_editPath).completeBaseName())
                                : QString("%1 captures").arg(m_batchKeys.size());
    const auto answer = QMessageBox::question(this, "Unsaved Changes", QString("Save your changes to %1?").arg(what),
                                              QMessageBox::Save | QMessageBox::Discard, QMessageBox::Save);
    const CaptureLibrary::Changes changes = single ? m_edit : m_batchChanges;
    const QStringList paths = single ? QStringList{m_editPath} : m_batchKeys;
    m_edit = {};
    m_editPath.clear();
    m_batchChanges = {};
    if (answer == QMessageBox::Save) CaptureLibrary::instance().apply(paths, changes);
}

void Tone3000Dialog::showLibraryDetails() {
    if (!m_libraryInfo) return;
    const CaptureLibrary& library = CaptureLibrary::instance();
    const CaptureLibrary::File* f = library.file(activeLibraryPath());
    m_libraryInfo->setVisible(f != nullptr);
    if (!f) return;
    // The library shows its own tags; TONE3000's are part of them.
    m_tags->setVisible(false);
    const CaptureLibrary::UserData d = library.userData(f->path);
    // Unsaved edits show on top of what is saved.
    const CaptureLibrary::Changes pending = m_editPath == f->path ? m_edit : CaptureLibrary::Changes();
    // Editing ends when the panel moves to another capture.
    if (m_editing && m_editingPath != f->path) m_editing = false;
    m_libraryForm->setVisible(m_editing);
    m_librarySummary->setVisible(!m_editing);
    m_editBar->findChild<QLabel*>("unsaved")->setText(pending.isEmpty() ? QString() : QString("Unsaved changes"));

    const int rating = pending.rating.value_or(d.rating);
    for (int i = 0; i < m_ratingButtons.size(); ++i) {
        const bool lit = i < rating;
        m_ratingButtons[i]->setText(lit ? QString::fromUtf8("★") : QString::fromUtf8("☆"));
        m_ratingButtons[i]->setStyleSheet(QString("QToolButton { color: %1; font-size: 18px; border: none; padding: 0 2px; }")
                                              .arg(lit ? "#FFD54F" : "#4A4A54"));
    }
    {
        const QSignalBlocker blocker(m_folderCombo);
        m_folderCombo->clear();
        QStringList folders = library.folders();
        if (!pending.folder.isEmpty() && !folders.contains(pending.folder)) folders << pending.folder;
        for (const QString& folder : folders) m_folderCombo->addItem(folder, folder);
        m_folderCombo->addItem(QString::fromUtf8("New folder…"), QString());
        const QString folder = !pending.folder.isEmpty() ? pending.folder : library.folderOf(f->path);
        m_folderCombo->setCurrentIndex(std::max(0, m_folderCombo->findData(folder)));
    }
    {
        const QSignalBlocker blocker(m_packCombo);
        m_packCombo->clear();
        m_packCombo->addItem("No pack", QString());
        for (const CaptureLibrary::Pack& pack : library.packs()) m_packCombo->addItem(pack.title, pack.id);
        m_packCombo->addItem(QString::fromUtf8("New pack…"), QString("__new"));
        m_packCombo->setCurrentIndex(std::max(0, m_packCombo->findData(pending.pack.value_or(library.packOf(f->path)))));
    }
    if (!pending.name) {
        const QSignalBlocker blocker(m_nameEdit);
        m_nameEdit->setText(library.displayName(*f));
        m_nameEdit->setPlaceholderText(f->originalName);
    }

    // Tags: the user's own in green, ones about to be added marked +.
    QStringList html;
    QStringList tags = library.tags(*f);
    for (const QString& tag : pending.removeTags) tags.removeAll(tag);
    for (const QString& tag : tags) {
        const bool own = d.tags.contains(tag);
        QString text = shownTag(tag).toHtmlEscaped();
        text.replace(' ', "&nbsp;").replace('-', QChar(0x2011));
        html << QString("<span style='background:%1;'>&nbsp;<a href='filter:%3' style='color:%2; text-decoration:none;'>%4</a>"
                        "&nbsp;<a href='remove:%3' style='color:#6E6E7A; text-decoration:none;'>×</a>&nbsp;</span>")
                    .arg(own ? "#1D3A2F" : "#24242A", own ? "#9FE3C9" : "#B8B8C4",
                         QString(QUrl::toPercentEncoding(tag)), text);
    }
    for (const QString& tag : pending.addTags) {
        if (tags.contains(tag)) continue;
        html << QString("<span style='background:#1D3A2F;'>&nbsp;<span style='color:#9FE3C9;'>+&nbsp;%1</span>"
                        "&nbsp;<a href='remove:%2' style='color:#6E6E7A; text-decoration:none;'>×</a>&nbsp;</span>")
                    .arg(tag.toHtmlEscaped(), QString(QUrl::toPercentEncoding(tag)));
    }
    if (!pending.removeTags.isEmpty()) {
        html << QString("<span style='color:#F08080;'>removing: %1</span>").arg(pending.removeTags.join(", ").toHtmlEscaped());
    }
    m_libraryTagsLabel->setText(html.isEmpty() ? QString("<span style='color:#6E6E7A;'>No tags yet.</span>") : html.join(" "));
    QStringList known;
    for (const auto& list : library.tagsByCategory(m_format)) {
        for (const auto& [tag, count] : list) known << tag;
    }
    known.sort();
    static_cast<QStringListModel*>(m_addTagEdit->completer()->model())->setStringList(known);

    {
        const QSignalBlocker b1(m_gearType), b2(m_gearTone), b3(m_gearMake), b4(m_gearModel);
        m_gearType->setCurrentIndex(std::max(0, m_gearType->findData(pending.type.value_or(d.gear.type))));
        m_gearTone->setCurrentIndex(std::max(0, m_gearTone->findData(pending.tone.value_or(d.gear.tone))));
        if (!pending.make) m_gearMake->setText(d.gear.make);
        if (!pending.model) m_gearModel->setText(d.gear.model);
        // What the file itself says, as a hint.
        m_gearMake->setPlaceholderText(f->nam.gearMake.isEmpty() ? QString("e.g. Marshall") : f->nam.gearMake);
        m_gearModel->setPlaceholderText(f->nam.gearModel.isEmpty() ? QString("e.g. JCM800 2203") : f->nam.gearModel);
    }
    if (!pending.notes) {
        const QSignalBlocker blocker(m_notesEdit);
        m_notesEdit->setPlainText(d.notes);
    }

    QStringList facts;
    if (f->missing) facts << "<span style='color:#F08080;'>The file is missing.</span>";
    const QString arch = NamMetadata::architectureLabel(f->nam);
    if (!arch.isEmpty()) facts << arch;
    if (!f->nam.size.isEmpty()) facts << f->nam.size;
    if (f->nam.calibrated) facts << "calibrated";
    if (f->nam.loudness != 0) facts << QString("loudness %1 dB").arg(f->nam.loudness, 0, 'f', 1);
    if (!f->nam.modeledBy.isEmpty()) facts << "modeled by " + f->nam.modeledBy.toHtmlEscaped();
    QString text = facts.join(QString::fromUtf8(" · "));
    if (d.addedAt > 0) {
        text += (text.isEmpty() ? "" : "<br>")
            + QString("Added %1").arg(QLocale().toString(QDateTime::fromMSecsSinceEpoch(d.addedAt).date(), QLocale::ShortFormat));
    }
    text += "<br>" + QFileInfo(f->path).fileName().toHtmlEscaped();
    Q_UNUSED(text);
    // Summary: rating and tags, then a few label/value lines, then notes.
    QStringList chips;
    if (d.rating > 0) chips << QString("<span style='color:#FFD54F; font-size:13px;'>%1</span>").arg(QString(d.rating, QChar(0x2605)));
    for (const QString& tag : library.tags(*f)) {
        QString shown = shownTag(tag).toHtmlEscaped();
        shown.replace(' ', "&nbsp;").replace('-', QChar(0x2011));
        chips << QString("<a href='filter:%1' style='color:#B8B8C4; text-decoration:none; background:#24242A;'>&nbsp;%2&nbsp;</a>")
                     .arg(QString(QUrl::toPercentEncoding(tag)), shown);
    }
    m_summaryTags->setText(chips.join(" "));
    m_summaryTags->setVisible(!chips.isEmpty());
    auto line = [](const QString& label, const QString& value) {
        return QString("<tr><td style='color:#6E6E7A; padding-right:12px;'>%1</td><td>%2</td></tr>").arg(label, value.toHtmlEscaped());
    };
    QString rows;
    rows += line("Folder", library.folderOf(f->path));
    if (const CaptureLibrary::Pack pack = library.pack(library.packOf(f->path)); !pack.id.isEmpty()) rows += line("Pack", pack.title);
    const QString make = d.gear.make.isEmpty() ? f->nam.gearMake : d.gear.make;
    const QString model = d.gear.model.isEmpty() ? f->nam.gearModel : d.gear.model;
    // Files often repeat the make as the model; say it once.
    QString gear = make;
    if (!model.isEmpty() && model != make) {
        gear = make.isEmpty() || model.contains(make, Qt::CaseInsensitive) ? model : make + " " + model;
    }
    gear = gear.simplified();
    if (!gear.isEmpty()) rows += line("Gear", gear);
    QStringList file{arch, f->nam.size};
    if (f->nam.calibrated) file << "calibrated";
    if (f->nam.loudness != 0) file << QString("%1 dB").arg(f->nam.loudness, 0, 'f', 1);
    file.removeAll(QString());
    if (!file.isEmpty()) rows += line("Model", file.join(QString::fromUtf8(" · ")));
    if (!f->nam.modeledBy.isEmpty()) rows += line("Captured by", f->nam.modeledBy);
    if (d.addedAt > 0) rows += line("Added", QLocale().toString(QDateTime::fromMSecsSinceEpoch(d.addedAt).date(), QLocale::ShortFormat));
    rows += line("File", QFileInfo(f->path).fileName());
    m_factsLabel->setText((f->missing ? QString("<p style='color:#F08080;'>The file is missing.</p>") : QString())
                          + "<table cellspacing='0' cellpadding='1'>" + rows + "</table>");
    m_factsLabel->setToolTip(f->path);
    m_summaryNotes->setText(d.notes);
    m_summaryNotes->setVisible(!d.notes.isEmpty());

}

// ─── Packs ───────────────────────────────────────────────────────────────────

QString Tone3000Dialog::choosePack(const QString& suggestedTitle) {
    bool ok = false;
    const QString title = QInputDialog::getText(this, "New Pack", "Pack name (e.g. the amp or the capture set):",
                                                QLineEdit::Normal, suggestedTitle, &ok).trimmed();
    if (!ok || title.isEmpty()) return QStringLiteral("__new"); // cancelled
    return CaptureLibrary::instance().createPack(title);
}

void Tone3000Dialog::editPack(const QString& id) {
    CaptureLibrary& library = CaptureLibrary::instance();
    CaptureLibrary::Pack pack = library.pack(id);
    if (pack.id.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Edit Pack");
    dialog.setStyleSheet(BrowserStyle::kStyleSheet);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* title = new QLineEdit(pack.title, &dialog);
    auto* creator = new QLineEdit(pack.creator, &dialog);
    creator->setPlaceholderText("Who made the captures");
    auto* description = new QPlainTextEdit(pack.description, &dialog);
    description->setStyleSheet("QPlainTextEdit { background: #1F1F24; color: #EDEDF2; border: 1px solid #34343C; border-radius: 6px; }");
    description->setFixedHeight(110);
    form->addRow("Name", title);
    form->addRow("Creator", creator);
    form->addRow("Description", description);
    layout->addLayout(form);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* cancel = new QPushButton("Cancel", &dialog);
    auto* save = new QPushButton("Save", &dialog);
    save->setStyleSheet(BrowserStyle::kPrimaryButton);
    save->setDefault(true);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    dialog.resize(420, 300);
    if (dialog.exec() != QDialog::Accepted) return;
    pack.title = title->text();
    pack.creator = creator->text().trimmed();
    pack.description = description->toPlainText().trimmed();
    library.updatePack(pack);
}

// ─── Several files at once ───────────────────────────────────────────────────

QWidget* Tone3000Dialog::buildBatchInfo() {
    m_batchInfo = new QWidget(this);
    m_batchInfo->setMaximumWidth(390); // stays inside the info panel
    auto* layout = new QVBoxLayout(m_batchInfo);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    m_batchTitle = new QLabel(m_batchInfo);
    m_batchTitle->setWordWrap(true);
    m_batchTitle->setStyleSheet("font-size: 17px; font-weight: bold; color: #F2F2F5;");
    layout->addWidget(m_batchTitle);
    auto* hint = new QLabel(QString::fromUtf8("Changes go to every selected capture when you apply them. Fields left on "
                                              "“Keep as is” stay as each capture has them."), m_batchInfo);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #8A8A96; font-size: 11px;");
    layout->addWidget(hint);

    const QString keep = QString::fromUtf8("Keep as is");
    auto combo = [this]() {
        auto* c = new QComboBox(m_batchInfo);
        c->setStyleSheet("font-size: 12px;");
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(10);
        c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return c;
    };
    auto changed = [this]() {
        const CaptureLibrary::Changes& c = m_batchChanges;
        const bool any = !c.folder.isEmpty() || c.pack || c.rating || c.type || c.make || c.model || c.tone
            || !c.addTags.isEmpty() || !c.removeTags.isEmpty();
        m_batchApply->setEnabled(any);
        m_batchReset->setEnabled(any);
    };
    auto* form = new QFormLayout();
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    auto label = [this](const QString& text) {
        auto* l = new QLabel(text, m_batchInfo);
        l->setStyleSheet("color: #9FA8B8; font-size: 12px;");
        return l;
    };

    m_batchFolder = combo();
    connect(m_batchFolder, &QComboBox::activated, this, [this, changed](int index) {
        QString folder = m_batchFolder->itemData(index).toString();
        if (folder == "__new") {
            folder = askNewFolder();
            if (folder.isEmpty()) m_batchFolder->setCurrentIndex(0);
            else {
                m_batchFolder->insertItem(m_batchFolder->count() - 1, folder, folder);
                m_batchFolder->setCurrentIndex(m_batchFolder->count() - 2);
            }
        }
        m_batchChanges.folder = folder == "__keep" ? QString() : folder;
        changed();
    });
    m_batchPack = combo();
    connect(m_batchPack, &QComboBox::activated, this, [this, changed](int index) {
        QString id = m_batchPack->itemData(index).toString();
        if (id == "__new") {
            id = choosePack(QString());
            if (id == "__new") {
                m_batchPack->setCurrentIndex(0);
                id = "__keep";
            } else {
                m_batchPack->insertItem(m_batchPack->count() - 1, CaptureLibrary::instance().pack(id).title, id);
                m_batchPack->setCurrentIndex(m_batchPack->count() - 2);
            }
        }
        if (id == "__keep") m_batchChanges.pack.reset();
        else m_batchChanges.pack = id;
        changed();
    });
    m_batchRating = combo();
    m_batchRating->addItem(keep, -1);
    m_batchRating->addItem("No rating", 0);
    for (int stars = 1; stars <= 5; ++stars) m_batchRating->addItem(QString(stars, QChar(0x2605)), stars);
    connect(m_batchRating, &QComboBox::activated, this, [this, changed]() {
        const int value = m_batchRating->currentData().toInt();
        if (value < 0) m_batchChanges.rating.reset();
        else m_batchChanges.rating = value;
        changed();
    });
    auto choiceCombo = [&](const QStringList& values) {
        QComboBox* c = combo();
        c->addItem(keep, QString("__keep"));
        c->addItem("From each file", QString());
        for (const QString& v : values) c->addItem(NamMetadata::tagLabel(v), v);
        return c;
    };
    m_batchType = choiceCombo(m_mode == Mode::Ir ? QStringList{"cab", "space", "pedal", "outboard"}
                                                 : QStringList{"amp-cab", "amp", "pedal", "outboard"});
    m_batchTone = choiceCombo(NamMetadata::vocabulary(NamMetadata::TagCategory::Tone));
    connect(m_batchType, &QComboBox::activated, this, [this, changed]() {
        const QString v = m_batchType->currentData().toString();
        if (v == "__keep") m_batchChanges.type.reset();
        else m_batchChanges.type = v;
        changed();
    });
    connect(m_batchTone, &QComboBox::activated, this, [this, changed]() {
        const QString v = m_batchTone->currentData().toString();
        if (v == "__keep") m_batchChanges.tone.reset();
        else m_batchChanges.tone = v;
        changed();
    });
    m_batchMake = new QLineEdit(m_batchInfo);
    m_batchModel = new QLineEdit(m_batchInfo);
    connect(m_batchMake, &QLineEdit::textEdited, this, [this, changed](const QString& text) {
        m_batchChanges.make = text.trimmed();
        changed();
    });
    connect(m_batchModel, &QLineEdit::textEdited, this, [this, changed](const QString& text) {
        m_batchChanges.model = text.trimmed();
        changed();
    });
    form->addRow(label("Folder"), m_batchFolder);
    form->addRow(label("Pack"), m_batchPack);
    form->addRow(label("Rating"), m_batchRating);
    form->addRow(label("Type"), m_batchType);
    form->addRow(label("Tone"), m_batchTone);
    form->addRow(label("Make"), m_batchMake);
    form->addRow(label("Model"), m_batchModel);
    layout->addLayout(form);

    auto* tagsTitle = new QLabel("TAGS ON ALL OF THEM", m_batchInfo);
    tagsTitle->setStyleSheet("color: #8A8A96; font-size: 10px; font-weight: bold; margin-top: 6px;");
    layout->addWidget(tagsTitle);
    m_batchTags = new QLabel(m_batchInfo);
    m_batchTags->setTextFormat(Qt::RichText);
    m_batchTags->setWordWrap(true);
    m_batchTags->setStyleSheet("font-size: 11px;");
    connect(m_batchTags, &QLabel::linkActivated, this, [this, changed](const QString& link) {
        const QString tag = QUrl::fromPercentEncoding(link.mid(link.indexOf(':') + 1).toUtf8());
        if (link.startsWith("remove:")) m_batchChanges.removeTags << tag;
        else if (link.startsWith("unadd:")) m_batchChanges.addTags.removeAll(tag);
        showBatchDetails();
        changed();
    });
    layout->addWidget(m_batchTags);
    m_batchAddTag = new QLineEdit(m_batchInfo);
    m_batchAddTag->setPlaceholderText(QString::fromUtf8("+ Add a tag to all…"));
    m_batchAddTag->setStyleSheet("QLineEdit { padding: 4px 8px; font-size: 12px; }");
    connect(m_batchAddTag, &QLineEdit::returnPressed, this, [this, changed]() {
        const QString tag = NamMetadata::normalizeTag(m_batchAddTag->text());
        m_batchAddTag->clear();
        if (tag.isEmpty()) return;
        m_batchChanges.removeTags.removeAll(tag);
        if (!m_batchChanges.addTags.contains(tag)) m_batchChanges.addTags << tag;
        showBatchDetails();
        changed();
    });
    layout->addWidget(m_batchAddTag);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_batchReset = new QPushButton("Discard", m_batchInfo);
    m_batchReset->setStyleSheet(BrowserStyle::kSecondaryButton);
    m_batchReset->setAutoDefault(false);
    connect(m_batchReset, &QPushButton::clicked, this, [this]() {
        m_batchChanges = {};
        showBatchDetails();
    });
    m_batchApply = new QPushButton(m_batchInfo);
    m_batchApply->setStyleSheet(BrowserStyle::kPrimaryButton);
    m_batchApply->setAutoDefault(false);
    connect(m_batchApply, &QPushButton::clicked, this, [this]() {
        const QStringList keys = selectedKeys();
        const CaptureLibrary::Changes changes = m_batchChanges;
        m_batchChanges = {};
        CaptureLibrary::instance().apply(keys, changes);
        setStatus(QString("Changed %1 captures.").arg(keys.size()));
    });
    auto* remove = new QPushButton(QString::fromUtf8("Remove…"), m_batchInfo);
    remove->setStyleSheet(BrowserStyle::kSecondaryButton);
    remove->setAutoDefault(false);
    connect(remove, &QPushButton::clicked, this, [this]() { removeFromLibrary(selectedKeys()); });
    buttons->addStretch();
    buttons->addWidget(m_batchReset);
    buttons->addWidget(m_batchApply);
    layout->addLayout(buttons);
    auto* removeRow = new QHBoxLayout();
    removeRow->addWidget(remove);
    removeRow->addStretch();
    layout->addLayout(removeRow);
    layout->addStretch();
    m_batchInfo->hide();
    return m_batchInfo;
}

void Tone3000Dialog::showBatchDetails() {
    const CaptureLibrary& library = CaptureLibrary::instance();
    const QStringList keys = selectedKeys();
    m_batchKeys = keys;
    const CaptureLibrary::Changes& c = m_batchChanges;
    m_batchTitle->setText(QString("%1 captures selected").arg(keys.size()));

    // What they have in common shows as the current value; otherwise "mixed".
    auto common = [&](const std::function<QString(const QString&)>& value) {
        QString first;
        for (int i = 0; i < keys.size(); ++i) {
            const QString v = value(keys[i]);
            if (i == 0) first = v;
            else if (v != first) return QString(QChar(1)); // mixed
        }
        return first;
    };
    const QString mixed = QString(QChar(1));
    auto keepLabel = [&](const QString& value, const QString& empty) {
        return value == mixed ? QString::fromUtf8("Keep as is (mixed)")
                              : QString::fromUtf8("Keep as is (%1)").arg(value.isEmpty() ? empty : value);
    };

    {
        const QSignalBlocker b(m_batchFolder);
        m_batchFolder->clear();
        m_batchFolder->addItem(keepLabel(common([&](const QString& k) { return library.folderOf(k); }), "Imported"),
                               QString("__keep"));
        for (const QString& folder : library.folders()) m_batchFolder->addItem(folder, folder);
        m_batchFolder->addItem(QString::fromUtf8("New folder…"), QString("__new"));
        m_batchFolder->setCurrentIndex(c.folder.isEmpty() ? 0 : std::max(0, m_batchFolder->findData(c.folder)));
    }
    {
        const QSignalBlocker b(m_batchPack);
        m_batchPack->clear();
        const QString packId = common([&](const QString& k) { return library.packOf(k); });
        m_batchPack->addItem(keepLabel(packId == mixed ? mixed : library.pack(packId).title, "no pack"), QString("__keep"));
        m_batchPack->addItem("No pack", QString());
        for (const CaptureLibrary::Pack& pack : library.packs()) m_batchPack->addItem(pack.title, pack.id);
        m_batchPack->addItem(QString::fromUtf8("New pack…"), QString("__new"));
        m_batchPack->setCurrentIndex(c.pack ? std::max(0, m_batchPack->findData(*c.pack)) : 0);
    }
    {
        const QSignalBlocker b(m_batchRating);
        const QString rating = common([&](const QString& k) { return QString::number(library.userData(k).rating); });
        m_batchRating->setItemText(0, keepLabel(rating == mixed ? mixed
                                                : rating == "0" ? QString("no rating") : QString(rating.toInt(), QChar(0x2605)), ""));
        m_batchRating->setCurrentIndex(c.rating ? m_batchRating->findData(*c.rating) : 0);
    }
    auto setChoice = [&](QComboBox* combo, const std::optional<QString>& change,
                         const std::function<QString(const CaptureLibrary::UserData&)>& value) {
        const QSignalBlocker b(combo);
        const QString v = common([&](const QString& k) { return value(library.userData(k)); });
        combo->setItemText(0, keepLabel(v == mixed ? mixed : NamMetadata::tagLabel(v), "from each file"));
        combo->setCurrentIndex(change ? std::max(0, combo->findData(*change)) : 0);
    };
    setChoice(m_batchType, c.type, [](const CaptureLibrary::UserData& d) { return d.gear.type; });
    setChoice(m_batchTone, c.tone, [](const CaptureLibrary::UserData& d) { return d.gear.tone; });
    auto setText = [&](QLineEdit* edit, const std::optional<QString>& change,
                       const std::function<QString(const CaptureLibrary::UserData&)>& value) {
        const QString v = common([&](const QString& k) { return value(library.userData(k)); });
        if (!change) edit->setText(v == mixed ? QString() : v);
        edit->setPlaceholderText(v == mixed ? QString::fromUtf8("Mixed; type to set for all") : QString("From each file"));
    };
    setText(m_batchMake, c.make, [](const CaptureLibrary::UserData& d) { return d.gear.make; });
    setText(m_batchModel, c.model, [](const CaptureLibrary::UserData& d) { return d.gear.model; });

    // Tags every selected capture has, then the ones about to be added.
    QStringList shared;
    for (int i = 0; i < keys.size(); ++i) {
        const CaptureLibrary::File* f = library.file(keys[i]);
        const QStringList tags = f ? library.tags(*f) : QStringList();
        if (i == 0) shared = tags;
        else shared.erase(std::remove_if(shared.begin(), shared.end(), [&tags](const QString& t) { return !tags.contains(t); }),
                          shared.end());
    }
    QStringList html;
    for (const QString& tag : shared) {
        if (c.removeTags.contains(tag)) continue;
        html << QString("<span style='background:#24242A;'>&nbsp;<span style='color:#B8B8C4;'>%1</span>&nbsp;"
                        "<a href='remove:%2' style='color:#6E6E7A; text-decoration:none;'>×</a>&nbsp;</span>")
                    .arg(shownTag(tag).toHtmlEscaped(), QString(QUrl::toPercentEncoding(tag)));
    }
    for (const QString& tag : c.addTags) {
        html << QString("<span style='background:#1D3A2F;'>&nbsp;<span style='color:#9FE3C9;'>+ %1</span>&nbsp;"
                        "<a href='unadd:%2' style='color:#6E6E7A; text-decoration:none;'>×</a>&nbsp;</span>")
                    .arg(tag.toHtmlEscaped(), QString(QUrl::toPercentEncoding(tag)));
    }
    if (!c.removeTags.isEmpty()) {
        html << QString("<span style='color:#F08080;'>removing: %1</span>").arg(c.removeTags.join(", ").toHtmlEscaped());
    }
    m_batchTags->setText(html.isEmpty() ? QString("<span style='color:#6E6E7A;'>No tag is on all of them.</span>")
                                        : html.join(" "));
    m_batchApply->setText(QString("Apply to %1").arg(keys.size()));
    const bool any = !c.folder.isEmpty() || c.pack || c.rating || c.type || c.make || c.model || c.tone
        || !c.addTags.isEmpty() || !c.removeTags.isEmpty();
    m_batchApply->setEnabled(any);
    m_batchReset->setEnabled(any);
}

// ─── Dragging into folders ───────────────────────────────────────────────────

void Tone3000Dialog::highlightDropTarget(QListWidgetItem* item) {
    if (m_dropTarget == item) return;
    // The sidebar's style sheet paints rows, so the target is shown as the
    // selected row in a "dropping" style; the view's own row comes back after.
    const QSignalBlocker blocker(m_sidebar);
    if (m_dropTarget) m_dropTarget->setText(m_dropTarget->text().mid(2));
    if (!m_dropTarget && item) m_sidebarRowBeforeDrag = m_sidebar->currentRow();
    m_dropTarget = item;
    m_sidebar->setProperty("dropping", item != nullptr);
    m_sidebar->style()->unpolish(m_sidebar);
    m_sidebar->style()->polish(m_sidebar);
    if (item) {
        item->setText(QString::fromUtf8("⤵ ") + item->text());
        m_sidebar->setCurrentItem(item);
    } else {
        m_sidebar->setCurrentRow(m_sidebarRowBeforeDrag);
    }
}

void Tone3000Dialog::libraryDragMoved(const QPoint& global) {
    const QStringList keys = selectedKeys();
    if (!m_dragBadge) {
        m_dragBadge = new QLabel(this);
        m_dragBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dragBadge->setStyleSheet("QLabel { background: rgba(11, 79, 108, 215); color: white; border: 1px solid #00B0FF; "
                                   "border-radius: 8px; padding: 4px 10px; font-size: 12px; font-weight: bold; }");
    }
    if (!m_dragBadge->isVisible()) {
        QString text = keys.size() == 1 ? m_selected.title : QString("%1 captures").arg(keys.size());
        text = m_dragBadge->fontMetrics().elidedText(text, Qt::ElideRight, 220);
        m_dragBadge->setText(QString::fromUtf8("⇢ ") + text);
        m_dragBadge->adjustSize();
        m_dragBadge->show();
        m_dragBadge->raise();
    }
    // Beside the pointer, so the folder under it stays visible.
    m_dragBadge->move(mapFromGlobal(global) + QPoint(16, 12));

    QListWidgetItem* item = m_sidebar->itemAt(m_sidebar->viewport()->mapFromGlobal(global));
    const bool onFolder = item && item->data(Qt::UserRole).toInt() == static_cast<int>(View::LibraryFolder);
    highlightDropTarget(onFolder ? item : nullptr);
    const Qt::CursorShape shape = onFolder ? Qt::DragMoveCursor : Qt::ForbiddenCursor;
    if (!QGuiApplication::overrideCursor()) QGuiApplication::setOverrideCursor(shape);
    else QGuiApplication::changeOverrideCursor(shape);
}

void Tone3000Dialog::libraryDragEnded(const QPoint& global, bool drop) {
    QListWidgetItem* item = m_dropTarget;
    const QString folder = item ? item->data(Qt::UserRole + 1).toString() : QString();
    highlightDropTarget(nullptr);
    if (m_dragBadge) m_dragBadge->hide();
    while (QGuiApplication::overrideCursor()) QGuiApplication::restoreOverrideCursor();
    Q_UNUSED(global);
    if (!drop || folder.isEmpty()) return;
    const QStringList keys = selectedKeys();
    CaptureLibrary::instance().moveToFolder(keys, folder);
    setStatus(QString("Moved %1 capture%2 to %3.").arg(keys.size()).arg(keys.size() == 1 ? "" : "s", folder));
}
