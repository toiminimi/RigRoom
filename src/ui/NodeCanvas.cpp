#include "NodeCanvas.h"
#include <set>
#include <array>
#include "GridRow.h"
#include "PlusButtonWidget.h"
#include "RoutingHandleItem.h"
#include "CanvasMetrics.h"
#include <QPainter>
#include <QTimer>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QScrollBar>
#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <functional>

// ─── Dimensions ───────────────────────────────────────────────────────────────
static constexpr qreal SYS_NODE_W  = CanvasMetrics::cardWidth;
static constexpr qreal SYS_NODE_H  = CanvasMetrics::cardHeight;
static constexpr qreal PLUG_NODE_W = CanvasMetrics::cardWidth;
static constexpr qreal PLUG_NODE_H = CanvasMetrics::cardHeight;
static constexpr qreal MARGIN_X    = CanvasMetrics::marginX;
static constexpr qreal MARGIN_Y    = CanvasMetrics::marginY;
// Width of the small '+' slot between nodes
static constexpr qreal INSERT_BTN_W = 18.0;
static constexpr qreal MIN_SPACING = 4.0;
static constexpr qreal MAX_SPACING = 120.0;
static constexpr qreal CANVAS_GRID = 28.0;

static qreal snapToGrid(qreal value) {
    return std::round(value / CANVAS_GRID) * CANVAS_GRID;
}

// Slot descriptor for per-row layout calculation
struct LayoutSlot {
    bool isNode;
    qreal x;   // left edge
    int col;   // for nodes: grid column; for insert buttons: chain insertion index
};

// ─── Helpers ──────────────────────────────────────────────────────────────────
static QPainterPath makeCurve(QPointF a, QPointF b) {
    QPainterPath p;
    p.moveTo(a);
    qreal dx = std::max(30.0, std::abs(b.x() - a.x()) * 0.45);
    p.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
    return p;
}

class SplitToolItem final : public QGraphicsItem {
public:
    explicit SplitToolItem(NodeCanvas* canvas) : m_canvas(canvas) {
        setAcceptHoverEvents(true);
        setCursor(Qt::OpenHandCursor);
        setZValue(30);
        setToolTip("Drag onto any gap between blocks or on a path line to create a Split Section.");
    }

    bool isLimitReached() const {
        if (!m_canvas) return false;
        return m_canvas->hasSplitSection(1) && 
               m_canvas->hasSplitSection(3) && 
               m_canvas->hasSplitSection(0) && 
               m_canvas->hasSplitSection(4);
    }

    QRectF boundingRect() const override { return QRectF(0, 0, CanvasMetrics::splitToolWidth, CanvasMetrics::splitToolHeight); }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        const bool disabled = isLimitReached();
        if (disabled) {
            painter->setOpacity(0.35);
        } else {
            painter->setOpacity(1.0);
        }
        const QColor accent = m_dragging ? QColor(190, 164, 235)
            : (m_hovered ? QColor(143, 169, 224) : QColor(105, 126, 177));
        painter->setPen(QPen(accent, m_hovered || m_dragging ? 2.0 : 1.2));
        painter->setBrush(QColor(25, 29, 36, 235));
        painter->drawRoundedRect(boundingRect().adjusted(1, 1, -1, -1), 5, 5);

        painter->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap));
        const qreal centerY = CanvasMetrics::splitToolHeight / 2.0;
        painter->drawLine(QPointF(7, centerY), QPointF(16, centerY));
        painter->drawLine(QPointF(16, centerY), QPointF(22, centerY - 5));
        painter->drawLine(QPointF(16, centerY), QPointF(22, centerY + 5));
        painter->setBrush(accent);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(16, centerY), 2.2, 2.2);

        QFont font = painter->font();
        font.setPixelSize(10);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(235, 240, 246));
        painter->drawText(QRectF(27, 0, 35, CanvasMetrics::splitToolHeight), Qt::AlignVCenter | Qt::AlignLeft, "SPLIT");
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (isLimitReached()) {
            event->ignore();
            return;
        }
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
        update();
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (!m_dragging) return;
        TargetGap target = findTargetGap(event->scenePos());
        m_activeTarget = target;
        updatePlusHighlights(target.parentRow, target.gapIndex);

        if (target.parentRow >= 0 && target.gapIndex >= 0) {
            setPos(target.snapX - boundingRect().width() / 2.0,
                   target.snapY - boundingRect().height() / 2.0);
        } else {
            setPos(event->scenePos() - QPointF(boundingRect().width() / 2.0, boundingRect().height() / 2.0));
        }
        update();
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (!m_dragging) return;
        m_dragging = false;
        TargetGap target = m_activeTarget.parentRow >= 0 ? m_activeTarget : findTargetGap(event->scenePos());
        m_activeTarget = TargetGap{};
        updatePlusHighlights(-1, -1);
        setCursor(Qt::OpenHandCursor);
        update();

        if (target.parentRow >= 0 && target.gapIndex >= 0) {
            NodeCanvas* canvas = m_canvas;
            int parentRow = target.parentRow;
            int gap = target.gapIndex;
            QTimer::singleShot(0, canvas, [canvas, parentRow, gap] {
                canvas->createSplitAtPathGap(parentRow, gap);
            });
        } else {
            NodeCanvas* canvas = m_canvas;
            QTimer::singleShot(0, canvas, [canvas] { canvas->updateLayout(); });
        }
        event->accept();
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override {
        m_hovered = true;
        if (isLimitReached()) {
            setCursor(Qt::ForbiddenCursor);
            setToolTip("Maximum split limit reached. Delete an existing split section to create a new one.");
        } else {
            setCursor(Qt::OpenHandCursor);
            setToolTip("Drag onto any gap between blocks or on a path line to create a Split Section.");
        }
        update();
        event->accept();
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override {
        m_hovered = false;
        setCursor(Qt::ArrowCursor);
        update();
        event->accept();
    }

private:
    struct TargetGap {
        int parentRow = -1;
        int gapIndex = -1;
        qreal snapX = 0.0;
        qreal snapY = 0.0;
    };

    bool isValidSplitGap(int pRow, int gapIndex) const {
        if (!m_canvas || pRow < 0 || pRow >= NodeCanvas::NUM_ROWS || gapIndex < 0) return false;
        
        int numCols = m_canvas->getNumCols();
        if (gapIndex >= numCols) return false;

        if (pRow != NodeCanvas::MAIN_ROW) {
            int parentSplitCol = m_canvas->getSplitCol(pRow);
            if (parentSplitCol >= 0 && gapIndex <= parentSplitCol + 1) {
                return false; // Must be at least 1 slot after parent branch split point
            }
            int parentMergeCol = m_canvas->getMergeCol(pRow);
            if (parentMergeCol >= 0 && gapIndex >= parentMergeCol) {
                return false; // Must be strictly before parent branch merge point
            }
        }

        int maxAllowedGap = numCols - 1;
        if (pRow != NodeCanvas::MAIN_ROW) {
            int mCol = m_canvas->getMergeCol(pRow);
            if (mCol >= 0) {
                maxAllowedGap = std::min(maxAllowedGap, mCol - 1);
            }
        }
        
        if (gapIndex > maxAllowedGap) {
            return false;
        }

        return true;
    }

    TargetGap findTargetGap(const QPointF& scenePoint) const {
        TargetGap best;
        if (!m_canvas) return best;

        qreal minDistance = 140.0;

        std::vector<int> eligibleParents;
        if (!m_canvas->hasSplitSection(1) || !m_canvas->hasSplitSection(3)) {
            eligibleParents.push_back(NodeCanvas::MAIN_ROW);
        }
        if (m_canvas->hasSplitSection(1) && !m_canvas->hasSplitSection(0)) {
            eligibleParents.push_back(1);
        }
        if (m_canvas->hasSplitSection(3) && !m_canvas->hasSplitSection(4)) {
            eligibleParents.push_back(3);
        }

        for (int pRow : eligibleParents) {
            qreal rY = m_canvas->getRowCenterY(pRow);
            for (int k = 0; k <= m_canvas->getNumCols(); ++k) {
                if (!isValidSplitGap(pRow, k)) continue;

                qreal gX = m_canvas->getGapX(k);
                qreal dist = std::hypot(gX - scenePoint.x(), rY - scenePoint.y());
                if (dist < minDistance) {
                    minDistance = dist;
                    best.parentRow = pRow;
                    best.gapIndex = k;
                    best.snapX = gX;
                    best.snapY = rY;
                }
            }
        }
        return best;
    }

    void updatePlusHighlights(int activeRow, int activeGap) {
        if (!scene()) return;
        for (QGraphicsItem* item : scene()->items()) {
            if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                bool match = (plus->getRow() == activeRow && (plus->getCol() == activeGap || plus->getCol() == activeGap - 1));
                plus->setRoutingTarget(match);
            }
        }
    }

    NodeCanvas* m_canvas;
    TargetGap m_activeTarget;
    bool m_hovered = false;
    bool m_dragging = false;
};

// ─── Constructor ──────────────────────────────────────────────────────────────
NodeCanvas::NodeCanvas(AudioEngine* engine, QWidget* parent)
    : QGraphicsView(parent), m_engine(engine)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setBackgroundBrush(QColor(14, 14, 16));
    setDragMode(QGraphicsView::NoDrag);
    setAcceptDrops(true); // plugins dragged in from the plugin browser
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);

    // Build system node widgets once
    for (auto& node : m_engine->getNodes()) {
        if (node->uniqueId == "system_input") {
            m_sysInputWidget = new NodeWidget(node);
            m_scene->addItem(m_sysInputWidget);
        } else if (node->uniqueId == "system_output") {
            m_sysOutputWidget = new NodeWidget(node);
            m_scene->addItem(m_sysOutputWidget);
        }
    }

    // Initialize drag placeholder item
    m_dragPlaceholderItem = new QGraphicsRectItem();
    QPen dashedPen(QColor(255, 200, 50), 2, Qt::DashLine);
    m_dragPlaceholderItem->setPen(dashedPen);
    m_dragPlaceholderItem->setBrush(QColor(255, 200, 50, 15));
    m_dragPlaceholderItem->setZValue(-1);
    m_dragPlaceholderItem->hide();
    m_scene->addItem(m_dragPlaceholderItem);

    // Initialize animation timer
    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &NodeCanvas::tickAnimations);

    setupZoomOverlay();
    m_rows[0].name = "Path B";
    m_rows[2].name = "Path B";
}

NodeCanvas::~NodeCanvas() {
    clearSceneItems();
}

// ─── Public API ───────────────────────────────────────────────────────────────
void NodeCanvas::insertPluginAt(int row, int col, std::shared_ptr<AudioNode> node) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    m_rows[row].plugins[col] = node;
    if (row != MAIN_ROW) {
        m_rows[row].hasSplitSection = true;
        if (!m_rows[row].levelConfigured) setBranchDefaults(row, 0.25118864f);
        m_rows[row].enabled = true;
        updateBranchGains(row);
    }
    m_engine->addNode(node);
    applyRoutingChange();
}

