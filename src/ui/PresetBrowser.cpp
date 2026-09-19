#include "PresetBrowser.h"
#include "../preset/PresetLibrary.h"
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>

namespace {
constexpr int kSceneLineRole = Qt::UserRole + 1;

// Preset name on the first line, its scene names dimmed underneath.
class SlotCellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QString scenes = index.data(kSceneLineRole).toString();
        if (scenes.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString name = opt.text;
        opt.text.clear();
        opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const QRect r = opt.rect.adjusted(6, 3, -6, -3);
        const int half = r.height() / 2;
        painter->save();
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(QPalette::Text));
        if (const QVariant fg = index.data(Qt::ForegroundRole); fg.isValid()) painter->setPen(fg.value<QBrush>().color());
        if (opt.state & QStyle::State_Selected) painter->setPen(Qt::white);
        painter->drawText(QRect(r.left(), r.top(), r.width(), half), Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(name, Qt::ElideRight, r.width()));
        QFont small = opt.font;
        small.setBold(false);
        small.setPointSizeF(std::max(6.0, small.pointSizeF() - 2));
        painter->setFont(small);
        painter->setPen(opt.state & QStyle::State_Selected ? QColor("#D0F0EC") : QColor("#8A8A96"));
        painter->drawText(QRect(r.left(), r.top() + half, r.width(), r.height() - half), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(small).elidedText(scenes, Qt::ElideRight, r.width()));
        painter->restore();
    }
};
} // namespace

// Table that turns an internal drag between cells into a swap request instead
// of letting Qt move item data around.
class PresetSlotTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void(int fromRow, int fromCol, int toRow, int toCol)> onSwap;

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        m_dragRow = currentRow();
        m_dragCol = currentColumn();
        QTableWidget::startDrag(supportedActions);
    }

    void dropEvent(QDropEvent* event) override {
        const QModelIndex target = indexAt(event->position().toPoint());
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (target.isValid() && m_dragRow >= 0 && onSwap &&
            (target.row() != m_dragRow || target.column() != m_dragCol)) {
            onSwap(m_dragRow, m_dragCol, target.row(), target.column());
        }
        m_dragRow = m_dragCol = -1;
    }

private:
    int m_dragRow = -1;
    int m_dragCol = -1;
};

