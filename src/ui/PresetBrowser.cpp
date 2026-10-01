#include "PresetBrowser.h"
#include "../preset/PresetLibrary.h"
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>

namespace {
constexpr int kSceneLineRole = Qt::UserRole + 1;
constexpr int kKindRole = Qt::UserRole + 2;
constexpr int kSlotRole = Qt::UserRole + 3;
constexpr int kDimRole = Qt::UserRole + 4; // filtered out

enum CellKind { EmptyCell, PresetCell, CurrentCell, PendingCell };

const QColor kAccent("#00B0FF");

const char* kNormalHint =
    "Click to load · click an empty slot to start a new preset · drag to move or swap (Ctrl+Z undoes) · "
    "right-click for more · double-click a bank to name it";

// Draws every cell itself: preset name with its scenes underneath, the loaded
// preset tinted with an accent bar (like the footswitch tiles), quiet empty
// slots that say what a click does, and the drag / pick-mode states.
class SlotCellDelegate : public QStyledItemDelegate {
public:
    SlotCellDelegate(const PresetBrowser* browser, QObject* parent)
        : QStyledItemDelegate(parent), m_browser(browser) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const int kind = index.data(kKindRole).toInt();
        const int slot = index.data(kSlotRole).toInt();
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool selected = option.state & QStyle::State_Selected;
        const bool pick = m_browser->pickMode();
        const auto pickKind = m_browser->pickKind();
        const bool target = pick && m_browser->isPickTarget(slot);
        const bool source = pick && slot == m_browser->pickSource();
        const int dragSource = m_browser->dragSource();
        const bool dragging = dragSource >= 0;
        const bool dragFrom = dragging && slot == dragSource;
        const bool dropOn = dragging && !dragFrom && slot == m_browser->dropTarget();
        const bool dim = index.data(kDimRole).toBool() || (pick && !target && !source);
        const bool loaded = kind == CurrentCell || kind == PendingCell;
        const QRect r = option.rect;

        painter->save();
        QColor bg("#1A1A1E");
        if (loaded) bg = QColor("#0E2830");
        if (hover && !dragging && (!pick || target)) bg = pick ? QColor("#0E3442") : QColor("#232329");
        if (dropOn) bg = QColor("#0E3442");
        painter->fillRect(r, bg);
        if (loaded) painter->fillRect(QRect(r.left(), r.top(), 3, r.height()), kAccent);

        if (dim || dragFrom) painter->setOpacity(dragFrom ? 0.45 : 0.3);
        const QRect text = r.adjusted(14, 7, -10, -7);
        QFont nameFont = option.font;
        nameFont.setPixelSize(14);
        nameFont.setWeight(QFont::DemiBold);
        QFont smallFont = option.font;
        smallFont.setPixelSize(11);
        smallFont.setWeight(QFont::Normal);