NodeCanvas::InsertPlan NodeCanvas::planInsertColumn(int row, int col, int side) const {
    InsertPlan plan;
    // col == m_numCols means "after the last column".
    if (row < 0 || row >= NUM_ROWS || col < 0 || col > m_numCols) return plan;

    // First look for room inside the lane itself: between the points where
    // this lane's path starts, ends, or feeds a parallel path, blocks can slide
    // into a free slot without changing what they are connected to.
    {
        const GridRow& lane = m_rows[row];
        const int start = row == MAIN_ROW ? 0 : lane.splitCol + 1;
        const int end = row == MAIN_ROW ? m_numCols : (lane.mergeCol < 0 ? m_numCols : lane.mergeCol);
        std::set<int> childSplits, childMerges;
        for (int b : {0, 1, 3, 4}) {
            const GridRow& child = m_rows[b];
            if (b == row || !child.hasSplitSection || child.parentRow != row) continue;
            childSplits.insert(child.splitCol + 1);
            childMerges.insert(child.mergeCol < 0 ? m_numCols : child.mergeCol);
        }
        std::set<int> bounds = {start, end};
        bounds.insert(childSplits.begin(), childSplits.end());
        bounds.insert(childMerges.begin(), childMerges.end());
        // At a boundary the new block goes before a split or the lane's end,
        // and after a merge or the lane's start.
        // An explicit side (the two markers at a junction) decides it directly.
        const bool left = side == InsertBefore
            || (side != InsertAfter && (childSplits.count(col) || (row != MAIN_ROW && col == end)));
        const bool right = side == InsertAfter
            || (side != InsertBefore && (childMerges.count(col) || (row != MAIN_ROW && col == start)));
        if (col >= start && col <= end && (row == MAIN_ROW || lane.hasSplitSection) && !(left && right)) {
            int lo = start, hi = end;
            for (int b : bounds) {
                if (b < col || (b == col && right)) lo = std::max(lo, b);
                if (b > col || (b == col && left)) hi = std::min(hi, b);
            }
            if (right) lo = col;
            if (left) hi = col;
            for (int k = col; k < hi; ++k) {
                if (!lane.plugins[k]) {
                    plan = InsertPlan{};
                    plan.emptyColumn = k; plan.right = true; plan.newColumn = col; plan.laneOnly = true;
                    return plan;
                }
            }
            for (int k = col - 1; k >= lo; --k) {
                if (!lane.plugins[k]) {
                    plan = InsertPlan{};
                    plan.emptyColumn = k; plan.right = false; plan.newColumn = col - 1; plan.laneOnly = true;
                    return plan;
                }
            }
        }
    }

    // The board never grows by itself. Otherwise room comes from a column that is empty
    // in every lane: preferably by sliding the blocks between it and the insert
    // point to the right, otherwise by sliding blocks on the left to the left.
    //
    // Split/merge gaps move with the blocks. At the insert point itself, the
    // target lane and the branches containing it widen to include the new
    // column; every other section keeps it outside (before a split, after a merge).
    std::set<int> widening;
    for (int r = row; r >= 0 && r < NUM_ROWS && r != MAIN_ROW; r = (r == 0 ? 1 : (r == 4 ? 3 : MAIN_ROW))) {
        widening.insert(r);
    }
    auto columnEmpty = [this](int c) {
        for (const auto& r : m_rows) if (r.plugins[c]) return false;
        return true;
    };
    // New gaps when the empty column `k` is used; false if a path would vanish.
    auto remap = [&](int k, bool right) {
        for (int b : {0, 1, 3, 4}) {
            const GridRow& section = m_rows[b];
            auto map = [&](int g, bool isSplit) {
                // Does the new column fall inside this section? Always for the
                // lanes that widen; at a junction, "after the split" and "before
                // the merge" also put it inside (as an empty slot).
                const bool inside = widening.count(b) > 0
                    || (isSplit ? side == InsertAfter : side == InsertBefore);
                if (right) {
                    if (g < col) return g;
                    if (g == col) return isSplit ? (inside ? g : g + 1) : (inside ? g + 1 : g);
                    return g <= k ? g + 1 : g;
                }
                // Left: blocks in (k, col) move one column left, the new column is col - 1.
                if (g > col) return g;
                if (g == col) return isSplit ? (inside ? col - 1 : col) : (inside ? col : col - 1);
                return g > k ? g - 1 : g;
            };
            const int split = map(section.splitCol + 1, true);
            const int merge = map(section.mergeCol < 0 ? m_numCols : section.mergeCol, false);
            if (section.hasSplitSection && merge <= split) return false;
            plan.splitGap[b] = split;
            plan.mergeGap[b] = merge;
        }
        return true;
    };

    for (int c = col; c < m_numCols; ++c) {
        if (columnEmpty(c) && remap(c, true)) {
            plan.emptyColumn = c;
            plan.right = true;
            plan.newColumn = col;
            return plan;
        }
    }
    for (int c = col - 1; c >= 0; --c) {
        if (columnEmpty(c) && remap(c, false)) {
            plan.emptyColumn = c;
            plan.right = false;
            plan.newColumn = col - 1;
            return plan;
        }
    }
    return plan;
}

int NodeCanvas::insertColumn(int row, int col, int side) {
    const InsertPlan plan = planInsertColumn(row, col, side);
    if (plan.emptyColumn < 0) {
        emit boardFull();
        return -1;
    }
    const int k = plan.emptyColumn;
    if (plan.laneOnly) {
        auto& lane = m_rows[row].plugins;
        if (plan.right) {
            for (int c = k; c > col; --c) lane[c] = lane[c - 1];
        } else {
            for (int c = k; c < col - 1; ++c) lane[c] = lane[c + 1];
        }
        lane[plan.newColumn] = nullptr;
        return plan.newColumn;
    }
    for (int b : {0, 1, 3, 4}) {
        m_rows[b].splitCol = plan.splitGap[b] - 1;
        m_rows[b].mergeCol = plan.mergeGap[b] >= m_numCols ? -1 : plan.mergeGap[b];
    }
    for (auto& r : m_rows) {
        if (plan.right) {
            for (int c = k; c > col; --c) r.plugins[c] = r.plugins[c - 1];
        } else {
            for (int c = k; c < col - 1; ++c) r.plugins[c] = r.plugins[c + 1];
        }
        r.plugins[plan.newColumn] = nullptr;
    }
    return plan.newColumn;
}

bool NodeCanvas::insertPluginBefore(int row, int col, std::shared_ptr<AudioNode> node, int insert) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return false;
    // An occupied slot (or an explicit insert marker) opens a new column:
    // later blocks in every lane move right and the signal path is unchanged.
    if (m_rows[row].plugins[col] || insert) {
        col = insertColumn(row, col, insert ? insert : InsertAuto); // may land one column left
        if (col < 0) return false;
    }
    
    m_rows[row].plugins[col] = node;

    if (row != MAIN_ROW) {
        m_rows[row].hasSplitSection = true;
        if (!m_rows[row].levelConfigured) setBranchDefaults(row, 0.25118864f);
        m_rows[row].enabled = true;
        updateBranchGains(row);
    }
    
    m_engine->addNode(node);
    applyRoutingChange();
    return true;
}

void NodeCanvas::removePluginAt(int row, int col) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    if (m_rows[row].plugins[col]) {
        emit nodeAboutToBeRemoved(m_rows[row].plugins[col].get());
        m_engine->removeNode(m_rows[row].plugins[col]->uniqueId);
        m_rows[row].plugins[col] = nullptr;
    }
    applyRoutingChange();
}

void NodeCanvas::replacePluginAt(int row, int col, std::shared_ptr<AudioNode> newNode) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    if (m_rows[row].plugins[col]) {
        emit nodeAboutToBeRemoved(m_rows[row].plugins[col].get());
        const std::string removedId = m_rows[row].plugins[col]->uniqueId;
        m_engine->removeNode(removedId);
    }
    m_rows[row].plugins[col] = newNode;
    if (newNode) {
        if (row != MAIN_ROW) {
            if (!m_rows[row].levelConfigured) setBranchDefaults(row, 0.25118864f);
            m_rows[row].enabled = true;
            updateBranchGains(row);
        }
        m_engine->addNode(newNode);
    }
    applyRoutingChange();
}

void NodeCanvas::movePlugin(int fromRow, int fromCol, int toRow, int toCol) {
    if (fromRow < 0 || fromRow >= NUM_ROWS || fromCol < 0 || fromCol >= NUM_COLS) return;
    if (toRow < 0 || toRow >= NUM_ROWS || toCol < 0 || toCol >= NUM_COLS) return;
    if (fromRow == toRow && fromCol == toCol) return;

    auto movingNode = m_rows[fromRow].plugins[fromCol];
    if (!movingNode) return;

    std::swap(m_rows[fromRow].plugins[fromCol], m_rows[toRow].plugins[toCol]);
    if (toRow != MAIN_ROW) {
        m_rows[toRow].hasSplitSection = true;
        if (!m_rows[toRow].levelConfigured) setBranchDefaults(toRow, 0.25118864f);
        m_rows[toRow].enabled = true;
        updateBranchGains(toRow);
    }

    applyRoutingChange();
}

void NodeCanvas::clearCanvas() {
    emit canvasAboutToBeCleared();
    m_numCols = MIN_COLS;
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < NUM_COLS; ++c)
            m_rows[r].plugins[c] = nullptr;
        if (r != MAIN_ROW) {
            m_rows[r].splitAfterNodeId.clear();
            m_rows[r].mergeBeforeNodeId.clear();
            m_rows[r].hasSplitSection = false;
            m_rows[r].parentRow = MAIN_ROW;
            m_rows[r].splitMode = GridRow::SplitMode::Copy;
            m_rows[r].splitPosition = 0.0f;
            m_rows[r].mainInputEnabled = true;
            m_rows[r].mainMix = 1.0f;
            m_rows[r].mix = 1.0f;
            m_rows[r].pan = 0.0f;
            m_rows[r].enabled = false;
            m_rows[r].levelConfigured = false;
            m_rows[r].polarityInverted = false;
            updateBranchGains(r);
        }
    }
    m_mainOutputEnabled = true;
    m_engine->clearGraph();
    clearSceneItems();
    for (int r = 0; r < NUM_ROWS; ++r)
        for (int c = 0; c < NUM_COLS; ++c)
            m_nodeWidgets[r][c] = nullptr;
    applyRoutingChange();
}

std::shared_ptr<AudioNode> NodeCanvas::getPluginAt(int row, int col) const {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return nullptr;
    return m_rows[row].plugins[col];
}

std::pair<int,int> NodeCanvas::findNode(const std::shared_ptr<AudioNode>& node) const {
    for (int r = 0; r < NUM_ROWS; ++r)
        for (int c = 0; c < NUM_COLS; ++c)
            if (m_rows[r].plugins[c] == node)
                return {r, c};
    return {-1, -1};
}

int NodeCanvas::findNodeColumn(int row, const std::string& nodeId) const {
    if (nodeId.empty()) return -1;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[row].plugins[c] && m_rows[row].plugins[c]->uniqueId == nodeId) return c;
    }
    return -1;
}

bool NodeCanvas::isCanonicalParent(int row, int parentRow) const {
    switch (row) {
    case 0: return parentRow == 1;
    case 1:
    case 3: return parentRow == MAIN_ROW;
    case 4: return parentRow == 3;
    default: return false;
    }
}

int NodeCanvas::splitGroupSize(int parentRow, int splitCol) const {
    int count = 0;
    for (int row : {0, 1, 3, 4}) {
        if (m_rows[row].hasSplitSection && m_rows[row].enabled && m_rows[row].parentRow == parentRow &&
            m_rows[row].splitCol == splitCol) {
            ++count;
        }
    }
    return count;
}

int NodeCanvas::splitGroupLeader(int parentRow, int splitCol) const {
    for (int row : {0, 1, 3, 4}) {
        if (m_rows[row].hasSplitSection && m_rows[row].enabled && m_rows[row].parentRow == parentRow &&
            m_rows[row].splitCol == splitCol) {
            return row;
        }
    }
    return -1;
}

int NodeCanvas::mixerGroupLeader(int parentRow, int mergeCol) const {
    for (int row : {0, 1, 3, 4}) {
        if (m_rows[row].hasSplitSection && m_rows[row].enabled && m_rows[row].parentRow == parentRow &&
            m_rows[row].mergeCol == mergeCol) {
            return row;
        }
    }
    return -1;
}

void NodeCanvas::refreshRoutingAnchors() {
    for (int row : {0, 1, 3, 4}) {
        GridRow& section = m_rows[row];
        section.splitAfterNodeId.clear();
        section.mergeBeforeNodeId.clear();
        if (!section.hasSplitSection || !isCanonicalParent(row, section.parentRow)) continue;

        for (int col = section.splitCol; col >= 0; --col) {
            if (m_rows[section.parentRow].plugins[col]) {
                section.splitAfterNodeId = m_rows[section.parentRow].plugins[col]->uniqueId;
                break;
            }
        }
        if (section.mergeCol >= 0) {
            for (int col = section.mergeCol; col < m_numCols; ++col) {
                if (m_rows[section.parentRow].plugins[col]) {
                    section.mergeBeforeNodeId = m_rows[section.parentRow].plugins[col]->uniqueId;
                    break;
                }
            }
        }
    }
}

void NodeCanvas::normalizeRoutingTopology() {
    for (int row : {0, 1, 3, 4}) {
        GridRow& section = m_rows[row];
        const int parentRow = row == 0 ? 1 : (row == 4 ? 3 : MAIN_ROW);
        section.parentRow = parentRow;

        int parentStartGap = 0;
        int parentEndGap = m_numCols;
        if (parentRow != MAIN_ROW && m_rows[parentRow].hasSplitSection) {
            parentStartGap = std::clamp(m_rows[parentRow].splitCol + 1, 0, m_numCols - 1);
            parentEndGap = m_rows[parentRow].mergeCol < 0 ? m_numCols
                : std::clamp(m_rows[parentRow].mergeCol, parentStartGap + 1, m_numCols);
        }

        int splitGap = std::clamp(section.splitCol + 1, parentStartGap, parentEndGap - 1);
        int mergeGap = section.mergeCol < 0 ? parentEndGap
            : std::clamp(section.mergeCol, parentStartGap + 1, parentEndGap);
        if (mergeGap <= splitGap) mergeGap = std::min(parentEndGap, splitGap + 1);

        section.splitCol = splitGap - 1;
        section.mergeCol = mergeGap == m_numCols ? -1 : mergeGap;
    }

    // A shared source is a fan-out, not two competing binary A/B processors.
    for (int row : {0, 1, 3, 4}) {
        if (m_rows[row].hasSplitSection && splitGroupSize(m_rows[row].parentRow, m_rows[row].splitCol) > 1) {
            m_rows[row].splitMode = GridRow::SplitMode::Copy;
        }
    }
    refreshRoutingAnchors();
}

