#include "SoundGallery.h"
#include "CaptureLibrary.h"
#include "Tone3000ImageLoader.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

using namespace CaptureSounds;

namespace {
constexpr int kRowRole = Qt::UserRole + 1;
constexpr int kCardW = 156;
constexpr int kCardH = 200;
constexpr int kImage = 140;

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }
QString rgba(const QColor& c, double a) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
}

QString chipHtml(const QString& text, const QColor& c) {
    return QString("<span style='color:%1; background:%2; font-size:10px; font-weight:bold;'>&nbsp;%3&nbsp;</span>")
        .arg(hex(c.lighter(130)), rgba(c, 0.18), text.toUpper().toHtmlEscaped());
}

// One card: picture (or drawn artwork), name, type and creator.
class CardDelegate : public QStyledItemDelegate {
public:
    CardDelegate(const SoundGallery* gallery, std::function<const Item*(int)> item, QObject* parent)
        : QStyledItemDelegate(parent), m_item(std::move(item)) { Q_UNUSED(gallery); }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {kCardW, kCardH}; }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const Item* item = m_item(index.data(kRowRole).toInt());
        if (!item) return;
        const QRectF r = QRectF(option.rect).adjusted(5, 5, -5, -5);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const QColor accent = typeColor(item->type);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(selected ? accent : QColor(hover ? "#3A3D44" : "#2A2C31"), selected ? 2 : 1));
        p->setBrush(QColor(selected ? "#1F2126" : hover ? "#1C1E22" : "#17181B"));
        p->drawRoundedRect(r, 9, 9);

        const QRectF img(r.left() + (r.width() - kImage) / 2, r.top() + 6, kImage, kImage - 8);
        const qreal dpr = p->device()->devicePixelRatioF();
        QPixmap pm;
        if (!item->imageUrl.isEmpty()) {
            pm = Tone3000ImageLoader::instance()->thumbnail(item->imageUrl, (img.size() * dpr).toSize());
        }
        QPainterPath clip;
        clip.addRoundedRect(img, 6, 6);
        p->setClipPath(clip);
        if (!pm.isNull()) p->drawPixmap(img, pm, QRectF(0, 0, pm.width(), pm.height()));
        else p->drawPixmap(img.topLeft(), artwork(item->type, img.size().toSize(), dpr));
        p->setClipping(false);

        QFont name = option.font;
        name.setPixelSize(12);
        name.setBold(true);
        p->setFont(name);
        p->setPen(QColor("#EDEFF2"));
        const QRectF text(r.left() + 8, img.bottom() + 6, r.width() - 16, 16);
        p->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(name).elidedText(item->name, Qt::ElideRight, int(text.width())));

        QFont small = option.font;
        small.setPixelSize(10);
        small.setBold(true);
        p->setFont(small);
        const QString label = typeLabel(item->type).toUpper();
        const qreal chipW = QFontMetrics(small).horizontalAdvance(label) + 10;
        const QRectF chip(text.left(), text.bottom() + 5, chipW, 15);
        QColor fill = accent;
        fill.setAlphaF(0.18);
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawRoundedRect(chip, 3, 3);
        p->setPen(accent.lighter(130));
        p->drawText(chip, Qt::AlignCenter, label);
        small.setBold(false);
        p->setFont(small);
        // Variants of one tone share its name; what tells them apart goes here.
        p->setPen(QColor(item->variant.isEmpty() ? "#8A8F98" : "#C9CED6"));
        const QRectF by(chip.right() + 6, chip.top(), text.right() - chip.right() - 6, 15);
        const QString second = item->variant.isEmpty() ? item->creator : item->variant;
        p->drawText(by, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(second, Qt::ElideRight, int(by.width())));
        if (item->rating > 0) {
            p->setPen(QColor("#E8B84A"));
            p->drawText(QRectF(r.right() - 60, img.top() + 4, 54, 14), Qt::AlignRight, QString(item->rating, QChar(0x2605)));
        }
        p->restore();
    }

private:
    std::function<const Item*(int)> m_item;
};
} // namespace