        if (kind == EmptyCell) {
            QString offer;
            if (dropOn) offer = "Move here";
            else if (hover && !dragging && !pick) offer = "+ New preset";
            else if (hover && target) {
                offer = pickKind == PresetBrowser::PickKind::Duplicate ? "+ Copy here"
                      : pickKind == PresetBrowser::PickKind::Move ? "Move here" : "+ Save here";
            }
            QFont f = option.font;
            f.setPixelSize(13);
            painter->setFont(f);
            painter->setPen(offer.isEmpty() ? QColor("#3A3A42") : kAccent);
            painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, offer.isEmpty() ? QString::fromUtf8("—") : offer);
        } else {
            QString sub = index.data(kSceneLineRole).toString();
            QColor subColor("#8A8A96");
            if (kind == PendingCell) {
                sub = "Not saved yet";
                subColor = QColor("#FF9800");
            }
            const QString name = index.data(Qt::DisplayRole).toString();
            painter->setFont(nameFont);
            painter->setPen(loaded ? kAccent : QColor("#E6E6EA"));
            if (sub.isEmpty()) {
                painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                                  QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, text.width()));
            } else {
                const int half = text.height() / 2;
                painter->drawText(QRect(text.left(), text.top(), text.width(), half), Qt::AlignLeft | Qt::AlignVCenter,
                                  QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, text.width()));
                painter->setFont(smallFont);
                painter->setPen(subColor);
                painter->drawText(QRect(text.left(), text.top() + half, text.width(), text.height() - half),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  QFontMetrics(smallFont).elidedText(sub, Qt::ElideRight, text.width()));
            }
        }
        painter->setOpacity(1.0);

        QFont tag = smallFont;
        tag.setPixelSize(9);
        tag.setBold(true);
        auto outline = [&](const QColor& color, Qt::PenStyle style, const QString& label) {
            painter->setPen(QPen(color, 2, style));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(r.adjusted(1, 1, -1, -1));
            if (!label.isEmpty()) {
                painter->setFont(tag);
                painter->drawText(r.adjusted(0, 4, -8, 0), Qt::AlignRight | Qt::AlignTop, label);
            }
        };
        if (source) {
            outline(kAccent, Qt::SolidLine, pickKind == PresetBrowser::PickKind::Move ? "MOVING" : "COPYING");
        } else if (dragFrom) {
            outline(QColor("#8A8A96"), Qt::DashLine, dropOn || m_browser->dropTarget() == slot
                                                         ? QString() : QString("DROP HERE TO CANCEL"));
        } else if (dropOn) {
            outline(kAccent, Qt::SolidLine, kind == EmptyCell ? QString() : QString("SWAP"));
        } else if (pick && hover && target && kind != EmptyCell) {
            outline(kAccent, Qt::SolidLine, "SWAP");
        } else if (selected) {
            painter->setPen(QPen(QColor("#4A4A55"), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(r.adjusted(0, 0, -1, -1));
        }
        painter->restore();
    }

private:
    const PresetBrowser* m_browser;
};
} // namespace

// Table that turns an internal drag between cells into a swap request instead
// of letting Qt move item data around, and remembers where the drag is so the
// cells can show "move here" / "drop here to cancel".
class PresetSlotTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void(int fromRow, int fromCol, int toRow, int toCol)> onSwap;
    std::function<void(bool started)> onDragState;
    int dragRow = -1;
    int dragCol = -1;
    int hoverRow = -1;
    int hoverCol = -1;

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        dragRow = currentRow();
        dragCol = currentColumn();
        hoverRow = dragRow;
        hoverCol = dragCol;
        if (onDragState) onDragState(true);
        viewport()->update();
        QTableWidget::startDrag(supportedActions); // blocks until dropped or cancelled
        dragRow = dragCol = hoverRow = hoverCol = -1;
        if (onDragState) onDragState(false);
        viewport()->update();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        QTableWidget::dragMoveEvent(event);
        const QModelIndex at = indexAt(event->position().toPoint());
        if (at.row() != hoverRow || at.column() != hoverCol) {
            hoverRow = at.isValid() ? at.row() : -1;
            hoverCol = at.isValid() ? at.column() : -1;
            viewport()->update();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override {
        QTableWidget::dragLeaveEvent(event);
        hoverRow = hoverCol = -1;
        viewport()->update();
    }

    void dropEvent(QDropEvent* event) override {
        const QModelIndex target = indexAt(event->position().toPoint());
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (target.isValid() && dragRow >= 0 && onSwap &&
            (target.row() != dragRow || target.column() != dragCol)) {
            onSwap(dragRow, dragCol, target.row(), target.column());
        }
    }
};