int NodeCanvas::getSplitCol(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].splitCol : -1;
}

int NodeCanvas::getMergeCol(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].mergeCol : -1;
}

std::string NodeCanvas::getSplitAnchor(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].splitAfterNodeId : std::string{};
}

std::string NodeCanvas::getMergeAnchor(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].mergeBeforeNodeId : std::string{};
}

bool NodeCanvas::createSplitAtMainGap(int gapIndex) {
    return createSplitAtPathGap(MAIN_ROW, gapIndex);
}

bool NodeCanvas::createSplitAtPathGap(int parentRow, int gapIndex) {
    if (parentRow < 0 || parentRow >= NUM_ROWS || gapIndex < 0 || gapIndex >= m_numCols) return false;
    if (parentRow != MAIN_ROW && !m_rows[parentRow].hasSplitSection) return false;
    if (parentRow != MAIN_ROW) {
        const int parentStartGap = m_rows[parentRow].splitCol + 1;
        const int parentEndGap = m_rows[parentRow].mergeCol < 0 ? m_numCols : m_rows[parentRow].mergeCol;
        if (gapIndex < parentStartGap || gapIndex >= parentEndGap) return false;
    }

    int row = -1;
    if (parentRow == MAIN_ROW) {
        if (!m_rows[1].hasSplitSection) {
            row = 1;
        } else if (!m_rows[3].hasSplitSection) {
            row = 3;
        } else {
            return false;
        }
    } else if (parentRow == 1) {
        if (!m_rows[0].hasSplitSection) {
            row = 0;
        } else {
            return false;
        }
    } else if (parentRow == 3) {
        if (!m_rows[4].hasSplitSection) {
            row = 4;
        } else {
            return false;
        }
    }

    if (row < 0) return false;

    GridRow& section = m_rows[row];
    section.hasSplitSection = true;
    section.parentRow = parentRow;
    section.enabled = true;
    section.name = (row == 1 || row == 0) ? "Path B" : "Path C";
    section.splitCol = gapIndex - 1;
    section.mergeCol = -1;
    section.splitMode = GridRow::SplitMode::Copy;
    section.splitPosition = 0.0f;
    section.mainInputEnabled = true;
    section.mainMix = 1.0f;
    section.polarityInverted = false;
    setBranchDefaults(row, 1.0f);
    applyRoutingChange();
    selectRoutingNode(row, true);
    return true;
}

void NodeCanvas::removeSplitSection(int row) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;

    beginRoutingUpdate();
    std::function<void(int)> removeSection = [&](int sectionRow) {
        for (int child : {0, 1, 3, 4}) {
            if (child != sectionRow && m_rows[child].hasSplitSection && m_rows[child].parentRow == sectionRow) {
                removeSection(child);
            }
        }
        for (int c = 0; c < NUM_COLS; ++c) {
            if (m_rows[sectionRow].plugins[c]) {
                emit nodeAboutToBeRemoved(m_rows[sectionRow].plugins[c].get());
                m_engine->removeNode(m_rows[sectionRow].plugins[c]->uniqueId);
            }
        }
        m_rows[sectionRow] = GridRow{};
        m_rows[sectionRow].parentRow = sectionRow == 0 ? 1 : (sectionRow == 4 ? 3 : MAIN_ROW);
        updateBranchGains(sectionRow);
    };
    removeSection(row);
    m_routingUpdatePending = true;
    endRoutingUpdate();
}

bool NodeCanvas::hasSplitSection(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) && m_rows[row].hasSplitSection;
}

void NodeCanvas::setSplitSectionPresent(int row, bool present) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].hasSplitSection = present;
    applyRoutingChange();
}

int NodeCanvas::getSplitParentRow(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].parentRow : MAIN_ROW;
}

void NodeCanvas::setSplitParentRow(int row, int parentRow) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS || !isCanonicalParent(row, parentRow)) return;
    if (m_rows[row].parentRow == parentRow) return;
    m_rows[row].parentRow = parentRow;
    applyRoutingChange();
}

GridRow::SplitMode NodeCanvas::getSplitMode(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].splitMode : GridRow::SplitMode::Copy;
}

void NodeCanvas::setSplitMode(int row, GridRow::SplitMode mode) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].splitMode = mode;
    applyRoutingChange();
    updateLayout();
}

float NodeCanvas::getSplitPosition(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].splitPosition : 0.0f;
}

void NodeCanvas::setSplitPosition(int row, float position) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].splitPosition = std::clamp(position, -1.0f, 1.0f);
    applyRoutingChange();
    updateLayout();
}

bool NodeCanvas::isMainInputEnabled(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) && m_rows[row].mainInputEnabled;
}

void NodeCanvas::setMainInputEnabled(int row, bool enabled) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const GridRow& section = m_rows[row];
    for (int r : {0, 1, 3, 4}) {
        if (m_rows[r].hasSplitSection && m_rows[r].parentRow == section.parentRow &&
            m_rows[r].splitCol == section.splitCol) {
            m_rows[r].mainInputEnabled = enabled;
        }
    }
    applyRoutingChange();
    updateLayout();
}

bool NodeCanvas::isSharedSplitJunction(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS || !m_rows[row].hasSplitSection || !m_rows[row].enabled) return false;
    return splitGroupSize(m_rows[row].parentRow, m_rows[row].splitCol) > 1;
}

bool NodeCanvas::isSharedMixerJunction(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS || !m_rows[row].hasSplitSection) return false;
    int count = 0;
    for (int candidate : {0, 1, 3, 4}) {
        if (m_rows[candidate].hasSplitSection && m_rows[candidate].parentRow == m_rows[row].parentRow &&
            m_rows[candidate].mergeCol == m_rows[row].mergeCol) {
            ++count;
        }
    }
    return count > 1;
}

std::vector<int> NodeCanvas::getMixerGroupRows(int row) const {
    std::vector<int> rows;
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS || !m_rows[row].hasSplitSection) return rows;
    for (int candidate : {0, 1, 3, 4}) {
        if (m_rows[candidate].hasSplitSection && m_rows[candidate].parentRow == m_rows[row].parentRow &&
            m_rows[candidate].mergeCol == m_rows[row].mergeCol) {
            rows.push_back(candidate);
        }
    }
    return rows;
}

float NodeCanvas::getMainMix(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].mainMix : 1.0f;
}

void NodeCanvas::setMainMix(int row, float level) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const GridRow& section = m_rows[row];
    const float clamped = std::clamp(level, 0.0f, 2.0f);
    for (int r : {0, 1, 3, 4}) {
        if (m_rows[r].hasSplitSection && m_rows[r].parentRow == section.parentRow &&
            m_rows[r].mergeCol == section.mergeCol) {
            m_rows[r].mainMix = clamped;
        }
    }
    applyRoutingChange();
    updateLayout();
}

void NodeCanvas::nodeDoubleClicked(NodeWidget* node) {
    emit editPluginUI(node->getAudioNode());
}

void NodeCanvas::setSystemChannelModes(bool inputStereo, bool outputStereo) {
    if (m_sysInputWidget) m_sysInputWidget->setChannelMode(inputStereo);
    if (m_sysOutputWidget) m_sysOutputWidget->setChannelMode(outputStereo);
}

void NodeCanvas::onPlusButtonClicked(int row, int col, QPoint screenPos, int insert) {
    emit plusButtonClicked(row, col, screenPos, insert);
}

// ─── Layout Engine ────────────────────────────────────────────────────────────
int NodeCanvas::activeLaneSteps() const {
    const int topSteps = m_rows[0].hasSplitSection ? 2 : (m_rows[1].hasSplitSection ? 1 : 0);
    const int bottomSteps = m_rows[4].hasSplitSection ? 2 : (m_rows[3].hasSplitSection ? 1 : 0);
    return topSteps + bottomSteps;
}

qreal NodeCanvas::canvasHeightFor(qreal viewportHeight) const {
    const int steps = activeLaneSteps();
    const qreal minimumHeight = CanvasMetrics::requiredHeight(
        steps + 1, CanvasMetrics::cardHeight, CanvasMetrics::minimumLaneHeight);
    return std::max(viewportHeight, minimumHeight);
}

void NodeCanvas::calculateRowCenters(qreal rowCenters[NUM_ROWS], qreal H) const {
    const int steps = activeLaneSteps();
    qreal laneHeight = CanvasMetrics::preferredLaneHeight;
    if (steps > 0) {
        const qreal availableHeight = H - 2.0 * CanvasMetrics::marginY - CanvasMetrics::cardHeight;
        laneHeight = std::clamp(availableHeight / steps,
                                CanvasMetrics::minimumLaneHeight,
                                CanvasMetrics::preferredLaneHeight);
    }

    qreal offset[NUM_ROWS] = {0.0};
    if (m_rows[1].hasSplitSection) {
        offset[1] = -laneHeight;
        offset[0] = m_rows[0].hasSplitSection ? -2.0 * laneHeight : -laneHeight;
    }
    if (m_rows[3].hasSplitSection) {
        offset[3] = laneHeight;
        offset[4] = m_rows[4].hasSplitSection ? 2.0 * laneHeight : laneHeight;
    }

    qreal minY = 0.0;
    qreal maxY = 0.0;
    for (int r = 0; r < NUM_ROWS; ++r) {
        if (r == MAIN_ROW || m_rows[r].hasSplitSection) {
            minY = std::min(minY, offset[r]);
            maxY = std::max(maxY, offset[r]);
        }
    }

    const qreal mainY = H / 2.0 - (minY + maxY) / 2.0;
    for (int r = 0; r < NUM_ROWS; ++r) rowCenters[r] = mainY + offset[r];
}

qreal NodeCanvas::getRowCenterY(int row) const {
    if (row < 0 || row >= NUM_ROWS) return 0.0;
    qreal rowCenters[NUM_ROWS];
    qreal H = m_scene->sceneRect().height();
    if (H <= 0.0) {
        H = canvasHeightFor(viewport()->height());
    }
    calculateRowCenters(rowCenters, H);
    return rowCenters[row];
}