SoundGallery::SoundGallery(std::shared_ptr<CaptureNode> target, Category category, QWidget* parent)
    : QDialog(parent), m_target(std::move(target)), m_category(category) {
    setWindowFlags(Qt::Popup);
    setMinimumSize(820, 480);
    m_before = snapshot(*m_target);
    setStyleSheet(
        "QDialog { background-color: #141518; border: 1px solid #3A3A42; border-radius: 8px; }"
        "QLabel { color: #C9CED6; background: transparent; }"
        "QLineEdit { background: #202227; color: #E0E0E0; border: 1px solid #30333A; border-radius: 5px; padding: 6px 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #00B0FF; }"
        "QComboBox { background: #202227; color: #D6DAE0; border: 1px solid #30333A; border-radius: 5px; padding: 5px 8px; }"
        "QComboBox QAbstractItemView { background: #202227; color: #D6DAE0; selection-background-color: #00897B; }"
        "QPushButton { background: #24262B; color: #D6DAE0; border: 1px solid #34373E; border-radius: 5px; padding: 6px 14px; font-size: 12px; }"
        "QPushButton:hover { background: #2E3137; color: white; }"
        "QListView { background: transparent; border: none; outline: none; }"
        "QScrollBar:vertical { background: transparent; width: 10px; }"
        "QScrollBar::handle:vertical { background: #34373E; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 14, 16, 14);
    outer->setSpacing(10);

    // Header: what is being chosen, search, sort, and the way to TONE3000.
    auto* header = new QHBoxLayout();
    header->setSpacing(10);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(0);
    m_title = new QLabel(this);
    m_title->setStyleSheet("color: #F2F3F5; font-size: 18px; font-weight: bold;");
    m_subtitle = new QLabel(this);
    m_subtitle->setStyleSheet("color: #8A8F98; font-size: 11px;");
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    header->addLayout(titles);
    header->addStretch();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search name, creator, gear, tag…");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(260);
    header->addWidget(m_search);
    m_sort = new QComboBox(this);
    m_sort->addItems({"Recently used", "Name", "Rating"});
    header->addWidget(m_sort);
    auto* online = new QPushButton(QString::fromUtf8("TONE3000  ↗"), this);
    online->setToolTip("Find and download more on TONE3000; downloads land in your library");
    online->setStyleSheet("QPushButton { background: #1D3A4A; color: #CFEFFF; border: 1px solid #2E5A70; font-weight: bold; }"
                          "QPushButton:hover { background: #23506A; }");
    header->addWidget(online);
    outer->addLayout(header);

    // Second step of Amp + Cab.
    m_stepBanner = new QWidget(this);
    m_stepBanner->setObjectName("stepBanner");
    m_stepBanner->setAttribute(Qt::WA_StyledBackground);
    m_stepBanner->setStyleSheet("#stepBanner { background: #2A2116; border: 1px solid #6A4A22; border-radius: 6px; }");
    auto* stepLayout = new QHBoxLayout(m_stepBanner);
    stepLayout->setContentsMargins(12, 6, 6, 6);
    m_stepLabel = new QLabel(m_stepBanner);
    m_stepLabel->setStyleSheet("color: #F5D9B0; font-size: 13px; font-weight: bold;");
    stepLayout->addWidget(m_stepLabel, 1);
    m_skip = new QPushButton("No cab", m_stepBanner);
    m_skip->setToolTip("Keep the amp without a cab (add one later from the block)");
    stepLayout->addWidget(m_skip);
    m_stepBanner->hide();
    outer->addWidget(m_stepBanner);

    // Body: cards on the left, the selected one in detail on the right.
    auto* body = new QHBoxLayout();
    body->setSpacing(14);
    auto* listColumn = new QVBoxLayout();
    m_list = new QListView(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setSpacing(2);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_model = new QStandardItemModel(this);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new CardDelegate(this, [this](int row) { return itemAt(row); }, m_list));
    listColumn->addWidget(m_list, 1);
    m_empty = new QLabel(this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet("color: #8A8F98; font-size: 13px;");
    m_empty->hide();
    listColumn->addWidget(m_empty, 1);
    body->addLayout(listColumn, 1);

    auto* pane = new QWidget(this);
    pane->setObjectName("previewPane");
    pane->setAttribute(Qt::WA_StyledBackground);
    pane->setFixedWidth(300);
    pane->setStyleSheet("#previewPane { background: #1A1B1F; border: 1px solid #2A2C31; border-radius: 10px; }");
    auto* paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(12, 12, 12, 12);
    paneLayout->setSpacing(6);
    m_previewImage = new QLabel(pane);
    m_previewImage->setFixedSize(276, 190);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setStyleSheet("background: #111215; border-radius: 8px;");
    paneLayout->addWidget(m_previewImage);
    m_previewChips = new QLabel(pane);
    m_previewChips->setTextFormat(Qt::RichText);
    paneLayout->addWidget(m_previewChips);
    m_previewName = new QLabel(pane);
    m_previewName->setWordWrap(true);
    m_previewName->setStyleSheet("color: #F2F3F5; font-size: 16px; font-weight: bold;");
    paneLayout->addWidget(m_previewName);
    m_previewBy = new QLabel(pane);
    m_previewBy->setStyleSheet("color: #9AA3AE; font-size: 12px;");
    paneLayout->addWidget(m_previewBy);
    m_previewFacts = new QLabel(pane);
    m_previewFacts->setWordWrap(true);
    m_previewFacts->setStyleSheet("color: #8A8F98; font-size: 11px;");
    paneLayout->addWidget(m_previewFacts);
    m_previewText = new QLabel(pane);
    m_previewText->setWordWrap(true);
    m_previewText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_previewText->setStyleSheet("color: #B7BDC6; font-size: 11px;");
    paneLayout->addWidget(m_previewText, 1);
    m_use = new QPushButton("Use", pane);
    m_use->setDefault(true);
    m_use->setStyleSheet("QPushButton { background: #00897B; color: white; border: none; font-weight: bold; padding: 9px; font-size: 13px; }"
                         "QPushButton:hover { background: #009688; }"
                         "QPushButton:disabled { background: #24262B; color: #5A5E66; }");
    paneLayout->addWidget(m_use);
    body->addWidget(pane);
    outer->addLayout(body, 1);

    auto* footer = new QHBoxLayout();
    m_status = new QLabel("Click a sound to hear it in your chain  ·  Enter or double-click to use it  ·  Esc to cancel", this);
    m_status->setStyleSheet("color: #8A8F98; font-size: 11px;");
    footer->addWidget(m_status, 1);
    outer->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, &SoundGallery::refill);
    connect(m_sort, &QComboBox::currentIndexChanged, this, &SoundGallery::refill);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (current.isValid()) preview(current.data(kRowRole).toInt());
    });
    connect(m_list, &QListView::activated, this, [this](const QModelIndex&) { commit(); });
    connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex&) { commit(); });
    connect(m_use, &QPushButton::clicked, this, &SoundGallery::commit);
    connect(m_skip, &QPushButton::clicked, this, [this]() {
        // Amp without a cab: drop the previewed cab and keep the amp.
        m_target->loadIr("");
        m_target->setIrInfo({});
        finish();
    });
    connect(online, &QPushButton::clicked, this, [this]() {
        restore(*m_target, m_before);
        done(OpenTone3000);
    });
    connect(Tone3000ImageLoader::instance(), &Tone3000ImageLoader::ready, this, [this](const QString& url) {
        m_list->viewport()->update();
        const Item* item = itemAt(m_list->currentIndex().data(kRowRole).toInt());
        if (item && item->imageUrl == url) showDetails(m_list->currentIndex().data(kRowRole).toInt());
    });

    showCategory(m_category);

    // The library is read in the background; fill in when it is ready.
    CaptureLibrary& library = CaptureLibrary::instance();
    connect(&library, &CaptureLibrary::changed, this, [this]() {
        if (!m_items.isEmpty()) return; // don't reshuffle what the user is looking at
        showCategory(m_category);
    });
    if (!library.isScanned()) library.rescan();
}