PresetBrowser::PresetBrowser(PresetLibrary& library, int currentSlot, QWidget* parent)
    : QDialog(parent), m_library(library), m_currentSlot(currentSlot) {
    setWindowTitle("Presets");
    // Drop-down under the bank selector; closes when you click elsewhere.
    setWindowFlags(Qt::Popup);
    setMinimumSize(560, 360);
    setStyleSheet(
        "QDialog { background-color: #16161A; border: 1px solid #3A3A42; border-radius: 6px; }"
        "QLineEdit { background-color: #242528; color: #E0E0E0; border: 1px solid #333438; border-radius: 4px; padding: 7px 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #00B0FF; }"
        "QTableWidget { background-color: #1A1A1E; color: #E0E0E0; gridline-color: #2A2A30; border: 1px solid #2A2A30; }"
        "QHeaderView::section { background-color: #202024; color: #8A8A96; border: none; border-right: 1px solid #2A2A30; border-bottom: 1px solid #2A2A30; padding: 4px 10px; font-weight: bold; font-size: 12px; }"
        "QHeaderView::section:vertical { color: #C8C8D0; font-size: 13px; }"
        "QLabel { color: #8A8A96; font-size: 12px; }"
    );

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter by preset, scene or bank name… (Enter loads the first match)");
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);

    // Shown instead of the filter while picking a slot or confirming a delete.
    m_banner = new QWidget(this);
    m_banner->setObjectName("gridBanner");
    m_banner->setAttribute(Qt::WA_StyledBackground);
    auto* bannerLayout = new QHBoxLayout(m_banner);
    bannerLayout->setContentsMargins(12, 6, 6, 6);
    m_bannerLabel = new QLabel(m_banner);
    bannerLayout->addWidget(m_bannerLabel, 1);
    m_bannerAction = new QPushButton(m_banner);
    m_bannerAction->setObjectName("bannerAction");
    m_bannerAction->setCursor(Qt::PointingHandCursor);
    bannerLayout->addWidget(m_bannerAction);
    auto* cancelBtn = new QPushButton("Cancel", m_banner);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    bannerLayout->addWidget(cancelBtn);
    m_banner->hide();
    layout->addWidget(m_banner);

    m_table = new PresetSlotTable(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->viewport()->setAcceptDrops(true);
    m_table->setDragDropMode(QAbstractItemView::DragDrop);
    m_table->setDefaultDropAction(Qt::MoveAction);
    m_table->setDropIndicatorShown(false); // the cells draw their own drop state
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setMouseTracking(true); // hover hints on empty slots
    m_table->viewport()->setAttribute(Qt::WA_Hover);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setFixedHeight(30);
    m_table->verticalHeader()->setDefaultSectionSize(56);
    m_table->verticalHeader()->setFixedWidth(150);
    m_table->verticalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->setItemDelegate(new SlotCellDelegate(this, m_table));
    m_table->verticalHeader()->setToolTip("Bank number and name. Double-click to name a bank.");
    connect(m_table->verticalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int bank) {
        if (m_pickMode) return;
        emit bankRenameRequested(bank);
        accept();
    });
    layout->addWidget(m_table, 1);

    // Shown after a move: what happened, and a way back.
    m_undoBar = new QWidget(this);
    auto* undoLayout = new QHBoxLayout(m_undoBar);
    undoLayout->setContentsMargins(0, 0, 0, 0);
    m_undoLabel = new QLabel(m_undoBar);
    m_undoLabel->setStyleSheet("QLabel { color: #C8C8D0; font-size: 12px; }");
    undoLayout->addWidget(m_undoLabel, 1);
    auto* undoBtn = new QPushButton("Undo  (Ctrl+Z)", m_undoBar);
    undoBtn->setCursor(Qt::PointingHandCursor);
    undoBtn->setStyleSheet(
        "QPushButton { background-color: #242528; color: #00B0FF; border: 1px solid #333438; border-radius: 4px; padding: 4px 12px; font-size: 12px; }"
        "QPushButton:hover { background-color: #303236; color: white; }");
    undoLayout->addWidget(undoBtn);
    m_undoBar->hide();
    layout->addWidget(m_undoBar);

    m_hintLabel = new QLabel(this);
    setNormalHint();
    layout->addWidget(m_hintLabel);

    m_table->onSwap = [this](int fromRow, int fromCol, int toRow, int toCol) {
        moveSlot(slotAt(fromRow, fromCol), slotAt(toRow, toCol));
    };
    m_table->onDragState = [this](bool started) {
        if (started) m_hintLabel->setText("Drop on another slot to move it there (a taken slot swaps) · drop it back or press Esc to cancel");
        else setNormalHint();
    };

    connect(undoBtn, &QPushButton::clicked, this, &PresetBrowser::undoMove);
    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &PresetBrowser::undoMove);
    connect(m_bannerAction, &QPushButton::clicked, this, [this]() {
        const int slot = m_deleteSlot;
        hideBanner();
        if (slot < 0) return;
        emit deleteRequested(slot);
        // Earlier moves can't be undone reliably once a slot is gone.
        m_moves.clear();
        m_undoBar->hide();
        refresh(true);
    });
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_pickMode && m_pickKind == PickKind::Choose) reject();
        else hideBanner();
    });
    connect(m_table, &QTableWidget::cellActivated, this, [this](int row, int col) {
        if (m_pickMode) pickSlot(slotAt(row, col)); // Enter picks the highlighted slot
    });
    connect(m_table, &QTableWidget::cellEntered, this, [this](int row, int col) {
        const bool blocked = m_pickMode && !isPickTarget(slotAt(row, col));
        m_table->viewport()->setCursor(blocked ? Qt::ForbiddenCursor : Qt::PointingHandCursor);
    });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        const int slot = slotAt(row, col);
        if (m_pickMode) {
            pickSlot(slot);
            return;
        }
        if (m_deleteSlot >= 0) return; // answer the delete question first
        if (m_library.isOccupied(slot)) {
            emit slotActivated(slot);
        } else if (slot != m_pendingSlot) {
            emit blankRequested(slot);
        }
        accept();
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = m_table->indexAt(pos);
        if (!index.isValid() || m_pickMode || m_deleteSlot >= 0) return;
        showSlotMenu(slotAt(index.row(), index.column()), m_table->viewport()->mapToGlobal(pos));
    });
    connect(m_filterEdit, &QLineEdit::textChanged, this, &PresetBrowser::applyFilter);
    connect(m_filterEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString text = m_filterEdit->text().trimmed();
        if (text.isEmpty()) return;
        for (const auto& [slot, fileName] : m_library.entries()) {
            if (m_library.nameAt(slot).contains(text, Qt::CaseInsensitive)) {
                emit slotActivated(slot);
                accept();
                return;
            }
        }
    });

    refresh();
    m_filterEdit->setFocus();
}