void NodeCanvas::updateLayout() {
    scheduleAutoFit();
    clearSceneItems();

    int highestUsedCol = getHighestOccupiedCol();
    m_numCols = std::clamp(m_numCols, std::max(4, highestUsedCol + 1), MAX_COLS);

    qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : SYS_NODE_W;
    qreal outputW = m_sysOutputWidget ? m_sysOutputWidget->width() : SYS_NODE_W;
    
    // Calculate required width and visible viewport width in scene coordinates
    qreal viewW = viewport()->width() > 0 ? (qreal)viewport()->width() : 1000.0;
    qreal visibleSceneW = (m_zoomLevel > 0.0) ? (viewW / m_zoomLevel) : viewW;

    qreal requiredCanvasW = CanvasMetrics::requiredWidth(m_numCols, std::max(inputW, outputW));
    
    bool fitsInViewport = (requiredCanvasW <= visibleSceneW);
    // The grid itself never stretches: keeping this at its required width makes
    // logical gaps align with the system edges at every viewport size.
    qreal W = requiredCanvasW;

    if (fitsInViewport) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    
    qreal H = canvasHeightFor(viewport()->height());

    m_scene->setSceneRect(0, 0, W, H);

    qreal rowCenters[NUM_ROWS];
    calculateRowCenters(rowCenters, H);

    qreal sysCY = rowCenters[MAIN_ROW];

    qreal inputH = m_sysInputWidget ? m_sysInputWidget->height() : SYS_NODE_H;
    qreal outputH = m_sysOutputWidget ? m_sysOutputWidget->height() : SYS_NODE_H;

    if (m_sysInputWidget) {
        m_sysInputWidget->setPos(MARGIN_X, sysCY - inputH / 2);
    }
    if (m_sysOutputWidget) {
        m_sysOutputWidget->setPos(W - MARGIN_X - outputW, sysCY - outputH / 2);
    }

    qreal trackLeft  = MARGIN_X + inputW + CanvasMetrics::clearGap;
    qreal trackRight = W - MARGIN_X - outputW - CanvasMetrics::clearGap;
    qreal trackW     = trackRight - trackLeft;

    qreal sysRightX  = MARGIN_X + inputW;
    qreal sysLeftX   = W - MARGIN_X - outputW;
    qreal sysMidY    = sysCY;

    // 1. Layout main row (r == MAIN_ROW) first so that side rows can query its node positions
    layoutRow(MAIN_ROW, rowCenters[MAIN_ROW], trackLeft, trackRight, trackW, sysRightX, sysLeftX, sysMidY);

    // 2. Layout level 1 side rows (r == 1 and r == 3)
    layoutRow(1, rowCenters[1], trackLeft, trackRight, trackW, sysRightX, sysLeftX, rowCenters[m_rows[1].parentRow]);
    layoutRow(3, rowCenters[3], trackLeft, trackRight, trackW, sysRightX, sysLeftX, rowCenters[m_rows[3].parentRow]);

    // 3. Layout level 2 side rows (r == 0 and r == 4)
    layoutRow(0, rowCenters[0], trackLeft, trackRight, trackW, sysRightX, sysLeftX, rowCenters[m_rows[0].parentRow]);
    layoutRow(4, rowCenters[4], trackLeft, trackRight, trackW, sysRightX, sysLeftX, rowCenters[m_rows[4].parentRow]);

    auto* splitTool = new SplitToolItem(this);
    splitTool->setPos(MARGIN_X,
                      sysCY - CanvasMetrics::cardHeight / 2.0
                          - CanvasMetrics::splitToolHeight - 6.0);
    m_scene->addItem(splitTool);
    m_dynamicItems.push_back(splitTool);
    
    // ── If main row is empty but other rows have blocks, draw trunk ──────────
    if (m_rows[MAIN_ROW].isEmpty()) {
        bool othersHaveBlocks = !m_rows[0].isEmpty() || !m_rows[1].isEmpty() || !m_rows[3].isEmpty() || !m_rows[4].isEmpty();
        if (othersHaveBlocks) {
            auto* wire = new QGraphicsPathItem();
            QPainterPath path;
            path.moveTo(sysRightX, sysMidY);
            path.lineTo(sysLeftX, sysMidY);
            wire->setPath(path);
            wire->setPen(QPen(QColor(0, 176, 255, 40), 1.5, Qt::DashLine));
            wire->setZValue(-3);
            m_scene->addItem(wire);
            m_dynamicItems.push_back(wire);

        }
    }

    // ── Small dot markers on system node edges ──────────────────────────
    auto makeDot = [&](qreal x, qreal y, QColor col) {
        auto* dot = new QGraphicsEllipseItem(x - 4, y - 4, 8, 8);
        dot->setBrush(col);
        dot->setPen(Qt::NoPen);
        dot->setZValue(5);
        m_scene->addItem(dot);
        m_dynamicItems.push_back(dot);
    };
    if (m_sysInputWidget) {
        makeDot(sysRightX, sysMidY, QColor(0, 200, 120));
    }
    if (m_sysOutputWidget) {
        makeDot(sysLeftX, sysMidY, QColor(0, 200, 120));
    }

    if (fitsInViewport) {
        m_scene->setSceneRect(0, 0, W, H);
    } else {
        m_scene->setSceneRect(QRectF(0, 0, W, H).united(m_scene->itemsBoundingRect()));
    }
}

qreal NodeCanvas::getGapX(int gapIdx) const {
    qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : SYS_NODE_W;
    const qreal trackLeft = MARGIN_X + inputW + CanvasMetrics::clearGap;
    return CanvasMetrics::gapX(trackLeft, gapIdx, m_numCols);
}

void NodeCanvas::layoutRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW, qreal sysRightX, qreal sysLeftX, qreal sysMidY) {
    const GridRow& row = m_rows[r];
    if (r != MAIN_ROW && !row.hasSplitSection) return;

    auto getColX = [&](int col) -> qreal {
        return CanvasMetrics::columnX(trackLeft, col);
    };

    qreal startX = sysRightX;
    qreal endX = sysLeftX;
    qreal parentSplitX = trackLeft;
    qreal parentMergeX = trackRight;
    int minSlot = 0;
    int maxSlot = m_numCols - 1;

    if (r != MAIN_ROW) {
        int parentRow = row.parentRow;
        int splitCol = getSplitCol(r);
        parentSplitX = (splitCol < 0) ? getGapX(0) : getGapX(splitCol + 1);
        if (parentRow != MAIN_ROW && parentRow >= 0 && parentRow < NUM_ROWS) {
            parentSplitX = std::max(parentSplitX, m_rows[parentRow].parentSplitX);
        }

        int mergeCol = getMergeCol(r);
        parentMergeX = (mergeCol < 0) ? getGapX(m_numCols) : getGapX(mergeCol);
        if (parentRow != MAIN_ROW && parentRow >= 0 && parentRow < NUM_ROWS) {
            parentMergeX = std::min(parentMergeX, m_rows[parentRow].parentMergeX);
        }

        minSlot = 0;
        while (minSlot < m_numCols && getColX(minSlot) < parentSplitX - 10.0) {
            minSlot++;
        }

        maxSlot = m_numCols - 1;
        while (maxSlot >= 0 && getColX(maxSlot) + PLUG_NODE_W > parentMergeX + 10.0) {
            maxSlot--;
        }

        startX = parentSplitX;
        endX = parentMergeX;
    }

    // Clear stale node widgets not in active range
    for (int c = 0; c < m_numCols; ++c) {
        bool isActiveSlot = (r == MAIN_ROW) || (c >= minSlot && c <= maxSlot);
        if ((!row.plugins[c] || !isActiveSlot) && m_nodeWidgets[r][c]) {
            m_scene->removeItem(m_nodeWidgets[r][c]);
            delete m_nodeWidgets[r][c];
            m_nodeWidgets[r][c] = nullptr;
        }
    }

    // Place node widgets at fixed column positions
    for (int c = 0; c < m_numCols; ++c) {
        bool isActiveSlot = (r == MAIN_ROW) || (c >= minSlot && c <= maxSlot);
        if (isActiveSlot && row.plugins[c]) {
            if (!m_nodeWidgets[r][c]) {
                m_nodeWidgets[r][c] = new NodeWidget(row.plugins[c]);
                m_scene->addItem(m_nodeWidgets[r][c]);
            }
            qreal nodeH = m_nodeWidgets[r][c]->height();
            m_nodeWidgets[r][c]->setPos(getColX(c), cy - nodeH / 2.0);
        }
    }

    // Save evaluated positions into GridRow struct for child rows
    m_rows[r].parentSplitX = parentSplitX;
    m_rows[r].parentMergeX = parentMergeX;

    auto getWirePen = [&](int rowIdx) -> QPen {
        if (rowIdx == MAIN_ROW) {
            return QPen(QColor(0, 176, 255, 180), 2.2, Qt::SolidLine, Qt::RoundCap);
        }
        const GridRow& path = m_rows[rowIdx];
        if (!path.enabled || !path.hasSplitSection) {
            return QPen(QColor(70, 70, 80, 70), 1.2, Qt::DashLine, Qt::RoundCap);
        }

        const float splitGainFactor = branchSplitGain(rowIdx);

        float mixGainFactor = getMix(rowIdx);
        float effectiveGain = splitGainFactor * mixGainFactor;

        if (effectiveGain <= 0.05f) {
            return QPen(QColor(60, 70, 85, 90), 1.2, Qt::DashLine, Qt::RoundCap);
        } else if (effectiveGain < 0.95f) {
            qreal w = 1.2 + effectiveGain * 0.8;
            int alpha = qRound(100 + effectiveGain * 100);
            return QPen(QColor(0, 160, 230, alpha), w, Qt::SolidLine, Qt::RoundCap);
        } else if (effectiveGain <= 1.05f) {
            return QPen(QColor(53, 199, 255, 220), 2.2, Qt::SolidLine, Qt::RoundCap);
        } else if (effectiveGain <= 2.0f) {
            qreal w = 2.4 + (effectiveGain - 1.0f) * 1.2;
            return QPen(QColor(255, 200, 50, 240), w, Qt::SolidLine, Qt::RoundCap);
        } else {
            qreal w = 3.6 + std::min(1.4f, (effectiveGain - 2.0f) * 0.5f);
            return QPen(QColor(255, 60, 60, 255), w, Qt::SolidLine, Qt::RoundCap);
        }
    };

    // Draw horizontal wire segment
    {
        qreal segStartX = (r == MAIN_ROW) ? sysRightX : startX;
        qreal segEndX   = (r == MAIN_ROW) ? sysLeftX  : endX;
        auto* seg = new QGraphicsPathItem();
        QPainterPath path;
        path.moveTo(segStartX, cy);
        path.lineTo(segEndX, cy);
        seg->setPath(path);
        seg->setPen(getWirePen(r));
        seg->setZValue(-2);
        m_scene->addItem(seg);
        m_dynamicItems.push_back(seg);
        if (r != MAIN_ROW) m_routeItems[r].wire = seg;
    }

    // Draw branch connectors for side rows
    if (r != MAIN_ROW && row.hasSplitSection) {
        // Split connector
        auto* branch = new QGraphicsPathItem();
        QPainterPath bpath;
        bpath.moveTo(startX, cy);
        bpath.lineTo(parentSplitX, cy);
        bpath.lineTo(parentSplitX, sysMidY);
        branch->setPath(bpath);
        branch->setPen(getWirePen(r));
        branch->setZValue(-3);
        m_scene->addItem(branch);
        m_dynamicItems.push_back(branch);

        auto* splitHandle = new RoutingHandleItem(this, r, true);
        qreal splitY = (sysMidY + cy) / 2.0;
        splitHandle->setPos(parentSplitX, splitY);
        m_scene->addItem(splitHandle);
        m_dynamicItems.push_back(splitHandle);

        // Merge connector
        auto* branchR = new QGraphicsPathItem();
        QPainterPath bpathR;
        bpathR.moveTo(endX, cy);
        bpathR.lineTo(parentMergeX, cy);
        bpathR.lineTo(parentMergeX, sysMidY);
        branchR->setPath(bpathR);
        branchR->setPen(getWirePen(r));
        branchR->setZValue(-3);
        m_scene->addItem(branchR);
        m_dynamicItems.push_back(branchR);

        auto* mergeHandle = new RoutingHandleItem(this, r, false);
        qreal mergeY = (sysMidY + cy) / 2.0;
        mergeHandle->setPos(parentMergeX, mergeY);
        m_scene->addItem(mergeHandle);
        m_dynamicItems.push_back(mergeHandle);

        RouteItems& items = m_routeItems[r];
        items.splitPath = branch;
        items.mergePath = branchR;
        items.splitHandle = splitHandle;
        items.mergeHandle = mergeHandle;
        items.cy = cy;
        items.parentY = sysMidY;
        items.splitX = items.shownSplitX = parentSplitX;
        items.mergeX = items.shownMergeX = parentMergeX;
    }

    // Place plus buttons centered in unoccupied slot spaces up to m_numCols
    for (int c = 0; c < m_numCols; ++c) {
        bool isActiveSlot = (r == MAIN_ROW) || (c >= minSlot && c <= maxSlot);
        bool isOccupied = (row.plugins[c] != nullptr);
        if (isActiveSlot && !isOccupied) {
            auto* plus = new PlusButtonWidget(r, c, PlusButtonWidget::Style::Ghost);
            plus->setHomePos(QPointF(getColX(c) + PLUG_NODE_W / 2.0, cy));
            m_scene->addItem(plus);
            m_dynamicItems.push_back(plus);
        }
    }

    // Insert markers on boundaries next to a block where no free slot offers
    // the same spot: between two blocks, at the start or end of the lane, and
    // where a parallel path splits off or merges back. Adding there opens a
    // column and pushes later blocks right.
    const int firstSlot = (r == MAIN_ROW) ? 0 : minSlot;
    const int lastSlot = (r == MAIN_ROW) ? m_numCols - 1 : maxSlot;
    std::set<int> routingGaps, routingSplits;
    for (int b : {0, 1, 3, 4}) {
        const GridRow& child = m_rows[b];
        if (b == r || !child.hasSplitSection || child.parentRow != r) continue;
        routingGaps.insert(child.splitCol + 1);
        routingSplits.insert(child.splitCol + 1);
        routingGaps.insert(child.mergeCol < 0 ? m_numCols : child.mergeCol);
    }
    for (int g = firstSlot; g <= lastSlot + 1; ++g) {
        const bool leftTaken = g - 1 >= firstSlot && row.plugins[g - 1];
        const bool rightTaken = g <= lastSlot && row.plugins[g];
        const bool routing = routingGaps.count(g) > 0;
        const bool atStart = g == firstSlot;
        const bool atEnd = g == lastSlot + 1;
        const qreal x = getColX(g) - CanvasMetrics::clearGap / 2.0;
        auto addMarker = [&](int mode, qreal offset, const QString& tip) {
            auto* marker = new PlusButtonWidget(r, g, PlusButtonWidget::Style::Insert);
            marker->setInsertMode(mode);
            if (!tip.isEmpty()) marker->setToolTip(tip);
            marker->setHomePos(QPointF(x + offset, cy));
            m_scene->addItem(marker);
            m_dynamicItems.push_back(marker);
        };
        if (routing) {
            // A split or merge sits in this gap: one marker on each side of it,
            // so a block can go before or after the junction on this lane.
            const bool splitHere = routingSplits.count(g) > 0;
            const QString what = splitHere ? "split" : "merge";
            // At the very start or end of the lane there is no block on that
            // side, but the spot still exists: a path that merges back right
            // before the output still takes a block after the merge.
            if (!leftTaken && !rightTaken) continue;  // empty slots already offer both spots
            if (leftTaken || atStart) addMarker(InsertBefore, -7.0, QString("Insert before the %1").arg(what));
            if (rightTaken || atEnd) addMarker(InsertAfter, 7.0, QString("Insert after the %1").arg(what));
            continue;
        }
        if (!((leftTaken && (rightTaken || atEnd)) || (rightTaken && atStart))) continue;
        addMarker(InsertAuto, 0.0, QString());
    }
}