bool SoundGallery::wantsIr() const {
    return m_cabStep || m_category == Category::Cab || m_category == Category::Room;
}

void SoundGallery::placeUnder(QWidget* anchor, QWidget* window) {
    const QRect win = window->geometry();
    int w = std::clamp(static_cast<int>(win.width() * 0.7), 820, 1300);
    int h = std::clamp(static_cast<int>(win.height() * 0.7), 480, 900);
    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 6));
    if (QScreen* screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        w = std::min(w, avail.width() - 16);
        h = std::min(h, avail.height() - 16);
        pos.setX(std::clamp(pos.x(), avail.left() + 8, avail.right() - w - 8));
        pos.setY(std::clamp(pos.y(), avail.top() + 8, avail.bottom() - h - 8));
    }
    resize(w, h);
    move(pos);
}

const Item* SoundGallery::itemAt(int row) const {
    return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr;
}

void SoundGallery::showCategory(Category category, const QString& preselectPath) {
    m_category = category;
    m_items = itemsIn(category);
    m_title->setText(m_cabStep ? QString("Cab") : categoryTitle(category));
    m_subtitle->setText(m_cabStep ? QString("The cab after your amp. Your last used one is ready.") : categoryHint(category));
    m_search->blockSignals(true);
    m_search->clear();
    m_search->blockSignals(false);
    refill();
    int row = 0;
    if (!preselectPath.isEmpty()) {
        for (int i = 0; i < m_model->rowCount(); ++i) {
            const Item* item = itemAt(m_model->item(i)->data(kRowRole).toInt());
            if (item && item->path == preselectPath) row = i;
        }
    }
    // Start on what the block already has, if it is in the list.
    const std::string current = wantsIr() ? m_target->irPath() : m_target->modelPath();
    if (preselectPath.isEmpty() && !current.empty()) {
        for (int i = 0; i < m_model->rowCount(); ++i) {
            const Item* item = itemAt(m_model->item(i)->data(kRowRole).toInt());
            if (item && item->path.toStdString() == current) row = i;
        }
    }
    if (m_model->rowCount() > 0) {
        const QModelIndex index = m_model->index(row, 0);
        // Only the cab step previews straight away; otherwise nothing changes
        // until the user picks something.
        if (m_cabStep) m_list->setCurrentIndex(index);
        else {
            m_list->selectionModel()->blockSignals(true);
            m_list->setCurrentIndex(index);
            m_list->selectionModel()->blockSignals(false);
            showDetails(index.data(kRowRole).toInt());
        }
        m_list->scrollTo(index);
    }
    m_list->setFocus();
}