void PresetBrowser::placeUnder(QWidget* anchor, QWidget* window) {
    const QRect win = window->geometry();
    int w = std::clamp(static_cast<int>(win.width() * 0.6), 720, 1200);
    int h = std::clamp(static_cast<int>(win.height() * 0.75), 420, 1400);
    const QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 6));
    if (QScreen* screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        w = std::min(w, avail.right() - pos.x() - 8);
        h = std::min(h, avail.bottom() - pos.y() - 8);
    }
    resize(std::max(w, minimumWidth()), std::max(h, minimumHeight()));
    move(pos);
    if (QTableWidgetItem* item = m_table->currentItem()) {
        m_table->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}

int PresetBrowser::dragSource() const {
    return m_table && m_table->dragRow >= 0 ? m_library.slotFor(m_table->dragRow, m_table->dragCol) : -1;
}

int PresetBrowser::dropTarget() const {
    return m_table && m_table->hoverRow >= 0 ? m_library.slotFor(m_table->hoverRow, m_table->hoverCol) : -1;
}

bool PresetBrowser::isPickTarget(int slot) const {
    if (!m_library.isValidSlot(slot)) return false;
    if (m_pickKind == PickKind::Move) return slot != m_pickSource;
    return !m_library.isOccupied(slot) && slot != m_pendingSlot;
}

void PresetBrowser::setNormalHint() {
    m_hintLabel->setText(kNormalHint);
}

void PresetBrowser::showBanner(const QString& text, const QString& actionText) {
    const bool danger = !actionText.isEmpty();
    m_banner->setStyleSheet(QString(
        "#gridBanner { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
        "QLabel { color: #F2F2F5; font-size: 13px; font-weight: bold; }"
        "QPushButton { background-color: transparent; color: #E6E6EA; border: 1px solid %3; border-radius: 4px; padding: 4px 14px; font-size: 12px; }"
        "QPushButton:hover { background-color: rgba(255,255,255,0.08); }"
        "#bannerAction { background-color: #C62828; border-color: #C62828; color: white; font-weight: bold; }"
        "#bannerAction:hover { background-color: #E53935; }")
        .arg(danger ? "#3A1618" : "#0B3442", danger ? "#C62828" : "#00B0FF", danger ? "#6A3A3C" : "#3A6A7A"));
    m_bannerLabel->setText(text);
    m_bannerAction->setText(actionText);
    m_bannerAction->setVisible(danger);
    m_filterEdit->hide();
    m_banner->show();
}

void PresetBrowser::hideBanner() {
    m_banner->hide();
    m_filterEdit->show();
    m_deleteSlot = -1;
    if (m_pickMode) endPick();
    m_table->viewport()->update();
}