void NodeCanvas::rebuildAudioConnections() {
    m_engine->suspendProcessing();
    // Routing edits only replace connections. Re-adding every processor would
    // call prepare(), destroying live LV2 instances while JACK may be using them.
    m_engine->clearConnections();
    normalizeRoutingTopology();
    
    // Connect dry bypass for the primary middle row (r == 1) if it contains no active blocks
    bool middleRowEmpty = true;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[MAIN_ROW].plugins[c]) {
            middleRowEmpty = false;
            break;
        }
    }
    if (middleRowEmpty && m_mainOutputEnabled) {
        const float gain = pathSplitGainBetween(MAIN_ROW, -1, m_numCols) * pathMixerGainBetween(MAIN_ROW, -1, m_numCols);
        m_engine->connectPorts("system_input", 0, "system_output", 0, gain);
        m_engine->connectPorts("system_input", 1, "system_output", 1, gain);
    }
    
    auto connect = [&](const std::string& srcId, int si, const std::string& dstId, int di,
                       float gain = 1.0f, std::shared_ptr<std::atomic<float>> liveGain = nullptr) {
        m_engine->connectPorts(srcId, si, dstId, di, gain, std::move(liveGain));
    };

    auto connectNodes = [&](const std::shared_ptr<AudioNode>& src, const std::shared_ptr<AudioNode>& dst, float gain = 1.0f) {
        int outs = src->getAudioOutputCount();
        int ins  = dst->getAudioInputCount();
        if (outs >= 2 && ins >= 2) {
            connect(src->uniqueId, 0, dst->uniqueId, 0, gain);
            connect(src->uniqueId, 1, dst->uniqueId, 1, gain);
        } else if (outs >= 2 && ins == 1) {
            connect(src->uniqueId, 0, dst->uniqueId, 0, gain);
            connect(src->uniqueId, 1, dst->uniqueId, 0, gain);
        } else {
            connect(src->uniqueId, 0, dst->uniqueId, 0, gain);
            if (ins >= 2)
                connect(src->uniqueId, 0, dst->uniqueId, 1, gain);
        }
    };

    // ─── Connect Main Row (MAIN_ROW) ───
    std::vector<std::pair<int, std::shared_ptr<AudioNode>>> mainChain;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[MAIN_ROW].plugins[c]) mainChain.emplace_back(c, m_rows[MAIN_ROW].plugins[c]);
    }

    if (!mainChain.empty()) {
        const float inputGain = pathSplitGainBetween(MAIN_ROW, -1, mainChain[0].first) *
                                pathMixerGainBetween(MAIN_ROW, -1, mainChain[0].first);
        connect("system_input", 0, mainChain[0].second->uniqueId, 0, inputGain);
        if (mainChain[0].second->getAudioInputCount() > 1)
            connect("system_input", 1, mainChain[0].second->uniqueId, 1, inputGain);

        for (size_t i = 0; i + 1 < mainChain.size(); ++i) {
            const float gain = pathSplitGainBetween(MAIN_ROW, mainChain[i].first, mainChain[i + 1].first) *
                               pathMixerGainBetween(MAIN_ROW, mainChain[i].first, mainChain[i + 1].first);
            connectNodes(mainChain[i].second, mainChain[i + 1].second, gain);
        }

        if (m_mainOutputEnabled) {
            const auto& last = mainChain.back();
            int outs = last.second->getAudioOutputCount();
            const float outputGain = pathSplitGainBetween(MAIN_ROW, last.first, m_numCols) *
                                     pathMixerGainBetween(MAIN_ROW, last.first, m_numCols);
            connect(last.second->uniqueId, 0, "system_output", 0, outputGain);
            if (outs >= 2)
                connect(last.second->uniqueId, 1, "system_output", 1, outputGain);
            else
                connect(last.second->uniqueId, 0, "system_output", 1, outputGain);
        }
    }

    struct RouteEndpoint {
        std::string id;
        int channels;
    };
    std::function<RouteEndpoint(int)> pathInput;
    std::function<RouteEndpoint(int)> pathOutput;
    std::function<int(int)> pathInputColumn;
    pathInput = [&](int row) -> RouteEndpoint {
        if (row == MAIN_ROW) return {"system_input", 2};
        const GridRow& path = m_rows[row];
        const int splitCol = getSplitCol(row);
        if (splitCol >= 0) {
            for (int c = splitCol; c >= 0; --c) {
                if (m_rows[path.parentRow].plugins[c]) {
                    const auto& node = m_rows[path.parentRow].plugins[c];
                    return {node->uniqueId, node->getAudioOutputCount()};
                }
            }
        }
        return pathInput(path.parentRow);
    };
    pathOutput = [&](int row) -> RouteEndpoint {
        if (row == MAIN_ROW) return {"system_output", 2};
        const GridRow& path = m_rows[row];
        const int mergeCol = getMergeCol(row);
        if (mergeCol >= 0) {
            for (int c = mergeCol; c < NUM_COLS; ++c) {
                if (m_rows[path.parentRow].plugins[c]) {
                    const auto& node = m_rows[path.parentRow].plugins[c];
                    return {node->uniqueId, node->getAudioInputCount()};
                }
            }
        }
        return pathOutput(path.parentRow);
    };
    pathInputColumn = [&](int row) -> int {
        if (row == MAIN_ROW) return -1;
        const GridRow& path = m_rows[row];
        for (int c = path.splitCol; c >= 0; --c) {
            if (m_rows[path.parentRow].plugins[c]) return c;
        }
        return pathInputColumn(path.parentRow);
    };
    auto connectBalanced = [&](const RouteEndpoint& src, const RouteEndpoint& dst,
                               const BranchGainControls& gains, float fixedGain = 1.0f) {
        if (src.channels >= 2 && dst.channels >= 2) {
            connect(src.id, 0, dst.id, 0, fixedGain, gains.left);
            connect(src.id, 1, dst.id, 1, fixedGain, gains.right);
        } else if (src.channels >= 2 && dst.channels == 1) {
            connect(src.id, 0, dst.id, 0, fixedGain * 0.5f, gains.left);
            connect(src.id, 1, dst.id, 0, fixedGain * 0.5f, gains.right);
        } else if (dst.channels >= 2) {
            connect(src.id, 0, dst.id, 0, fixedGain, gains.left);
            connect(src.id, 0, dst.id, 1, fixedGain, gains.right);
        } else {
            connect(src.id, 0, dst.id, 0, fixedGain, gains.mono);
        }
    };
    auto connectFixed = [&](const RouteEndpoint& src, const std::shared_ptr<AudioNode>& dst, float gain) {
        const int inputs = dst->getAudioInputCount();
        if (src.channels >= 2 && inputs >= 2) {
            connect(src.id, 0, dst->uniqueId, 0, gain);
            connect(src.id, 1, dst->uniqueId, 1, gain);
        } else if (src.channels >= 2) {
            connect(src.id, 0, dst->uniqueId, 0, gain * 0.5f);
            connect(src.id, 1, dst->uniqueId, 0, gain * 0.5f);
        } else {
            connect(src.id, 0, dst->uniqueId, 0, gain);
            if (inputs >= 2) connect(src.id, 0, dst->uniqueId, 1, gain);
        }
    };

    // ─── Connect Side Rows (Row 0 and Row 2) ───
    for (int r : {0, 1, 3, 4}) {
        const GridRow& row = m_rows[r];
        if (!row.hasSplitSection || !row.enabled) continue;
        updateBranchGains(r);
        std::vector<std::pair<int, std::shared_ptr<AudioNode>>> chain;
        const int splitGap = row.splitCol + 1;
        const int mergeGap = row.mergeCol < 0 ? m_numCols : row.mergeCol;
        for (int c = 0; c < NUM_COLS; ++c) {
            if (c >= splitGap && c < mergeGap && row.plugins[c]) chain.emplace_back(c, row.plugins[c]);
        }

        const int splitCol = getSplitCol(r);
        const int mergeCol = getMergeCol(r);

        if (splitCol >= 0 && mergeCol >= 0 && splitCol >= mergeCol) continue;

        const RouteEndpoint source = pathInput(r);
        const RouteEndpoint destination = pathOutput(r);
        const int sourceCol = pathInputColumn(r);

        if (chain.empty()) {
            const float pathGain = branchSplitGain(r) *
                pathSplitGainBetween(row.parentRow, sourceCol, splitCol, r) *
                pathMixerGainBetween(row.parentRow, sourceCol, splitCol) *
                pathSplitGainBetween(r, splitCol, mergeGap) *
                pathMixerGainBetween(r, splitCol, mergeGap);
            connectBalanced(source, destination, m_branchGains[r], pathGain);
        }

        if (chain.empty()) continue;

        // Connect chain internal links
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const float gain = pathSplitGainBetween(r, chain[i].first, chain[i + 1].first) *
                               pathMixerGainBetween(r, chain[i].first, chain[i + 1].first);
            connectNodes(chain[i].second, chain[i + 1].second, gain);
        }

        // Connect first node input (Split point)
        const float inputGain = branchSplitGain(r) *
            pathSplitGainBetween(row.parentRow, sourceCol, splitCol, r) *
            pathMixerGainBetween(row.parentRow, sourceCol, splitCol) *
            pathSplitGainBetween(r, splitCol, chain[0].first) *
            pathMixerGainBetween(r, splitCol, chain[0].first);
        connectFixed(source, chain[0].second, inputGain);

        // Connect last node output (Merge point) with return mix gain
        const auto& last = chain.back();
        const float outputGain = pathSplitGainBetween(r, last.first, mergeGap) *
                                 pathMixerGainBetween(r, last.first, mergeGap);
        connectBalanced({last.second->uniqueId, last.second->getAudioOutputCount()}, destination, m_branchGains[r], outputGain);
    }

    m_engine->rebuildGraph();
    m_engine->resumeProcessing();
}

// ─── Scene item lifecycle ─────────────────────────────────────────────────────
void NodeCanvas::clearSceneItems() {
    m_targetPositions.clear();
    if (m_animationTimer) m_animationTimer->stop();
    if (m_routeAnim) m_routeAnim->stop();
    for (auto& items : m_routeItems) items = RouteItems{};

    for (auto* item : m_dynamicItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_dynamicItems.clear();
    
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < NUM_COLS; ++c) {
            if (m_nodeWidgets[r][c]) {
                m_scene->removeItem(m_nodeWidgets[r][c]);
                delete m_nodeWidgets[r][c];
                m_nodeWidgets[r][c] = nullptr;
            }
        }
    }
}

// ─── Events ───────────────────────────────────────────────────────────────────
bool NodeCanvas::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_zoomOverlay && event->type() == QEvent::Resize) {
        const int x = viewport()->width() - m_zoomOverlay->width() - 16;
        const int y = viewport()->height() - m_zoomOverlay->height() - 16;
        m_zoomOverlay->move(x, y);
    }
    return QGraphicsView::eventFilter(watched, event);
}

void NodeCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    updateLayout();
    updateZoomOverlayPos();
    scheduleAutoFit();
}