PresetBrowser::PresetBrowser(PresetLibrary& library, int currentSlot, QWidget* parent)
    : QDialog(parent), m_library(library), m_currentSlot(currentSlot) {
    setWindowTitle("Presets");
    // Drop-down under the bank selector; closes when you click elsewhere.
    setWindowFlags(Qt::Popup);
    setMinimumSize(640, 460);
    setStyleSheet(
        "QDialog { background-color: #16161A; border: 1px solid #3A3A42; border-radius: 6px; }"
        "QLineEdit { background-color: #242528; color: #E0E0E0; border: 1px solid #333438; border-radius: 4px; padding: 5px 8px; }"
        "QLineEdit:focus { border-color: #00B0FF; }"
        "QTableWidget { background-color: #1A1A1E; color: #E0E0E0; gridline-color: #2A2A30; border: 1px solid #2A2A30; }"
        "QTableWidget::item { padding: 4px 6px; }"
        "QTableWidget::item:selected { background-color: #00897B; color: white; }"
        "QHeaderView::section { background-color: #202024; color: #8A8A96; border: none; border-right: 1px solid #2A2A30; border-bottom: 1px solid #2A2A30; padding: 4px; font-weight: bold; }"
        "QLabel { color: #8A8A96; font-size: 11px; }"
    );

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter by preset, scene or bank name… (Enter loads the first match)");
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);

    m_table = new PresetSlotTable(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->viewport()->setAcceptDrops(true);
    m_table->setDragDropMode(QAbstractItemView::DragDrop);
    m_table->setDefaultDropAction(Qt::MoveAction);
    m_table->setDropIndicatorShown(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(40);
    m_table->setItemDelegate(new SlotCellDelegate(m_table));
    m_table->verticalHeader()->setToolTip("Bank number and name. Double-click to name a bank.");
    connect(m_table->verticalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int bank) {
        emit bankRenameRequested(bank);
        accept();
    });
    layout->addWidget(m_table, 1);

    m_hintLabel = new QLabel("Click to load · drag to move or swap · right-click for more · double-click a bank to name it", this);
    layout->addWidget(m_hintLabel);

    m_table->onSwap = [this](int fromRow, int fromCol, int toRow, int toCol) {
        const int from = slotAt(fromRow, fromCol);
        const int to = slotAt(toRow, toCol);
        if (!m_library.isOccupied(from)) return;
        if (m_currentSlot == from) m_currentSlot = to;
        else if (m_currentSlot == to) m_currentSlot = from;
        emit slotsSwapped(from, to);
        refresh();
        m_table->setCurrentCell(toRow, toCol);
    };

    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        const int slot = slotAt(row, col);
        if (m_library.isOccupied(slot)) {
            emit slotActivated(slot);
            accept();
        } else {
            const QRect rect = m_table->visualRect(m_table->model()->index(row, col));
            showSlotMenu(slot, m_table->viewport()->mapToGlobal(rect.bottomLeft()));
        }
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = m_table->indexAt(pos);
        if (!index.isValid()) return;
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

int PresetBrowser::slotAt(int row, int col) const {
    return m_library.slotFor(row, col);
}

void PresetBrowser::refresh() {
    const int banks = m_library.numBanks();
    const int perBank = m_library.slotsPerBank();
    m_table->clear();
    m_table->setRowCount(banks);
    m_table->setColumnCount(perBank);

    QStringList columnLabels;
    for (int c = 0; c < perBank; ++c) columnLabels << QString(QChar('A' + c));
    m_table->setHorizontalHeaderLabels(columnLabels);
    // Keep the header wide enough for "Bank 04 Name".
    m_table->verticalHeader()->setMinimumWidth(56);
    QStringList rowLabels;
    for (int b = 0; b < banks; ++b) rowLabels << m_library.bankLabel(b);
    m_table->setVerticalHeaderLabels(rowLabels);

    for (int b = 0; b < banks; ++b) {
        for (int c = 0; c < perBank; ++c) {
            const int slot = m_library.slotFor(b, c);
            auto* item = new QTableWidgetItem();
            item->setToolTip(m_library.slotLabel(slot));
            if (m_library.isOccupied(slot)) {
                item->setText(m_library.nameAt(slot));
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
                if (slot == m_currentSlot) {
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                    item->setForeground(QColor("#00B0FF"));
                }
            } else {
                item->setText(QString::fromUtf8("—"));
                item->setForeground(QColor("#45454D"));
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
            }
            m_table->setItem(b, c, item);
        }
    }

    if (m_library.isValidSlot(m_currentSlot)) {
        const int row = m_library.bankOf(m_currentSlot);
        const int col = m_library.indexInBank(m_currentSlot);
        m_table->setCurrentCell(row, col);
        m_table->scrollToItem(m_table->item(row, col), QAbstractItemView::PositionAtCenter);
    }
    applyFilter();
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
            if (slot != m_currentSlot) {
                item->setForeground(match ? QColor("#E0E0E0") : QColor("#55555D"));
            }
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
        QAction* renameAct = menu.addAction("Rename…");
        QAction* dupAct = menu.addAction("Duplicate to next free slot");
        menu.addSeparator();
        QAction* deleteAct = menu.addAction("Delete preset…");
        QAction* chosen = menu.exec(globalPos);
        if (chosen == loadAct) { emit slotActivated(slot); accept(); }
        // These open dialogs, which would close the popup anyway; the window
        // runs them after closing it and then reopens the grid.
        else if (chosen == renameAct) { emit renameRequested(slot); accept(); }
        else if (chosen == dupAct) { emit duplicateRequested(slot); accept(); }
        else if (chosen == deleteAct) { emit deleteRequested(slot); accept(); }
    } else {
        QAction* saveAct = menu.addAction(QString("Save current board to %1…").arg(label));
        if (menu.exec(globalPos) == saveAct) {
            emit saveCurrentToSlotRequested(slot);
            accept();
        }
    }
}