void PresetBrowser::beginPick(PickKind kind, int sourceSlot, const QString& prompt) {
    m_deleteSlot = -1;
    m_pickMode = true;
    m_pickKind = kind;
    m_pickSource = sourceSlot;
    m_pickedSlot = -1;
    // A filter could hide the free slots; picking needs the whole grid.
    const bool filtered = !m_filterEdit->text().isEmpty();
    if (filtered) m_filterEdit->clear();
    showBanner(prompt);
    m_table->setDragEnabled(false);
    m_table->viewport()->setAcceptDrops(false);
    m_table->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_hintLabel->setText(kind == PickKind::Move
        ? "Click a slot: an empty one takes it, a taken one swaps · arrow keys and Enter work too · Esc cancels"
        : "Click a free slot · arrow keys and Enter work too · Esc cancels");

    if (!isVisible()) {
        // Opened for picking: start on the first target after the source (or
        // the loaded preset) so Enter alone picks it, with the source in view.
        const int start = m_library.isValidSlot(sourceSlot) ? sourceSlot : std::max(0, m_currentSlot);
        for (int i = 1; i <= m_library.slotCount(); ++i) {
            const int slot = (start + i) % m_library.slotCount();
            if (isPickTarget(slot) && !m_library.isOccupied(slot)) {
                m_table->setCurrentCell(m_library.bankOf(slot), m_library.indexInBank(slot));
                break;
            }
        }
        if (m_library.isValidSlot(sourceSlot)) {
            m_table->scrollToItem(m_table->item(m_library.bankOf(sourceSlot), m_library.indexInBank(sourceSlot)),
                                  QAbstractItemView::PositionAtCenter);
        }
    } else if (filtered && m_library.isValidSlot(sourceSlot)) {
        m_table->scrollToItem(m_table->item(m_library.bankOf(sourceSlot), m_library.indexInBank(sourceSlot)));
    }
    m_table->setFocus();
    m_table->viewport()->update();
}

void PresetBrowser::endPick() {
    m_pickMode = false;
    m_pickSource = -1;
    m_banner->hide();
    m_filterEdit->show();
    m_table->setDragEnabled(true);
    m_table->viewport()->setAcceptDrops(true);
    m_table->setDragDropMode(QAbstractItemView::DragDrop);
    m_table->viewport()->unsetCursor();
    setNormalHint();
    m_table->viewport()->update();
}

void PresetBrowser::pickSlot(int slot) {
    if (!m_pickMode || !isPickTarget(slot)) return;
    if (m_pickKind == PickKind::Choose) {
        m_pickedSlot = slot;
        accept();
        return;
    }
    const PickKind kind = m_pickKind;
    const int source = m_pickSource;
    endPick();
    if (kind == PickKind::Move) {
        moveSlot(source, slot);
        return;
    }
    emit duplicateTo(source, slot);
    // Stay where the user is looking; just show the copy.
    refresh(true);
    m_table->setCurrentCell(m_library.bankOf(slot), m_library.indexInBank(slot));
}

void PresetBrowser::moveSlot(int from, int to, bool recordUndo) {
    if (from == to || !m_library.isOccupied(from)) return;
    const QString name = m_library.nameAt(from);
    const QString displaced = m_library.nameAt(to);
    if (m_currentSlot == from) m_currentSlot = to;
    else if (m_currentSlot == to) m_currentSlot = from;
    if (m_pendingSlot == to) m_pendingSlot = from;
    emit slotsSwapped(from, to);
    refresh(true);
    m_table->setCurrentCell(m_library.bankOf(to), m_library.indexInBank(to));
    if (!recordUndo) return;
    m_moves.emplace_back(from, to);
    QString text = QString("Moved \"%1\" %2 → %3").arg(name, m_library.slotLabel(from), m_library.slotLabel(to));
    if (!displaced.isEmpty()) text += QString(", \"%1\" went to %2").arg(displaced, m_library.slotLabel(from));
    m_undoLabel->setText(text);
    m_undoBar->show();
}

void PresetBrowser::undoMove() {
    if (m_moves.empty() || m_pickMode) return;
    const auto [from, to] = m_moves.back();
    m_moves.pop_back();
    moveSlot(to, from, false);
    m_table->setCurrentCell(m_library.bankOf(from), m_library.indexInBank(from));
    if (m_moves.empty()) {
        m_undoBar->hide();
    } else {
        const auto [a, b] = m_moves.back();
        m_undoLabel->setText(QString("Undone. Earlier move: %1 → %2")
                                 .arg(m_library.slotLabel(a), m_library.slotLabel(b)));
    }
}