void NodeCanvas::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    // Keep the canvas calm: only fixed-grid columns and the primary lane cue.
    const qreal trackLeft = MARGIN_X + SYS_NODE_W + CanvasMetrics::clearGap;
    painter->setPen(QPen(QColor(88, 94, 108, 24), 1));
    for (int c = 0; c < m_numCols; ++c) {
        const qreal x = CanvasMetrics::columnX(trackLeft, c) + CanvasMetrics::cardWidth / 2.0;
        if (x >= rect.left() && x <= rect.right()) painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    }
    painter->setPen(QPen(QColor(115, 125, 145, 24), 1));
    const qreal mainY = getRowCenterY(MAIN_ROW);
    painter->drawLine(QPointF(rect.left(), mainY), QPointF(rect.right(), mainY));
}

void NodeCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        auto selected = m_scene->selectedItems();
        for (auto* item : selected) {
            auto* nw = dynamic_cast<NodeWidget*>(item);
            if (!nw) continue;
            for (int r = 0; r < NUM_ROWS; ++r)
                for (int c = 0; c < NUM_COLS; ++c)
                    if (m_nodeWidgets[r][c] == nw)
                        removePluginAt(r, c);
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void NodeCanvas::setSplitCol(int row, int col) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].splitCol = col;
    applyRoutingChange();
}

void NodeCanvas::setMergeCol(int row, int col) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].mergeCol = col;
    applyRoutingChange();
}

void NodeCanvas::setSplitAnchor(int row, const std::string& nodeId) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const int parentRow = m_rows[row].parentRow;
    const int splitCol = nodeId.empty() ? -1 : findNodeColumn(parentRow, nodeId);
    if (!nodeId.empty() && splitCol < 0) return;
    m_rows[row].splitCol = splitCol;
    applyRoutingChange();
}

void NodeCanvas::setMergeAnchor(int row, const std::string& nodeId) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const int parentRow = m_rows[row].parentRow;
    const int mergeCol = nodeId.empty() ? -1 : findNodeColumn(parentRow, nodeId);
    if (!nodeId.empty() && mergeCol < 0) return;
    m_rows[row].mergeCol = mergeCol;
    applyRoutingChange();
}

bool NodeCanvas::isPolarityInverted(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) && m_rows[row].polarityInverted;
}

void NodeCanvas::setPolarityInverted(int row, bool inverted) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].polarityInverted = inverted;
    updateBranchGains(row);
    emit routingChanged();
}

QString NodeCanvas::getBranchName(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return {};

    const bool b1Active = m_rows[1].hasSplitSection;
    const bool b2Active = m_rows[0].hasSplitSection;
    const bool c1Active = m_rows[3].hasSplitSection;
    const bool c2Active = m_rows[4].hasSplitSection;

    if (row == 1 || row == 0) {
        if (b1Active && b2Active) {
            return row == 1 ? "Path B1" : "Path B2";
        }
        return "Path B";
    } else if (row == 3 || row == 4) {
        if (b1Active) {
            if (c1Active && c2Active) {
                return row == 3 ? "Path C1" : "Path C2";
            }
            return "Path C";
        } else {
            if (c1Active && c2Active) {
                return row == 3 ? "Path B1" : "Path B2";
            }
            return "Path B";
        }
    }
    return "Path B";
}

void NodeCanvas::setBranchName(int row, const QString& name) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const QString trimmed = name.trimmed();
    m_rows[row].name = (trimmed.isEmpty() ? (QString("Branch %1").arg(row)).toStdString() : trimmed.toStdString());
    if (m_routingUpdateDepth > 0) {
        m_routingUpdatePending = true;
        return;
    }
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::setMainOutputEnabled(bool enabled) {
    if (m_mainOutputEnabled == enabled) return;
    m_mainOutputEnabled = enabled;
    applyRoutingChange();
}

void NodeCanvas::updateBranchGains(int row) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const float level = std::clamp(m_rows[row].mix, 0.0f, 2.0f);
    const float pan = std::clamp(m_rows[row].pan, -1.0f, 1.0f);
    const float polarity = m_rows[row].polarityInverted ? -1.0f : 1.0f;
    m_branchGains[row].left->store(polarity * level * (pan > 0.0f ? 1.0f - pan : 1.0f), std::memory_order_relaxed);
    m_branchGains[row].right->store(polarity * level * (pan < 0.0f ? 1.0f + pan : 1.0f), std::memory_order_relaxed);
    m_branchGains[row].mono->store(polarity * level, std::memory_order_relaxed);
}

float NodeCanvas::branchSplitGain(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return 1.0f;
    if (isSharedSplitJunction(row)) return 1.0f;
    if (m_rows[row].splitMode == GridRow::SplitMode::Copy) return 1.0f;
    constexpr float halfPi = 1.57079632679f;
    const float p = (std::clamp(m_rows[row].splitPosition, -1.0f, 1.0f) + 1.0f) * 0.5f;
    return std::sin(p * halfPi);
}

float NodeCanvas::pathSplitGainBetween(int parentRow, int sourceCol, int destinationCol, int ignoreBranchRow) const {
    constexpr float halfPi = 1.57079632679f;
    float gain = 1.0f;
    for (int row : {0, 1, 3, 4}) {
        if (!m_rows[row].hasSplitSection || !m_rows[row].enabled || m_rows[row].parentRow != parentRow ||
            m_rows[row].splitCol < sourceCol || m_rows[row].splitCol >= destinationCol ||
            splitGroupLeader(parentRow, m_rows[row].splitCol) != row) continue;
        if (ignoreBranchRow >= 0 && m_rows[ignoreBranchRow].hasSplitSection &&
            m_rows[ignoreBranchRow].parentRow == parentRow && m_rows[ignoreBranchRow].splitCol == m_rows[row].splitCol) {
            continue;
        }
        if (!m_rows[row].mainInputEnabled) {
            gain = 0.0f;
            continue;
        }
        if (splitGroupSize(parentRow, m_rows[row].splitCol) > 1) continue;
        if (m_rows[row].splitMode == GridRow::SplitMode::Copy) continue;
        const float p = (std::clamp(m_rows[row].splitPosition, -1.0f, 1.0f) + 1.0f) * 0.5f;
        gain *= std::cos(p * halfPi);
    }
    return gain;
}

float NodeCanvas::pathMixerGainBetween(int parentRow, int sourceCol, int destinationCol) const {
    float gain = 1.0f;
    for (int row : {0, 1, 3, 4}) {
        const int mergeGap = m_rows[row].mergeCol < 0 ? m_numCols : m_rows[row].mergeCol;
        if (!m_rows[row].hasSplitSection || !m_rows[row].enabled || m_rows[row].parentRow != parentRow ||
            mergeGap <= sourceCol || mergeGap > destinationCol ||
            mixerGroupLeader(parentRow, m_rows[row].mergeCol) != row) continue;
        gain *= m_rows[row].mainInputEnabled ? std::clamp(m_rows[row].mainMix, 0.0f, 2.0f) : 0.0f;
    }
    return gain;
}

void NodeCanvas::setBranchDefaults(int row, float level) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].mix = level;
    m_rows[row].levelConfigured = true;
    updateBranchGains(row);
}

void NodeCanvas::beginRoutingUpdate() {
    ++m_routingUpdateDepth;
}

void NodeCanvas::endRoutingUpdate() {
    if (m_routingUpdateDepth == 0) return;
    if (--m_routingUpdateDepth == 0 && m_routingUpdatePending) {
        m_routingUpdatePending = false;
        rebuildAudioConnections();
        updateLayout();
        emit routingChanged();
    }
}

void NodeCanvas::applyRoutingChange(bool rebuildAudio) {
    if (m_routingUpdateDepth > 0) {
        m_routingUpdatePending = m_routingUpdatePending || rebuildAudio;
        return;
    }
    if (rebuildAudio) rebuildAudioConnections();
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::setMix(int row, float val) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].mix = std::clamp(val, 0.0f, 2.0f);
    m_rows[row].levelConfigured = true;
    updateBranchGains(row);
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::setPan(int row, float val) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].pan = std::clamp(val, -1.0f, 1.0f);
    updateBranchGains(row);
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::setBranchEnabled(int row, bool enabled) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    if (enabled && !m_rows[row].enabled && m_rows[row].isEmpty() && !m_rows[row].levelConfigured) {
        m_rows[row].mix = 0.0f;
        m_rows[row].levelConfigured = true;
        updateBranchGains(row);
    }
    m_rows[row].enabled = enabled;
    if (enabled) m_rows[row].hasSplitSection = true;
    applyRoutingChange();
}

int NodeCanvas::getBranchOutputChannels(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return 0;
    for (int c = NUM_COLS - 1; c >= 0; --c) {
        if (m_rows[row].plugins[c]) return m_rows[row].plugins[c]->getAudioOutputCount();
    }
    const int splitCol = getSplitCol(row);
    if (splitCol >= 0) {
        for (int c = splitCol; c >= 0; --c) {
            if (m_rows[m_rows[row].parentRow].plugins[c]) return m_rows[m_rows[row].parentRow].plugins[c]->getAudioOutputCount();
        }
    }
    return 2;
}

int NodeCanvas::getBranchDestinationChannels(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return 0;
    const int mergeCol = getMergeCol(row);
    if (mergeCol >= 0) {
        for (int c = mergeCol; c < NUM_COLS; ++c) {
            if (m_rows[m_rows[row].parentRow].plugins[c]) return m_rows[m_rows[row].parentRow].plugins[c]->getAudioInputCount();
        }
    }
    return 2;
}

QString NodeCanvas::getBranchStereoCollapseReason(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS || !m_rows[row].hasSplitSection) return {};

    int pathRow = m_rows[row].parentRow;
    int startCol = m_rows[row].mergeCol < 0 ? m_numCols : m_rows[row].mergeCol;
    while (pathRow >= 0 && pathRow < NUM_ROWS) {
        for (int col = startCol; col < m_numCols; ++col) {
            const auto& node = m_rows[pathRow].plugins[col];
            if (node && (node->getAudioInputCount() < 2 || node->getAudioOutputCount() < 2)) {
                return QString::fromStdString(node->getName());
            }
        }
        if (pathRow == MAIN_ROW) break;
        startCol = m_rows[pathRow].mergeCol < 0 ? m_numCols : m_rows[pathRow].mergeCol;
        pathRow = m_rows[pathRow].parentRow;
    }

    return m_engine->isHardwareOutputStereo() ? QString{} : QStringLiteral("the mono hardware output");
}

PlusButtonWidget* NodeCanvas::findPlusButton(int row, int col) const {
    for (auto* item : m_scene->items()) {
        if (auto* pb = dynamic_cast<PlusButtonWidget*>(item)) {
            if (pb->getRow() == row && pb->getCol() == col) {
                return pb;
            }
        }
    }
    return nullptr;
}

void NodeCanvas::previewInsertColumn(int row, int col) {
    // While a block hovers an insert marker, slide the blocks that would move
    // (right, or left when there is no room on the right) so the gap it will
    // land in is visible.
    const qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : SYS_NODE_W;
    const qreal trackLeft = MARGIN_X + inputW + CanvasMetrics::clearGap;
    const InsertPlan plan = col >= 0 ? planInsertColumn(row, col, m_dragGapInsert ? m_dragGapInsert : InsertAuto) : InsertPlan{};
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < m_numCols; ++c) {
            NodeWidget* nw = m_nodeWidgets[r][c];
            if (!nw || nw->isDragging()) continue;
            qreal shift = 0.0;
            if (plan.emptyColumn >= 0 && (!plan.laneOnly || r == row)) {
                if (plan.right && c >= col && c < plan.emptyColumn) shift = CanvasMetrics::columnPitch;
                if (!plan.right && c > plan.emptyColumn && c < col) shift = -CanvasMetrics::columnPitch;
            }
            setItemTargetPos(nw, QPointF(CanvasMetrics::columnX(trackLeft, c) + shift, nw->pos().y()), true);
        }
    }

    // The empty-slot dots and insert markers travel with the blocks they sit
    // between. The marker being hovered stays put: it is where the block lands.
    for (QGraphicsItem* item : m_dynamicItems) {
        auto* pb = dynamic_cast<PlusButtonWidget*>(item);
        if (!pb) continue;
        qreal shift = 0.0;
        const bool affected = plan.emptyColumn >= 0 && (!plan.laneOnly || pb->getRow() == row);
        const bool hovered = pb->getRow() == row && pb->getCol() == col && pb->isInsert();
        if (affected && !hovered) {
            const int k = plan.emptyColumn;
            const int g = pb->getCol();
            const bool slot = !pb->isInsert();
            if (plan.right && (slot ? (g >= col && g < k) : (g > col && g <= k))) {
                shift = CanvasMetrics::columnPitch;
            } else if (!plan.right && (slot ? (g > k && g < col) : (g > k + 1 && g < col))) {
                shift = -CanvasMetrics::columnPitch;
            }
        }
        setItemTargetPos(pb, pb->homePos() + QPointF(shift, 0.0), true);
    }

    m_previewInsertCol = col;
    m_previewInsertRow = row;

    // Splits and merges that the insert moves slide with the blocks.
    qreal splitX[NUM_ROWS], mergeX[NUM_ROWS];
    for (int r = 0; r < NUM_ROWS; ++r) {
        splitX[r] = m_routeItems[r].splitX;
        mergeX[r] = m_routeItems[r].mergeX;
    }
    if (plan.emptyColumn >= 0 && !plan.laneOnly) {
        auto gapX = [&](int g) { return CanvasMetrics::gapX(trackLeft, g, m_numCols); };
        for (int r : {1, 3, 0, 4}) {  // parents before their nested paths
            if (!m_routeItems[r].splitPath) continue;
            splitX[r] = gapX(plan.splitGap[r]);
            mergeX[r] = gapX(plan.mergeGap[r]);
            const int parent = m_rows[r].parentRow;
            if (parent != MAIN_ROW && parent >= 0 && parent < NUM_ROWS && m_routeItems[parent].splitPath) {
                splitX[r] = std::max(splitX[r], splitX[parent]);
                mergeX[r] = std::min(mergeX[r], mergeX[parent]);
            }
        }
    }
    animateRoutesTo(splitX, mergeX);
}