void SoundGallery::refill() {
    const QString text = m_search->text().trimmed();
    m_shown.clear();
    for (int i = 0; i < m_items.size(); ++i) {
        const Item& item = m_items[i];
        if (!text.isEmpty()) {
            const QString hay = item.name + " " + item.variant + " " + item.creator + " " + item.make + " " + item.model + " " + item.tags.join(' ');
            if (!hay.contains(text, Qt::CaseInsensitive)) continue;
        }
        m_shown << i;
    }
    const int sort = m_sort->currentIndex();
    std::stable_sort(m_shown.begin(), m_shown.end(), [this, sort](int a, int b) {
        const Item& x = m_items[a];
        const Item& y = m_items[b];
        if (sort == 1) return x.name.compare(y.name, Qt::CaseInsensitive) < 0;
        if (sort == 2 && x.rating != y.rating) return x.rating > y.rating;
        return x.lastUsed > y.lastUsed;
    });
    m_model->clear();
    for (int index : m_shown) {
        auto* row = new QStandardItem(m_items[index].name);
        row->setData(index, kRowRole);
        m_model->appendRow(row);
    }
    const bool empty = m_shown.isEmpty();
    m_list->setVisible(!empty);
    m_empty->setVisible(empty);
    m_use->setEnabled(!empty);
    if (empty) {
        m_empty->setText(m_items.isEmpty()
            ? QString("Nothing for %1 in your library yet.\n\nFind some on TONE3000 (top right) or add files to the library.")
                  .arg(categoryTitle(m_category))
            : QString("Nothing matches \"%1\".").arg(text));
    }
}