void PresetBrowser::startRename(int slot) {
    QTableWidgetItem* item = m_table->item(m_library.bankOf(slot), m_library.indexInBank(slot));
    if (!item) return;
    m_table->scrollToItem(item);
    auto* editor = new QLineEdit(m_table->viewport());
    editor->setText(slot == m_pendingSlot && !m_library.isOccupied(slot) ? m_pendingName : m_library.nameAt(slot));
    editor->setGeometry(m_table->visualItemRect(item).adjusted(4, 8, -4, -8));
    editor->setStyleSheet("QLineEdit { background-color: #1E1E22; color: white; border: 1px solid #00B0FF; border-radius: 4px; padding: 2px 8px; font-size: 14px; font-weight: bold; }");
    editor->selectAll();
    editor->show();
    editor->setFocus();
    auto done = std::make_shared<bool>(false);
    auto finish = [this, editor, slot, done](bool accept) {
        if (*done) return;
        *done = true;
        const QString name = editor->text().trimmed();
        editor->deleteLater();
        m_table->setFocus();
        if (!accept || name.isEmpty()) return;
        emit renameTo(slot, name);
        refresh(true);
    };
    connect(editor, &QLineEdit::editingFinished, this, [finish]() { finish(true); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), editor, nullptr, nullptr, Qt::WidgetShortcut);
    connect(escape, &QShortcut::activated, this, [finish]() { finish(false); });
}

void PresetBrowser::confirmDelete(int slot) {
    if (m_pickMode) endPick();
    m_deleteSlot = slot;
    QString text = QString("Delete %1 \"%2\"? The preset file is removed.")
                       .arg(m_library.slotLabel(slot), m_library.nameAt(slot));
    if (slot == m_currentSlot) text += " The board is cleared.";
    showBanner(text, "Delete");
    m_table->setFocus();
}