qreal NodeCanvas::shownSplitX(int row) const {
    return (row >= 0 && row < NUM_ROWS) ? m_routeItems[row].shownSplitX : 0.0;
}

qreal NodeCanvas::shownMergeX(int row) const {
    return (row >= 0 && row < NUM_ROWS) ? m_routeItems[row].shownMergeX : 0.0;
}

void NodeCanvas::setRoutePositions(const qreal splitX[5], const qreal mergeX[5]) {
    for (int r = 0; r < NUM_ROWS; ++r) {
        RouteItems& items = m_routeItems[r];
        if (!items.splitPath) continue;
        items.shownSplitX = splitX[r];
        items.shownMergeX = mergeX[r];
        const qreal handleY = (items.parentY + items.cy) / 2.0;
        QPainterPath split;
        split.moveTo(splitX[r], items.cy);
        split.lineTo(splitX[r], items.parentY);
        items.splitPath->setPath(split);
        QPainterPath merge;
        merge.moveTo(mergeX[r], items.cy);
        merge.lineTo(mergeX[r], items.parentY);
        items.mergePath->setPath(merge);
        if (items.wire) {
            QPainterPath wire;
            wire.moveTo(splitX[r], items.cy);
            wire.lineTo(mergeX[r], items.cy);
            items.wire->setPath(wire);
        }
        if (items.splitHandle) items.splitHandle->setPos(splitX[r], handleY);
        if (items.mergeHandle) items.mergeHandle->setPos(mergeX[r], handleY);
    }
}

void NodeCanvas::animateRoutesTo(const qreal splitX[5], const qreal mergeX[5]) {
    std::array<qreal, NUM_ROWS> fromS{}, fromM{}, toS{}, toM{};
    bool moves = false;
    for (int r = 0; r < NUM_ROWS; ++r) {
        fromS[r] = m_routeItems[r].shownSplitX;
        fromM[r] = m_routeItems[r].shownMergeX;
        toS[r] = splitX[r];
        toM[r] = mergeX[r];
        if (m_routeItems[r].splitPath && (fromS[r] != toS[r] || fromM[r] != toM[r])) moves = true;
    }
    if (!m_routeAnim) {
        m_routeAnim = new QVariantAnimation(this);
        m_routeAnim->setDuration(160);
        m_routeAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_routeAnim->setStartValue(0.0);
        m_routeAnim->setEndValue(1.0);
    }
    m_routeAnim->stop();
    m_routeAnim->disconnect(this);
    if (!moves) return;
    connect(m_routeAnim, &QVariantAnimation::valueChanged, this, [this, fromS, fromM, toS, toM](const QVariant& v) {
        const qreal t = v.toReal();
        qreal s[NUM_ROWS], m[NUM_ROWS];
        for (int r = 0; r < NUM_ROWS; ++r) {
            s[r] = fromS[r] + (toS[r] - fromS[r]) * t;
            m[r] = fromM[r] + (toM[r] - fromM[r]) * t;
        }
        setRoutePositions(s, m);
    });
    m_routeAnim->start();
}

void NodeCanvas::setDragGap(int row, int plusIdx, int insert) {
    const int previewCol = insert ? plusIdx : -1;
    const bool changed = previewCol != m_previewInsertCol || row != m_previewInsertRow || insert != m_dragGapInsert;
    m_dragGapRow = row;
    m_dragGapCol = plusIdx;
    m_dragGapInsert = insert;
    if (changed) previewInsertColumn(row, previewCol);
    // The block lands where the plan puts it (one column left after a left shift).
    int landCol = plusIdx;
    if (insert) {
        const InsertPlan plan = planInsertColumn(row, plusIdx, insert);
        if (plan.emptyColumn >= 0) landCol = plan.newColumn;
    }

    if (row >= 0 && row < NUM_ROWS && landCol >= 0 && landCol < NUM_COLS && m_dragPlaceholderItem) {
        qreal H = m_scene->sceneRect().height();
        if (H <= 0.0) H = canvasHeightFor(viewport()->height());
        qreal rowCenters[NUM_ROWS];
        calculateRowCenters(rowCenters, H);
        qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : SYS_NODE_W;
        qreal trackLeft = MARGIN_X + inputW + CanvasMetrics::clearGap;
        qreal slotX = CanvasMetrics::columnX(trackLeft, landCol);

        m_dragPlaceholderItem->setRect(0, 0, PLUG_NODE_W, PLUG_NODE_H);
        m_dragPlaceholderItem->setPos(slotX, rowCenters[row] - PLUG_NODE_H / 2.0);
        m_dragPlaceholderItem->show();
    }
}

void NodeCanvas::clearDragGap() {
    if (m_dragGapRow != -1 || m_dragGapCol != -1) {
        m_dragGapRow = -1;
        m_dragGapCol = -1;
        m_dragGapInsert = FillSlot;
        if (m_dragPlaceholderItem) m_dragPlaceholderItem->hide();
    }
    if (m_previewInsertCol >= 0) previewInsertColumn(-1, -1);
}

bool NodeCanvas::movePluginToGap(int fromRow, int fromCol, int toRow, int toColGap, int insert) {
    if (fromRow < 0 || fromRow >= NUM_ROWS || fromCol < 0 || fromCol >= NUM_COLS) return false;
    if (toRow < 0 || toRow >= NUM_ROWS || toColGap < 0 || toColGap > (insert ? m_numCols : NUM_COLS - 1)) return false;
    if (fromRow == toRow && fromCol == toColGap && !insert) {
        updateLayout();
        return false;
    }

    auto plugin = m_rows[fromRow].plugins[fromCol];
    if (!plugin) return false;
    if (!insert && m_rows[toRow].plugins[toColGap]) return false;
    // The insert markers right before or right after itself would only shift
    // the lane for nothing, unless the block crosses a split or merge there.
    const bool leftGap = toColGap == fromCol && insert != InsertBefore;
    const bool rightGap = toColGap == fromCol + 1 && insert != InsertAfter;
    if (insert && fromRow == toRow && (leftGap || rightGap)) {
        updateLayout();
        return false;
    }

    m_engine->suspendProcessing();

    m_rows[fromRow].plugins[fromCol] = nullptr;
    // Dropped between two blocks: open a column there. The vacated slot stays
    // empty, so nothing else moves left behind your back.
    if (insert) {
        toColGap = insertColumn(toRow, toColGap, insert); // its own freed column counts as room
        if (toColGap < 0) {
            m_rows[fromRow].plugins[fromCol] = plugin;
            m_engine->resumeProcessing();
            updateLayout();
            return false;
        }
    }
    m_rows[toRow].plugins[toColGap] = plugin;

    if (toRow != MAIN_ROW) {
        m_rows[toRow].hasSplitSection = true;
        if (!m_rows[toRow].levelConfigured) setBranchDefaults(toRow, 0.25118864f);
        m_rows[toRow].enabled = true;
        updateBranchGains(toRow);
    }

    m_engine->resumeProcessing();

    applyRoutingChange();
    return true;
}

void NodeCanvas::setItemTargetPos(QGraphicsItem* item, QPointF targetPos, bool animate) {
    if (!item) return;

    if (auto* nw = dynamic_cast<NodeWidget*>(item)) {
        if (nw->isDragging()) {
            return; // Do not animate or affect currently dragged item
        }
    }

    if (!animate) {
        item->setPos(targetPos);
        m_targetPositions.erase(item);
        return;
    }

    m_targetPositions[item] = targetPos;

    if (m_animationTimer && !m_animationTimer->isActive()) {
        m_animationTimer->start(16);
    }
}

void NodeCanvas::tickAnimations() {
    bool anyMoving = false;
    const qreal LERP_FACTOR = 0.25; // Speed factor of the slide transition

    std::vector<QGraphicsItem*> toRemove;

    for (auto& pair : m_targetPositions) {
        QGraphicsItem* item = pair.first;
        QPointF target = pair.second;

        // Skip currently dragged node widgets
        if (auto* nw = dynamic_cast<NodeWidget*>(item)) {
            if (nw->isDragging()) {
                toRemove.push_back(item);
                continue;
            }
        }

        QPointF current = item->pos();
        QPointF diff = target - current;
        qreal distSq = diff.x() * diff.x() + diff.y() * diff.y();

        if (distSq < 0.25) {
            item->setPos(target);
            toRemove.push_back(item);
        } else {
            item->setPos(current + diff * LERP_FACTOR);
            anyMoving = true;
        }
    }

    for (auto* item : toRemove) {
        m_targetPositions.erase(item);
    }

    if (!anyMoving && m_animationTimer) {
        m_animationTimer->stop();
    }
}

void NodeCanvas::addCol() {
    if (m_numCols < MAX_COLS) {
        m_numCols++;
        updateLayout();
    }
}

int NodeCanvas::getHighestOccupiedCol() const {
    int highestUsedCol = -1;
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < NUM_COLS; ++c) {
            if (m_rows[r].plugins[c]) {
                highestUsedCol = std::max(highestUsedCol, c);
            }
        }
        if (m_rows[r].hasSplitSection) {
            highestUsedCol = std::max(highestUsedCol, getSplitCol(r));
            highestUsedCol = std::max(highestUsedCol, getMergeCol(r));
        }
    }
    return highestUsedCol;
}

void NodeCanvas::setNumCols(int cols) {
    int minNeeded = getHighestOccupiedCol() + 1;
    m_numCols = std::clamp(cols, std::max(4, minNeeded), MAX_COLS);
    updateLayout();
}

void NodeCanvas::setZoomLevel(double zoom) {
    stopZoomAnimation();
    applyZoom(zoom, true);
}

void NodeCanvas::stopZoomAnimation() {
    if (m_zoomAnim && m_zoomAnim->state() == QAbstractAnimation::Running) {
        m_zoomAnim->stop();
        m_fitting = false;
        updateLayout();
    }
}

void NodeCanvas::applyZoom(double zoom, bool relayout) {
    double newZoom = std::clamp(zoom, 0.5, 2.0);
    if (qAbs(m_zoomLevel - newZoom) < 0.001) return;

    double scaleFactor = newZoom / m_zoomLevel;
    m_zoomLevel = newZoom;
    scale(scaleFactor, scaleFactor);
    if (relayout) updateLayout();

    if (m_zoomOverlayLabel) {
        m_zoomOverlayLabel->setText(QString("%1%").arg(qRound(m_zoomLevel * 100.0)));
    }
    if (m_zoomOverlayMinusBtn) {
        m_zoomOverlayMinusBtn->setEnabled(m_zoomLevel > 0.51);
    }
    if (m_zoomOverlayPlusBtn) {
        m_zoomOverlayPlusBtn->setEnabled(m_zoomLevel < 1.99);
    }

    emit zoomChanged(m_zoomLevel);
}