void SoundGallery::showDetails(int row) {
    const Item* item = itemAt(row);
    if (!item) return;
    const QColor accent = typeColor(item->type);
    QPixmap pm;
    const qreal dpr = devicePixelRatioF();
    if (!item->imageUrl.isEmpty()) {
        pm = Tone3000ImageLoader::instance()->thumbnail(item->imageUrl, m_previewImage->size() * dpr);
        if (!pm.isNull()) pm.setDevicePixelRatio(dpr);
    }
    m_previewImage->setPixmap(!pm.isNull() ? pm : artwork(item->type, m_previewImage->size(), dpr));
    QString chips = chipHtml(typeLabel(item->type), accent);
    if (item->toneId > 0) chips += "&nbsp;&nbsp;" + chipHtml("TONE3000", QColor("#55B8E8"));
    if (item->rating > 0) chips += "&nbsp;&nbsp;<span style='color:#E8B84A;'>" + QString(item->rating, QChar(0x2605)) + "</span>";
    m_previewChips->setText(chips);
    m_previewName->setText(item->variant.isEmpty() ? item->name : item->name + "\n" + item->variant);
    m_previewBy->setText(item->creator.isEmpty() ? QString() : "by " + item->creator);
    QStringList facts;
    const QString gear = gearText(item->make, item->model);
    if (!gear.isEmpty()) facts << gear;
    if (!item->architecture.isEmpty()) facts << item->architecture;
    if (item->hasLoudness) facts << QString("loudness %1 dB").arg(item->loudness, 0, 'f', 1);
    m_previewFacts->setText(facts.join(QString::fromUtf8("  ·  ")));
    QString text = item->description.left(400);
    if (!item->tags.isEmpty()) text += (text.isEmpty() ? "" : "\n\n") + item->tags.mid(0, 8).join(QString::fromUtf8(" · "));
    m_previewText->setText(text);
}

void SoundGallery::preview(int row) {
    const Item* item = itemAt(row);
    if (!item) return;
    showDetails(row);
    QString error;
    if (!apply(*m_target, *item, &error)) {
        m_status->setText("Could not load it: " + error);
        return;
    }
    m_status->setText(QString::fromUtf8("Playing \"%1\"  ·  Enter to use it  ·  Esc to cancel").arg(item->name));
}

void SoundGallery::commit() {
    const QModelIndex current = m_list->currentIndex();
    const Item* item = current.isValid() ? itemAt(current.data(kRowRole).toInt()) : nullptr;
    if (!item) return;
    // The card may only have been selected, not yet heard.
    const std::string loaded = item->format == Tone3000::Format::Nam ? m_target->modelPath() : m_target->irPath();
    if (loaded != item->path.toStdString()) preview(current.data(kRowRole).toInt());
    markUsed(*item);

    // An amp needs a cab: Amp + Cab goes on to pick one, unless the block has one.
    if (!m_cabStep && m_category == Category::AmpCab && item->type != Type::FullRig && m_target->irPath().empty()) {
        m_cabStep = true;
        m_stepLabel->setText(QString("Step 2 of 2  ·  Cab for \"%1\"").arg(item->name));
        m_stepBanner->show();
        m_target->setParameter(CaptureNode::CabEnabled, 1.0f);
        const QString last = lastUsedCab();
        showCategory(Category::Cab, last);
        if (m_model->rowCount() == 0) finish(); // no cabs in the library: done with the amp
        return;
    }
    if (item->type == Type::FullRig && m_target->irPath().empty()) {
        m_target->setParameter(CaptureNode::CabEnabled, 0.0f);
    }
    finish();
}

void SoundGallery::finish() {
    QDialog::accept();
}

void SoundGallery::reject() {
    restore(*m_target, m_before);
    QDialog::reject();
}

void SoundGallery::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commit();
        return;
    }
    QDialog::keyPressEvent(event);
}