void PresetBrowser::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        const bool closes = m_pickMode && m_pickKind == PickKind::Choose;
        if (!closes && (m_pickMode || m_deleteSlot >= 0)) {
            hideBanner();
            event->accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void PresetBrowser::setPendingSlot(int slot, const QString& name) {
    m_pendingSlot = slot;
    m_pendingName = name;
    refresh();
}

void PresetBrowser::setBoardSlots(int currentSlot, int pendingSlot, const QString& pendingName) {
    m_currentSlot = currentSlot;
    m_pendingSlot = pendingSlot;
    m_pendingName = pendingName;
}

int PresetBrowser::slotAt(int row, int col) const {
    return m_library.slotFor(row, col);
}

void PresetBrowser::refresh(bool keepScroll) {
    const int scrollY = m_table->verticalScrollBar()->value();
    const int banks = m_library.numBanks();
    const int perBank = m_library.slotsPerBank();
    m_table->clear();
    m_table->setRowCount(banks);
    m_table->setColumnCount(perBank);

    QStringList columnLabels;
    for (int c = 0; c < perBank; ++c) columnLabels << QString(QChar('A' + c));
    m_table->setHorizontalHeaderLabels(columnLabels);
    QStringList rowLabels;
    for (int b = 0; b < banks; ++b) rowLabels << m_library.bankLabel(b);
    m_table->setVerticalHeaderLabels(rowLabels);

    for (int b = 0; b < banks; ++b) {
        for (int c = 0; c < perBank; ++c) {
            const int slot = m_library.slotFor(b, c);
            auto* item = new QTableWidgetItem();
            item->setData(kSlotRole, slot);
            item->setToolTip(m_library.slotLabel(slot));
            if (m_library.isOccupied(slot)) {
                item->setText(m_library.nameAt(slot));
                item->setData(kKindRole, slot == m_currentSlot ? CurrentCell : PresetCell);
                const QStringList scenes = m_library.sceneNamesAt(slot);
                if (!scenes.isEmpty()) {
                    // Scene numbers are what Alt+N and footswitches use, so keep them visible.
                    QStringList numbered;
                    for (int k = 0; k < scenes.size(); ++k) numbered << QString("%1 %2").arg(k + 1).arg(scenes[k]);
                    const QString line = numbered.join(QString::fromUtf8(" · "));
                    item->setData(kSceneLineRole, line);
                    item->setToolTip(m_library.slotLabel(slot) + "\nScenes: " + line);
                }
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            } else if (slot == m_pendingSlot) {
                item->setText(m_pendingName);
                item->setData(kKindRole, PendingCell);
                item->setToolTip(m_library.slotLabel(slot) + "\nNew preset, not saved yet");
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
            } else {
                item->setData(kKindRole, EmptyCell);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
            }
            m_table->setItem(b, c, item);
        }
    }

    applyFilter();
    if (keepScroll) {
        m_table->verticalScrollBar()->setValue(scrollY);
        return;
    }
    const int shown = m_library.isValidSlot(m_currentSlot) ? m_currentSlot : m_pendingSlot;
    if (m_library.isValidSlot(shown)) {
        const int row = m_library.bankOf(shown);
        const int col = m_library.indexInBank(shown);
        m_table->setCurrentCell(row, col);
        m_table->scrollToItem(m_table->item(row, col), QAbstractItemView::PositionAtCenter);
    }
}

void PresetBrowser::applyFilter() {
    const QString text = m_filterEdit->text().trimmed();
    for (int b = 0; b < m_table->rowCount(); ++b) {
        bool bankMatches = text.isEmpty();
        for (int c = 0; c < m_table->columnCount(); ++c) {
            const int slot = slotAt(b, c);
            QTableWidgetItem* item = m_table->item(b, c);
            if (!item || !m_library.isOccupied(slot)) continue;
            const bool match = text.isEmpty()
                || m_library.nameAt(slot).contains(text, Qt::CaseInsensitive)
                || m_library.bankName(b).contains(text, Qt::CaseInsensitive)
                || m_library.sceneNamesAt(slot).join(' ').contains(text, Qt::CaseInsensitive);
            bankMatches |= match;
            item->setData(kDimRole, !match && slot != m_currentSlot);
        }
        m_table->setRowHidden(b, !bankMatches);
    }
}

void PresetBrowser::showSlotMenu(int slot, const QPoint& globalPos) {
    if (!m_library.isValidSlot(slot)) return;
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1E1E22; color: #E0E0E0; border: 1px solid #333333; }"
        "QMenu::item:selected { background-color: #007ACC; color: white; }"
    );
    const QString label = m_library.slotLabel(slot);
    if (m_library.isOccupied(slot)) {
        QAction* loadAct = menu.addAction(QString("Load %1").arg(label));
        menu.addSeparator();
        QAction* dupAct = menu.addAction("Duplicate…");
        QAction* moveAct = menu.addAction("Move…");
        QAction* renameAct = menu.addAction("Rename");
        menu.addSeparator();
        QAction* deleteAct = menu.addAction("Delete…");
        QAction* chosen = menu.exec(globalPos);
        // All of these stay in the grid, so it keeps its place.
        if (chosen == loadAct) { emit slotActivated(slot); accept(); }
        else if (chosen == dupAct) {
            beginPick(PickKind::Duplicate, slot,
                      QString("Duplicating %1 \"%2\": click a free slot for the copy").arg(label, m_library.nameAt(slot)));
        } else if (chosen == moveAct) {
            beginPick(PickKind::Move, slot,
                      QString("Moving %1 \"%2\": click where it goes").arg(label, m_library.nameAt(slot)));
        }
        else if (chosen == renameAct) startRename(slot);
        else if (chosen == deleteAct) confirmDelete(slot);
    } else if (slot == m_pendingSlot) {
        QAction* saveAct = menu.addAction(QString("Save to %1").arg(label));
        QAction* renameAct = menu.addAction("Rename");
        QAction* chosen = menu.exec(globalPos);
        if (chosen == saveAct) { emit savePendingRequested(); accept(); }
        else if (chosen == renameAct) startRename(slot);
    } else {
        QAction* blankAct = menu.addAction(QString("New empty preset in %1").arg(label));
        QAction* copyAct = menu.addAction(QString("Copy current board to %1").arg(label));
        QAction* chosen = menu.exec(globalPos);
        if (chosen == blankAct) { emit blankRequested(slot); accept(); }
        else if (chosen == copyAct) { emit copyCurrentRequested(slot); accept(); }
    }
}