void NodeCanvas::setupZoomOverlay() {
    m_zoomOverlay = new QFrame(this);
    m_zoomOverlay->setObjectName("zoomOverlay");
    m_zoomOverlay->setStyleSheet(
        "QFrame#zoomOverlay { background-color: rgba(20, 22, 28, 225); border: 1px solid rgba(255, 255, 255, 30); border-radius: 8px; }"
    );

    QHBoxLayout* layout = new QHBoxLayout(m_zoomOverlay);
    m_zoomOverlayLayout = layout;
    // Grow/shrink with its contents ("6" -> "11", "73%" -> "100%") and stay
    // anchored to the corner (see eventFilter).
    layout->setSizeConstraint(QLayout::SetFixedSize);
    m_zoomOverlay->installEventFilter(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);

    QString btnStyle = 
        "QToolButton { background-color: #262830; color: #E0E0E0; font-size: 10px; border: 1px solid #363842; border-radius: 4px; padding: 2px; }"
        "QToolButton:hover { background-color: #363844; color: white; border-color: #00B0FF; }"
        "QToolButton:disabled { color: #555555; background-color: #1A1A1C; border-color: #252528; }";

    m_zoomOverlayMinusBtn = new QToolButton(m_zoomOverlay);
    m_zoomOverlayMinusBtn->setText("➖");
    m_zoomOverlayMinusBtn->setFixedSize(22, 22);
    m_zoomOverlayMinusBtn->setToolTip("Zoom Out (Ctrl + Wheel Down)");
    m_zoomOverlayMinusBtn->setCursor(Qt::PointingHandCursor);
    m_zoomOverlayMinusBtn->setStyleSheet(btnStyle);
    connect(m_zoomOverlayMinusBtn, &QToolButton::clicked, this, &NodeCanvas::zoomOut);
    layout->addWidget(m_zoomOverlayMinusBtn);

    m_zoomOverlayLabel = new QLabel("100%", m_zoomOverlay);
    m_zoomOverlayLabel->setAlignment(Qt::AlignCenter);
    m_zoomOverlayLabel->setMinimumWidth(m_zoomOverlayLabel->fontMetrics().horizontalAdvance("200%") + 10);
    m_zoomOverlayLabel->setStyleSheet("font-weight: bold; color: #00B0FF; font-size: 11px; padding: 0 4px;");
    layout->addWidget(m_zoomOverlayLabel);

    m_zoomOverlayPlusBtn = new QToolButton(m_zoomOverlay);
    m_zoomOverlayPlusBtn->setText("➕");
    m_zoomOverlayPlusBtn->setFixedSize(22, 22);
    m_zoomOverlayPlusBtn->setToolTip("Zoom In (Ctrl + Wheel Up)");
    m_zoomOverlayPlusBtn->setCursor(Qt::PointingHandCursor);
    m_zoomOverlayPlusBtn->setStyleSheet(btnStyle);
    connect(m_zoomOverlayPlusBtn, &QToolButton::clicked, this, &NodeCanvas::zoomIn);
    layout->addWidget(m_zoomOverlayPlusBtn);

    m_zoomOverlayResetBtn = new QToolButton(m_zoomOverlay);
    m_zoomOverlayResetBtn->setText("↺");
    m_zoomOverlayResetBtn->setFixedSize(22, 22);
    m_zoomOverlayResetBtn->setToolTip("Reset Zoom (Ctrl+0)");
    m_zoomOverlayResetBtn->setCursor(Qt::PointingHandCursor);
    m_zoomOverlayResetBtn->setStyleSheet(btnStyle);
    connect(m_zoomOverlayResetBtn, &QToolButton::clicked, this, &NodeCanvas::resetZoom);
    layout->addWidget(m_zoomOverlayResetBtn);

    m_zoomOverlayFitBtn = new QToolButton(m_zoomOverlay);
    m_zoomOverlayFitBtn->setText("Fit");
    m_zoomOverlayFitBtn->setFixedHeight(22);
    m_zoomOverlayFitBtn->setToolTip("Fit signal path");
    m_zoomOverlayFitBtn->setCursor(Qt::PointingHandCursor);
    m_zoomOverlayFitBtn->setStyleSheet(btnStyle);
    connect(m_zoomOverlayFitBtn, &QToolButton::clicked, this, &NodeCanvas::fitToCanvas);
    layout->addWidget(m_zoomOverlayFitBtn);

    m_zoomOverlayAutoBtn = new QToolButton(m_zoomOverlay);
    m_zoomOverlayAutoBtn->setText("Auto");
    m_zoomOverlayAutoBtn->setCheckable(true);
    m_zoomOverlayAutoBtn->setFixedHeight(22);
    m_zoomOverlayAutoBtn->setToolTip("Auto-fit: keep the whole signal path in view when the window is resized or the board changes.\nZooming by hand turns it off.");
    m_zoomOverlayAutoBtn->setCursor(Qt::PointingHandCursor);
    m_zoomOverlayAutoBtn->setStyleSheet(btnStyle +
        "QToolButton:checked { background-color: #00598A; color: white; border-color: #00B0FF; }");
    connect(m_zoomOverlayAutoBtn, &QToolButton::toggled, this, &NodeCanvas::setAutoFit);
    layout->addWidget(m_zoomOverlayAutoBtn);

    m_autoFitTimer = new QTimer(this);
    m_autoFitTimer->setSingleShot(true);
    // Wait for node move animations to settle before measuring the content.
    m_autoFitTimer->setInterval(350);
    connect(m_autoFitTimer, &QTimer::timeout, this, [this]() {
        if (!m_autoFit || m_fitting) return;
        fitToCanvas();
    });

    m_zoomOverlay->adjustSize();
    updateZoomOverlayPos();
}

void NodeCanvas::updateZoomOverlayPos() {
    if (!m_zoomOverlay) return;
    m_zoomOverlay->adjustSize();
    int x = viewport()->width() - m_zoomOverlay->width() - 16;
    int y = viewport()->height() - m_zoomOverlay->height() - 16;
    m_zoomOverlay->move(x, y);
    m_zoomOverlay->raise();
}

void NodeCanvas::addZoomOverlayWidget(QWidget* widget) {
    if (!m_zoomOverlayLayout || !widget) return;
    auto* divider = new QFrame(m_zoomOverlay);
    divider->setFrameShape(QFrame::VLine);
    divider->setStyleSheet("color: rgba(255, 255, 255, 40);");
    m_zoomOverlayLayout->addWidget(divider);
    widget->setParent(m_zoomOverlay);
    m_zoomOverlayLayout->addWidget(widget);
    updateZoomOverlayPos();
}

QLabel* NodeCanvas::infoOverlay() {
    if (!m_infoOverlay) {
        m_infoOverlay = new QLabel(this);
        m_infoOverlay->setObjectName("canvasInfo");
        m_infoOverlay->setTextFormat(Qt::RichText);
        m_infoOverlay->setStyleSheet(
            "QLabel#canvasInfo { background-color: rgba(20, 22, 28, 170); border: 1px solid rgba(255, 255, 255, 18);"
            " border-radius: 8px; padding: 6px 12px; }");
        m_infoOverlay->move(16, 12);
    }
    return m_infoOverlay;
}

void NodeCanvas::setInfoOverlayText(const QString& html) {
    QLabel* label = infoOverlay();
    label->setText(html);
    label->setVisible(!html.isEmpty());
    label->adjustSize();
    label->raise();
}

void NodeCanvas::resetZoom() {
    setAutoFit(false);
    setZoomLevel(1.0);
}

void NodeCanvas::setAutoFit(bool enabled) {
    if (m_autoFit == enabled) return;
    m_autoFit = enabled;
    if (m_zoomOverlayAutoBtn && m_zoomOverlayAutoBtn->isChecked() != enabled) {
        const QSignalBlocker blocker(m_zoomOverlayAutoBtn);
        m_zoomOverlayAutoBtn->setChecked(enabled);
    }
    if (enabled) scheduleAutoFit();
    emit autoFitChanged(enabled);
}

void NodeCanvas::scheduleAutoFit() {
    if (m_autoFit && !m_fitting && m_autoFitTimer) m_autoFitTimer->start();
}

void NodeCanvas::fitToCanvas() {
    QRectF content;
    for (QGraphicsItem* item : m_scene->items()) {
        if (!item->isVisible() || item == m_dragPlaceholderItem) continue;
        content = content.isNull() ? item->sceneBoundingRect() : content.united(item->sceneBoundingRect());
    }
    if (content.isEmpty() || viewport()->width() <= 0 || viewport()->height() <= 0) return;
    const qreal padding = 48.0;
    const qreal fit = std::min((viewport()->width() - padding) / content.width(),
                               (viewport()->height() - padding) / content.height());
    const double targetZoom = std::clamp<double>(fit, 0.5, 1.5);
    const QPointF targetCenter = content.center();

    // Glide to the new framing instead of jumping. The (costly) relayout only
    // runs once at the end; intermediate frames just scale and scroll.
    stopZoomAnimation();
    const double startZoom = m_zoomLevel;
    const QPointF startCenter = mapToScene(viewport()->rect().center());
    if (qAbs(startZoom - targetZoom) < 0.002 && QLineF(startCenter, targetCenter).length() < 2.0) {
        centerOn(targetCenter);
        return;
    }
    if (!m_zoomAnim) {
        m_zoomAnim = new QVariantAnimation(this);
        m_zoomAnim->setDuration(260);
        m_zoomAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_zoomAnim->setStartValue(0.0);
        m_zoomAnim->setEndValue(1.0);
    }
    m_zoomAnim->disconnect(this);
    connect(m_zoomAnim, &QVariantAnimation::valueChanged, this,
            [this, startZoom, targetZoom, startCenter, targetCenter](const QVariant& value) {
        const double t = value.toDouble();
        applyZoom(startZoom + (targetZoom - startZoom) * t, false);
        centerOn(startCenter + (targetCenter - startCenter) * t);
    });
    connect(m_zoomAnim, &QVariantAnimation::finished, this, [this, targetZoom, targetCenter]() {
        applyZoom(targetZoom, false);
        updateLayout();
        centerOn(targetCenter);
        m_fitting = false;
    });
    m_fitting = true; // the final relayout must not schedule another fit
    m_zoomAnim->start();
}

void NodeCanvas::zoomIn() {
    setAutoFit(false);
    setZoomLevel(m_zoomLevel + 0.10);
}

void NodeCanvas::zoomOut() {
    setAutoFit(false);
    setZoomLevel(m_zoomLevel - 0.10);
}

void NodeCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta > 0) {
            zoomIn();
        } else if (delta < 0) {
            zoomOut();
        }
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void NodeCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton ||
       (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier))) {
        m_isPanning = true;
        m_panStartPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void NodeCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_isPanning) {
        QPoint delta = event->pos() - m_panStartPos;
        m_panStartPos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void NodeCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isPanning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_isPanning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void NodeCanvas::setSceneMarkedNodes(std::unordered_set<std::string> ids) {
    if (ids == m_sceneMarkedNodes) return;
    m_sceneMarkedNodes = std::move(ids);
    viewport()->update();
}

void NodeCanvas::setMidiMarkedNodes(std::unordered_set<std::string> ids) {
    if (ids == m_midiMarkedNodes) return;
    m_midiMarkedNodes = std::move(ids);
    viewport()->update();
}

// ─── Plugin drops from the browser ───────────────────────────────────────────

namespace {
constexpr const char* kPluginMime = "application/x-rigroom-plugin-uri";
}

PlusButtonWidget* NodeCanvas::nearestDropTarget(const QPointF& scenePos) const {
    PlusButtonWidget* best = nullptr;
    qreal bestDist = 1e9;
    for (QGraphicsItem* item : m_scene->items()) {
        auto* pb = dynamic_cast<PlusButtonWidget*>(item);
        if (!pb) continue;
        const QPointF d = scenePos - pb->homePos();
        const qreal dist = std::hypot(d.x(), d.y());
        if (std::abs(d.y()) < 80.0 && dist < 96.0 && dist < bestDist) {
            bestDist = dist;
            best = pb;
        }
    }
    return best;
}

void NodeCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(kPluginMime)) event->acceptProposedAction();
    else QGraphicsView::dragEnterEvent(event);
}

void NodeCanvas::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasFormat(kPluginMime)) {
        QGraphicsView::dragMoveEvent(event);
        return;
    }
    if (PlusButtonWidget* target = nearestDropTarget(mapToScene(event->position().toPoint()))) {
        setDragGap(target->getRow(), target->getCol(), target->insertMode());
        event->acceptProposedAction();
    } else {
        clearDragGap();
        event->ignore();
    }
}

void NodeCanvas::dragLeaveEvent(QDragLeaveEvent* event) {
    clearDragGap();
    QGraphicsView::dragLeaveEvent(event);
}

void NodeCanvas::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(kPluginMime)) {
        QGraphicsView::dropEvent(event);
        return;
    }
    const QString uri = QString::fromUtf8(event->mimeData()->data(kPluginMime));
    PlusButtonWidget* target = nearestDropTarget(mapToScene(event->position().toPoint()));
    const int row = target ? target->getRow() : -1;
    const int col = target ? target->getCol() : -1;
    const int insert = target ? target->insertMode() : 0;
    clearDragGap();
    if (row < 0 || uri.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    // Creating the plugin rebuilds the scene; do it after the drop returns.
    QTimer::singleShot(0, this, [this, uri, row, col, insert]() { emit pluginDropped(uri, row, col, insert); });
}
