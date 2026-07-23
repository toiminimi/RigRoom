#include "NodeCanvas.h"
#include "GridRow.h"
#include "PlusButtonWidget.h"
#include "RoutingHandleItem.h"
#include <QPainter>
#include <QTimer>
#include <QKeyEvent>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <functional>

// ─── Dimensions ───────────────────────────────────────────────────────────────
static constexpr qreal SYS_NODE_W  = 136.0;
static constexpr qreal SYS_NODE_H  = 58.0;
static constexpr qreal PLUG_NODE_W = 136.0;
static constexpr qreal PLUG_NODE_H = 58.0;
static constexpr qreal MARGIN_X    = 14.0;
static constexpr qreal MARGIN_Y    = 30.0;
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
        setToolTip("Drag onto a connection point in any active lane to create a Split Section.");
    }

    bool isLimitReached() const {
        if (!m_canvas) return false;
        return m_canvas->hasSplitSection(1) && 
               m_canvas->hasSplitSection(3) && 
               m_canvas->hasSplitSection(0) && 
               m_canvas->hasSplitSection(4);
    }

    QRectF boundingRect() const override { return QRectF(0, 0, 132, 38); }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        const bool disabled = isLimitReached();
        if (disabled) {
            painter->setOpacity(0.35);
        } else {
            painter->setOpacity(1.0);
        }
        const QColor accent = m_dragging ? QColor(255, 200, 50)
            : (m_hovered ? QColor(83, 216, 255) : QColor(53, 199, 255));
        painter->setPen(QPen(accent, m_hovered || m_dragging ? 2.0 : 1.2));
        painter->setBrush(QColor(25, 29, 36, 235));
        painter->drawRoundedRect(boundingRect().adjusted(1, 1, -1, -1), 7, 7);

        painter->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(13, 19), QPointF(25, 19));
        painter->drawLine(QPointF(25, 19), QPointF(34, 11));
        painter->drawLine(QPointF(25, 19), QPointF(34, 27));
        painter->setBrush(accent);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(25, 19), 3, 3);

        QFont font = painter->font();
        font.setPixelSize(10);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(235, 240, 246));
        painter->drawText(QRectF(43, 0, 82, 38), Qt::AlignVCenter | Qt::AlignLeft, "DRAG SPLIT");
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
        PlusButtonWidget* target = nearestTarget(event->scenePos());
        setTarget(target);
        if (target) setPos(target->scenePos().x() - boundingRect().width() / 2.0,
                           event->scenePos().y() - boundingRect().height() / 2.0);
        else setPos(event->scenePos() - QPointF(boundingRect().width() / 2.0, boundingRect().height() / 2.0));
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (!m_dragging) return;
        m_dragging = false;
        PlusButtonWidget* target = m_target ? m_target : nearestTarget(event->scenePos());
        const int gap = target ? target->getCol() : -1;
        const int parentRow = target ? target->getRow() : -1;
        setTarget(nullptr);
        setCursor(Qt::OpenHandCursor);
        update();
        if (parentRow >= 0 && gap >= 0) {
            NodeCanvas* canvas = m_canvas;
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
            setToolTip("Drag onto a connection point in any active lane to create a Split Section.");
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
    PlusButtonWidget* nearestTarget(const QPointF& scenePoint) const {
        PlusButtonWidget* nearest = nullptr;
        qreal distance = 100.0;
        for (QGraphicsItem* item : scene()->items()) {
            auto* plus = dynamic_cast<PlusButtonWidget*>(item);
            if (!plus) continue;
            const int row = plus->getRow();
            if (row == NodeCanvas::MAIN_ROW) {
                if (m_canvas->hasSplitSection(1) && m_canvas->hasSplitSection(3)) continue;
            } else if (row == 1) {
                if (!m_canvas->hasSplitSection(1) || m_canvas->hasSplitSection(0)) continue;
            } else if (row == 3) {
                if (!m_canvas->hasSplitSection(3) || m_canvas->hasSplitSection(4)) continue;
            } else {
                continue;
            }
            const QPointF delta = plus->scenePos() - scenePoint;
            const qreal candidate = std::hypot(delta.x(), delta.y());
            if (candidate < distance) {
                distance = candidate;
                nearest = plus;
            }
        }
        return nearest;
    }

    void setTarget(PlusButtonWidget* target) {
        if (m_target == target) return;
        if (m_target) m_target->setRoutingTarget(false);
        m_target = target;
        if (m_target) m_target->setRoutingTarget(true);
    }

    NodeCanvas* m_canvas;
    PlusButtonWidget* m_target = nullptr;
    bool m_hovered = false;
    bool m_dragging = false;
};

class BranchLabelItem final : public QGraphicsItem {
public:
    BranchLabelItem(NodeCanvas* canvas, int row, bool enabled)
        : m_canvas(canvas), m_row(row), m_enabled(enabled) {
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
        setZValue(8);
    }

    QRectF boundingRect() const override { return QRectF(0, 0, 112, 26); }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(m_enabled ? QColor(65, 74, 86) : QColor(55, 58, 66), 1));
        painter->setBrush(m_hovered ? QColor(38, 43, 51) : QColor(25, 27, 32));
        painter->drawRoundedRect(boundingRect(), 6, 6);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_enabled ? QColor(53, 199, 255) : QColor(80, 84, 94));
        painter->drawEllipse(QRectF(10, 9, 8, 8));
        painter->setPen(m_enabled ? QColor(225, 229, 236) : QColor(130, 135, 145));
        painter->drawText(QRectF(24, 0, 82, 26), Qt::AlignVCenter | Qt::AlignLeft,
                          m_canvas->getBranchName(m_row));
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        m_canvas->selectBranch(m_row);
        event->accept();
    }
    void hoverEnterEvent(QGraphicsSceneHoverEvent*) override { m_hovered = true; update(); }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override { m_hovered = false; update(); }

private:
    NodeCanvas* m_canvas;
    int m_row;
    bool m_enabled;
    bool m_hovered = false;
};

// ─── Constructor ──────────────────────────────────────────────────────────────
NodeCanvas::NodeCanvas(AudioEngine* engine, QWidget* parent)
    : QGraphicsView(parent), m_engine(engine)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(QColor(14, 14, 16));
    setDragMode(QGraphicsView::NoDrag);
    setFocusPolicy(Qt::StrongFocus);

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
    if (row == 0 || row == 2) {
        m_rows[row].hasSplitSection = true;
        if (!m_rows[row].levelConfigured) setBranchDefaults(row, 0.25118864f);
        m_rows[row].enabled = true;
        updateBranchGains(row);
    }
    m_engine->addNode(node);
    applyRoutingChange();
}

// Insert before the current occupant at 'col' — shift everything right by one
void NodeCanvas::insertPluginBefore(int row, int col, std::shared_ptr<AudioNode> node, bool isSecondOfCol) {
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

void NodeCanvas::removePluginAt(int row, int col) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    if (m_rows[row].plugins[col]) {
        emit nodeAboutToBeRemoved(m_rows[row].plugins[col].get());
        const std::string removedId = m_rows[row].plugins[col]->uniqueId;
        m_engine->removeNode(removedId);
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
        if (row == MAIN_ROW) {
            for (int branchRow : {0, 1, 3, 4}) {
                if (m_rows[branchRow].splitAfterNodeId == removedId) {
                    m_rows[branchRow].splitAfterNodeId = newNode ? newNode->uniqueId : std::string{};
                }
                if (m_rows[branchRow].mergeBeforeNodeId == removedId) {
                    m_rows[branchRow].mergeBeforeNodeId = newNode ? newNode->uniqueId : std::string{};
                }
            }
        }
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
    return createSplitAtPathGap(1, gapIndex);
}

bool NodeCanvas::createSplitAtPathGap(int parentRow, int gapIndex) {
    if (parentRow < 0 || parentRow >= NUM_ROWS) return false;

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

    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[row].plugins[c]) {
            m_engine->removeNode(m_rows[row].plugins[c]->uniqueId);
            m_rows[row].plugins[c] = nullptr;
        }
    }

    for (int r : {0, 1, 3, 4}) {
        if (r != row && m_rows[r].hasSplitSection && m_rows[r].parentRow == row) {
            removeSplitSection(r);
        }
    }

    m_rows[row].hasSplitSection = false;
    m_rows[row].enabled = false;
    m_rows[row].splitAfterNodeId.clear();
    m_rows[row].mergeBeforeNodeId.clear();
    applyRoutingChange();
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
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS || parentRow < 0 || parentRow >= NUM_ROWS || parentRow == row) return;
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
    m_rows[row].mainInputEnabled = enabled;
    applyRoutingChange();
    updateLayout();
}

float NodeCanvas::getMainMix(int row) const {
    return (row != MAIN_ROW && row >= 0 && row < NUM_ROWS) ? m_rows[row].mainMix : 1.0f;
}

void NodeCanvas::setMainMix(int row, float level) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const int parentRow = m_rows[row].parentRow;
    const float clamped = std::clamp(level, 0.0f, 2.0f);
    for (int r = 0; r < NUM_ROWS; ++r) {
        if (r != MAIN_ROW && m_rows[r].parentRow == parentRow) {
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

void NodeCanvas::onPlusButtonClicked(int row, int col, QPoint screenPos, bool isSecondOfCol) {
    emit plusButtonClicked(row, col, screenPos, isSecondOfCol);
}

// ─── Layout Engine ────────────────────────────────────────────────────────────
void NodeCanvas::calculateRowCenters(qreal rowCenters[NUM_ROWS], qreal H) const {
    // Spacing between active lanes (spacious: 175px leaves generous vertical clearance between pedal cards and routing handle pills)
    constexpr qreal LANE_HEIGHT = 175.0;

    // Calculate Y offsets relative to Main Row (Row 2, offset = 0)
    qreal offset[NUM_ROWS] = {0.0};
    
    // Row 2 is MAIN_ROW
    offset[2] = 0.0;
    
    // Row 1 (B1) and Row 0 (B2) are top branches
    if (m_rows[1].hasSplitSection) {
        offset[1] = -LANE_HEIGHT;
        if (m_rows[0].hasSplitSection) {
            offset[0] = -2 * LANE_HEIGHT;
        } else {
            offset[0] = -LANE_HEIGHT;
        }
    } else {
        offset[1] = 0.0;
        offset[0] = 0.0;
    }
    
    // Row 3 (C1) and Row 4 (C2) are bottom branches
    if (m_rows[3].hasSplitSection) {
        offset[3] = LANE_HEIGHT;
        if (m_rows[4].hasSplitSection) {
            offset[4] = 2 * LANE_HEIGHT;
        } else {
            offset[4] = LANE_HEIGHT;
        }
    } else {
        offset[3] = 0.0;
        offset[4] = 0.0;
    }
    
    // Find min and max active offsets
    qreal minY = 0.0;
    qreal maxY = 0.0;
    for (int r = 0; r < NUM_ROWS; ++r) {
        if (r == MAIN_ROW || m_rows[r].hasSplitSection) {
            minY = std::min(minY, offset[r]);
            maxY = std::max(maxY, offset[r]);
        }
    }
    
    // Center the whole group around H / 2
    qreal midOffset = (minY + maxY) / 2.0;
    qreal mainY = snapToGrid(H / 2.0 - midOffset);

    for (int r = 0; r < NUM_ROWS; ++r) {
        rowCenters[r] = mainY + offset[r];
    }
}

qreal NodeCanvas::getRowCenterY(int row) const {
    if (row < 0 || row >= NUM_ROWS) return 0.0;
    qreal rowCenters[NUM_ROWS];
    qreal H = std::max(200.0, (qreal)viewport()->height());
    calculateRowCenters(rowCenters, H);
    return rowCenters[row];
}

void NodeCanvas::updateLayout() {
    clearSceneItems();

    qreal W = std::max(400.0, (qreal)viewport()->width());
    int longestChain = 0;
    for (const auto& row : m_rows) longestChain = std::max(longestChain, row.count());
    qreal minimumChainW = longestChain * PLUG_NODE_W + (longestChain + 1) * (INSERT_BTN_W + 2 * MIN_SPACING);
    W = std::max(W, 2 * MARGIN_X + 2 * SYS_NODE_W + 32.0 + minimumChainW);
    
    qreal H = std::max(200.0, (qreal)viewport()->height());
    constexpr qreal LANE_HEIGHT = 140.0;
    qreal offset[NUM_ROWS] = {0.0};
    if (m_rows[1].hasSplitSection) {
        offset[1] = -LANE_HEIGHT;
        offset[0] = m_rows[0].hasSplitSection ? -2 * LANE_HEIGHT : -LANE_HEIGHT;
    }
    if (m_rows[3].hasSplitSection) {
        offset[3] = LANE_HEIGHT;
        offset[4] = m_rows[4].hasSplitSection ? 2 * LANE_HEIGHT : LANE_HEIGHT;
    }
    qreal minY = 0.0;
    qreal maxY = 0.0;
    for (int r = 0; r < NUM_ROWS; ++r) {
        if (r == MAIN_ROW || m_rows[r].hasSplitSection) {
            minY = std::min(minY, offset[r]);
            maxY = std::max(maxY, offset[r]);
        }
    }
    qreal activeSpan = maxY - minY;
    H = std::max(H, activeSpan + 2 * MARGIN_Y + 40.0);

    m_scene->setSceneRect(0, 0, W, H);

    qreal rowCenters[NUM_ROWS];
    calculateRowCenters(rowCenters, H);

    qreal sysCY = rowCenters[MAIN_ROW];

    // Place system nodes using actual dimensions
    qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : 160.0;
    qreal inputH = m_sysInputWidget ? m_sysInputWidget->height() : 70.0;
    qreal outputW = m_sysOutputWidget ? m_sysOutputWidget->width() : 160.0;
    qreal outputH = m_sysOutputWidget ? m_sysOutputWidget->height() : 70.0;

    if (m_sysInputWidget) {
        m_sysInputWidget->setPos(MARGIN_X, sysCY - inputH / 2);
    }
    if (m_sysOutputWidget) {
        m_sysOutputWidget->setPos(W - MARGIN_X - outputW, sysCY - outputH / 2);
    }

    qreal trackLeft  = MARGIN_X + inputW + 16.0;
    qreal trackRight = W - MARGIN_X - outputW - 16.0;
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
    splitTool->setPos(trackLeft, 12.0);
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

    m_scene->setSceneRect(QRectF(0, 0, W, H).united(m_scene->itemsBoundingRect()));
}

qreal NodeCanvas::getGapX(int gapIdx) const {
    qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : 160.0;
    qreal trackLeft = MARGIN_X + inputW + 16.0;

    auto getColX = [&](int col) -> qreal {
        return trackLeft + col * 168.0 + 28.0;
    };

    if (gapIdx <= 0) return trackLeft + 4.0;
    if (gapIdx >= NUM_COLS) return getColX(NUM_COLS - 1) + PLUG_NODE_W + 24.0;
    return getColX(gapIdx) - 24.0;
}

void NodeCanvas::layoutRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW, qreal sysRightX, qreal sysLeftX, qreal sysMidY) {
    const GridRow& row = m_rows[r];
    if (r != MAIN_ROW && !row.hasSplitSection) return;

    auto getColX = [&](int col) -> qreal {
        return trackLeft + col * 168.0 + 28.0;
    };

    qreal startX = sysRightX;
    qreal endX = sysLeftX;
    qreal parentSplitX = trackLeft;
    qreal parentMergeX = trackRight;
    int minSlot = 0;
    int maxSlot = NUM_COLS - 1;

    if (r != MAIN_ROW) {
        int parentRow = row.parentRow;
        int splitCol = getSplitCol(r);
        parentSplitX = (splitCol < 0) ? getGapX(0) : getGapX(splitCol + 1);
        if (parentRow != MAIN_ROW && parentRow >= 0 && parentRow < NUM_ROWS) {
            parentSplitX = std::max(parentSplitX, m_rows[parentRow].parentSplitX);
        }

        int mergeCol = getMergeCol(r);
        parentMergeX = (mergeCol < 0) ? getGapX(NUM_COLS) : getGapX(mergeCol);
        if (parentRow != MAIN_ROW && parentRow >= 0 && parentRow < NUM_ROWS) {
            parentMergeX = std::min(parentMergeX, m_rows[parentRow].parentMergeX);
        }

        minSlot = 0;
        while (minSlot < NUM_COLS && getColX(minSlot) < parentSplitX - 10.0) {
            minSlot++;
        }

        maxSlot = NUM_COLS - 1;
        while (maxSlot >= 0 && getColX(maxSlot) + PLUG_NODE_W > parentMergeX + 10.0) {
            maxSlot--;
        }

        startX = parentSplitX;
        endX = parentMergeX;
    }

    if (r != MAIN_ROW) {
        auto* branchLabel = new BranchLabelItem(this, r, row.enabled);
        branchLabel->setPos(snapToGrid(trackLeft) + 6.0, cy - 36.0);
        m_scene->addItem(branchLabel);
        m_dynamicItems.push_back(branchLabel);
    }

    // Clear stale node widgets not in active range
    for (int c = 0; c < NUM_COLS; ++c) {
        bool isActiveSlot = (r == MAIN_ROW) || (c >= minSlot && c <= maxSlot);
        if ((!row.plugins[c] || !isActiveSlot) && m_nodeWidgets[r][c]) {
            m_scene->removeItem(m_nodeWidgets[r][c]);
            delete m_nodeWidgets[r][c];
            m_nodeWidgets[r][c] = nullptr;
        }
    }

    // Place node widgets at fixed column positions
    for (int c = 0; c < NUM_COLS; ++c) {
        bool isActiveSlot = (r == MAIN_ROW) || (c >= minSlot && c <= maxSlot);
        if (isActiveSlot && row.plugins[c]) {
            if (!m_nodeWidgets[r][c]) {
                m_nodeWidgets[r][c] = new NodeWidget(row.plugins[c]);
                m_scene->addItem(m_nodeWidgets[r][c]);
            }
            qreal nodeH = m_nodeWidgets[r][c]->height();
            m_nodeWidgets[r][c]->setPos(snapToGrid(getColX(c)), cy - nodeH / 2.0);
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

        float splitGainFactor = 1.0f;
        if (getSplitMode(rowIdx) == GridRow::SplitMode::AB) {
            float pos = getSplitPosition(rowIdx); // -1.0 .. +1.0
            if (rowIdx == 1 || rowIdx == 0) {
                splitGainFactor = std::clamp((1.0f - pos) * 0.5f, 0.0f, 1.0f);
            } else {
                splitGainFactor = std::clamp((1.0f + pos) * 0.5f, 0.0f, 1.0f);
            }
        }

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
    }

    // Draw branch connectors for side rows
    if (r != MAIN_ROW && row.hasSplitSection) {
        // Split connector
        auto* branch = new QGraphicsPathItem();
        QPainterPath bpath;
        bpath.moveTo(startX, cy);
        bpath.cubicTo(startX, (cy + sysMidY)/2.0, parentSplitX, (cy + sysMidY)/2.0, parentSplitX, sysMidY);
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
        bpathR.cubicTo(endX, (cy + sysMidY)/2.0, parentMergeX, (cy + sysMidY)/2.0, parentMergeX, sysMidY);
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
    }

    // Place plus buttons centered in unoccupied slot spaces
    for (int c = 0; c < NUM_COLS; ++c) {
        bool isActiveSlot = (r == MAIN_ROW) || (c >= minSlot && c <= maxSlot);
        bool isOccupied = (row.plugins[c] != nullptr);
        if (isActiveSlot && !isOccupied) {
            auto* plus = new PlusButtonWidget(r, c, PlusButtonWidget::Style::Ghost);
            plus->setPos(snapToGrid(getColX(c) + PLUG_NODE_W / 2.0 - 12.0), cy);
            m_scene->addItem(plus);
            m_dynamicItems.push_back(plus);
        }
    }
}

void NodeCanvas::rebuildAudioConnections() {
    m_engine->suspendProcessing();
    // Routing edits only replace connections. Re-adding every processor would
    // call prepare(), destroying live LV2 instances while JACK may be using them.
    m_engine->clearConnections();
    
    // Connect dry bypass for the primary middle row (r == 1) if it contains no active blocks
    bool middleRowEmpty = true;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[MAIN_ROW].plugins[c]) {
            middleRowEmpty = false;
            break;
        }
    }
    if (middleRowEmpty && m_mainOutputEnabled) {
        const float gain = pathSplitGainAfter(MAIN_ROW, {}) * pathMixerGainBefore(MAIN_ROW, {});
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
    std::vector<std::shared_ptr<AudioNode>> mainChain;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[MAIN_ROW].plugins[c]) mainChain.push_back(m_rows[MAIN_ROW].plugins[c]);
    }

    if (!mainChain.empty()) {
        const float inputGain = pathSplitGainAfter(MAIN_ROW, {}) * pathMixerGainBefore(MAIN_ROW, mainChain[0]->uniqueId);
        connect("system_input", 0, mainChain[0]->uniqueId, 0, inputGain);
        if (mainChain[0]->getAudioInputCount() > 1)
            connect("system_input", 1, mainChain[0]->uniqueId, 1, inputGain);

        for (size_t i = 0; i + 1 < mainChain.size(); ++i) {
            const float gain = pathSplitGainAfter(MAIN_ROW, mainChain[i]->uniqueId) *
                               pathMixerGainBefore(MAIN_ROW, mainChain[i + 1]->uniqueId);
            connectNodes(mainChain[i], mainChain[i+1], gain);
        }

        if (m_mainOutputEnabled) {
            auto& last = mainChain.back();
            int outs = last->getAudioOutputCount();
            const float outputGain = pathSplitGainAfter(MAIN_ROW, last->uniqueId) * pathMixerGainBefore(MAIN_ROW, {});
            connect(last->uniqueId, 0, "system_output", 0, outputGain);
            if (outs >= 2)
                connect(last->uniqueId, 1, "system_output", 1, outputGain);
            else
                connect(last->uniqueId, 0, "system_output", 1, outputGain);
        }
    }

    struct RouteEndpoint {
        std::string id;
        int channels;
    };
    std::function<RouteEndpoint(int)> pathInput;
    std::function<RouteEndpoint(int)> pathOutput;
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
    auto connectBalanced = [&](const RouteEndpoint& src, const RouteEndpoint& dst,
                               const BranchGainControls& gains, float fixedGain = 1.0f) {
        if (src.channels >= 2 && dst.channels >= 2) {
            connect(src.id, 0, dst.id, 0, fixedGain, gains.left);
            connect(src.id, 1, dst.id, 1, fixedGain, gains.right);
        } else if (src.channels >= 2 && dst.channels == 1) {
            connect(src.id, 0, dst.id, 0, fixedGain, gains.monoHalf);
            connect(src.id, 1, dst.id, 0, fixedGain, gains.monoHalf);
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
        std::vector<std::shared_ptr<AudioNode>> chain;
        for (int c = 0; c < NUM_COLS; ++c) {
            if (row.plugins[c]) chain.push_back(row.plugins[c]);
        }

        const int splitCol = getSplitCol(r);
        const int mergeCol = getMergeCol(r);

        if (splitCol >= 0 && mergeCol >= 0 && splitCol >= mergeCol) continue;

        const RouteEndpoint source = pathInput(r);
        const RouteEndpoint destination = pathOutput(r);

        if (chain.empty()) {
            const float pathGain = branchSplitGain(r) * pathSplitGainAfter(row.parentRow, source.id, r) * pathMixerGainBefore(r, {});
            connectBalanced(source, destination, m_branchGains[r], pathGain);
        }

        if (chain.empty()) continue;

        // Connect chain internal links
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const float gain = pathSplitGainAfter(row.parentRow, chain[i]->uniqueId, r) *
                               pathMixerGainBefore(r, chain[i + 1]->uniqueId);
            connectNodes(chain[i], chain[i+1], gain);
        }

        // Connect first node input (Split point)
        const float inputGain = branchSplitGain(r) * pathSplitGainAfter(row.parentRow, source.id, r) *
                                pathMixerGainBefore(r, chain[0]->uniqueId);
        connectFixed(source, chain[0], inputGain);

        // Connect last node output (Merge point) with return mix gain
        const auto& last = chain.back();
        const float outputGain = pathSplitGainAfter(row.parentRow, last->uniqueId, r) * pathMixerGainBefore(r, {});
        connectBalanced({last->uniqueId, last->getAudioOutputCount()}, destination, m_branchGains[r], outputGain);
    }

    m_engine->rebuildGraph();
    m_engine->resumeProcessing();
}

// ─── Scene item lifecycle ─────────────────────────────────────────────────────
void NodeCanvas::clearSceneItems() {
    m_targetPositions.clear();
    if (m_animationTimer) m_animationTimer->stop();

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
void NodeCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    updateLayout();
}

void NodeCanvas::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    
    painter->setPen(QPen(QColor(35, 35, 42), 1));
    qreal gs = CANVAS_GRID;
    qreal l = rect.left() - std::fmod(rect.left(), gs);
    qreal t = rect.top()  - std::fmod(rect.top(),  gs);
    for (qreal x = l; x < rect.right();  x += gs)
        for (qreal y = t; y < rect.bottom(); y += gs)
            painter->drawPoint(QPointF(x, y));
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
    int parentRow = m_rows[row].parentRow;
    if (parentRow >= 0 && parentRow < NUM_ROWS && col >= 0 && col < NUM_COLS && m_rows[parentRow].plugins[col]) {
        m_rows[row].splitAfterNodeId = m_rows[parentRow].plugins[col]->uniqueId;
    } else {
        m_rows[row].splitAfterNodeId.clear();
    }
    applyRoutingChange();
}

void NodeCanvas::setMergeCol(int row, int col) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    m_rows[row].mergeCol = col;
    int parentRow = m_rows[row].parentRow;
    if (parentRow >= 0 && parentRow < NUM_ROWS && col >= 0 && col < NUM_COLS && m_rows[parentRow].plugins[col]) {
        m_rows[row].mergeBeforeNodeId = m_rows[parentRow].plugins[col]->uniqueId;
    } else {
        m_rows[row].mergeBeforeNodeId.clear();
    }
    applyRoutingChange();
}

void NodeCanvas::setSplitAnchor(int row, const std::string& nodeId) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const int parentRow = m_rows[row].parentRow;
    if (!nodeId.empty() && findNodeColumn(parentRow, nodeId) < 0) return;
    const int newSplitCol = findNodeColumn(parentRow, nodeId);
    const int mergeCol = findNodeColumn(parentRow, m_rows[row].mergeBeforeNodeId);
    
    if (newSplitCol >= 0 && mergeCol >= 0 && mergeCol < newSplitCol + 2) {
        updateLayout();
        return;
    }
    if (newSplitCol < 0 && mergeCol == 0) {
        updateLayout();
        return;
    }
    m_rows[row].splitAfterNodeId = nodeId;
    applyRoutingChange();
}

void NodeCanvas::setMergeAnchor(int row, const std::string& nodeId) {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return;
    const int parentRow = m_rows[row].parentRow;
    if (!nodeId.empty() && findNodeColumn(parentRow, nodeId) < 0) return;
    const int splitCol = findNodeColumn(parentRow, m_rows[row].splitAfterNodeId);
    const int newMergeCol = findNodeColumn(parentRow, nodeId);
    
    if (splitCol >= 0 && newMergeCol >= 0 && newMergeCol < splitCol + 2) {
        updateLayout();
        return;
    }
    if (splitCol < 0 && newMergeCol == 0) {
        updateLayout();
        return;
    }
    m_rows[row].mergeBeforeNodeId = nodeId;
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
    const float level = std::clamp(m_rows[row].mix, 0.0f, 1.0f);
    const float pan = std::clamp(m_rows[row].pan, -1.0f, 1.0f);
    const float polarity = m_rows[row].polarityInverted ? -1.0f : 1.0f;
    m_branchGains[row].left->store(polarity * level * (pan > 0.0f ? 1.0f - pan : 1.0f), std::memory_order_relaxed);
    m_branchGains[row].right->store(polarity * level * (pan < 0.0f ? 1.0f + pan : 1.0f), std::memory_order_relaxed);
    m_branchGains[row].mono->store(polarity * level, std::memory_order_relaxed);
    m_branchGains[row].monoHalf->store(polarity * level * 0.5f, std::memory_order_relaxed);
}

float NodeCanvas::branchSplitGain(int row) const {
    if (row == MAIN_ROW || row < 0 || row >= NUM_ROWS) return 1.0f;
    if (m_rows[row].splitMode == GridRow::SplitMode::Copy) return 1.0f;
    constexpr float halfPi = 1.57079632679f;
    const float p = (std::clamp(m_rows[row].splitPosition, -1.0f, 1.0f) + 1.0f) * 0.5f;
    return std::sin(p * halfPi);
}

float NodeCanvas::pathSplitGainAfter(int parentRow, const std::string& sourceNodeId, int ignoreBranchRow) const {
    constexpr float halfPi = 1.57079632679f;
    float gain = 1.0f;
    for (int row : {0, 1, 3, 4}) {
        if (row == ignoreBranchRow) continue;
        if (!m_rows[row].hasSplitSection || !m_rows[row].enabled || m_rows[row].parentRow != parentRow ||
            m_rows[row].splitAfterNodeId != sourceNodeId) continue;
        if (!m_rows[row].mainInputEnabled) {
            gain = 0.0f;
            continue;
        }
        if (m_rows[row].splitMode == GridRow::SplitMode::Copy) continue;
        const float p = (std::clamp(m_rows[row].splitPosition, -1.0f, 1.0f) + 1.0f) * 0.5f;
        gain *= std::cos(p * halfPi);
    }
    return gain;
}

float NodeCanvas::pathMixerGainBefore(int parentRow, const std::string& destinationNodeId) const {
    float gain = 1.0f;
    for (int row : {0, 1, 3, 4}) {
        if (!m_rows[row].hasSplitSection || m_rows[row].parentRow != parentRow ||
            m_rows[row].mergeBeforeNodeId != destinationNodeId) continue;
        gain *= m_rows[row].mainInputEnabled ? std::clamp(m_rows[row].mainMix, 0.0f, 1.0f) : 0.0f;
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

void NodeCanvas::setDragGap(int row, int plusIdx, bool isSecondOfCol) {
    m_dragGapRow = row;
    m_dragGapCol = plusIdx;
    m_dragGapIsSecondOfCol = isSecondOfCol;

    if (row >= 0 && row < NUM_ROWS && plusIdx >= 0 && plusIdx < NUM_COLS && m_dragPlaceholderItem) {
        qreal H = std::max(200.0, (qreal)viewport()->height());
        qreal rowCenters[NUM_ROWS];
        calculateRowCenters(rowCenters, H);
        qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : 160.0;
        qreal trackLeft = MARGIN_X + inputW + 16.0;

        qreal slotX = trackLeft + plusIdx * 168.0 + 28.0;

        m_dragPlaceholderItem->setRect(0, 0, PLUG_NODE_W, PLUG_NODE_H);
        m_dragPlaceholderItem->setPos(snapToGrid(slotX), rowCenters[row] - PLUG_NODE_H / 2.0);
        m_dragPlaceholderItem->show();
    }
}

void NodeCanvas::clearDragGap() {
    if (m_dragGapRow != -1 || m_dragGapCol != -1) {
        m_dragGapRow = -1;
        m_dragGapCol = -1;
        m_dragGapIsSecondOfCol = false;
        if (m_dragPlaceholderItem) m_dragPlaceholderItem->hide();
    }
}

void NodeCanvas::reflowLayoutWithDragGap() {
    qreal W = std::max(400.0, (qreal)viewport()->width());
    int longestChain = 0;
    for (const auto& row : m_rows) longestChain = std::max(longestChain, row.count());
    qreal minimumChainW = longestChain * PLUG_NODE_W + (longestChain + 1) * (INSERT_BTN_W + 2 * MIN_SPACING);
    W = std::max(W, 2 * MARGIN_X + 2 * SYS_NODE_W + 32.0 + minimumChainW);
    qreal H = std::max(200.0, (qreal)viewport()->height());
    constexpr qreal LANE_HEIGHT = 140.0;
    qreal offset[NUM_ROWS] = {0.0};
    if (m_rows[1].hasSplitSection) {
        offset[1] = -LANE_HEIGHT;
        offset[0] = m_rows[0].hasSplitSection ? -2 * LANE_HEIGHT : -LANE_HEIGHT;
    }
    if (m_rows[3].hasSplitSection) {
        offset[3] = LANE_HEIGHT;
        offset[4] = m_rows[4].hasSplitSection ? 2 * LANE_HEIGHT : LANE_HEIGHT;
    }
    qreal minY = 0.0;
    qreal maxY = 0.0;
    for (int r = 0; r < NUM_ROWS; ++r) {
        if (r == MAIN_ROW || m_rows[r].hasSplitSection) {
            minY = std::min(minY, offset[r]);
            maxY = std::max(maxY, offset[r]);
        }
    }
    qreal activeSpan = maxY - minY;
    H = std::max(H, activeSpan + 2 * MARGIN_Y + 40.0);

    qreal rowCenters[NUM_ROWS];
    calculateRowCenters(rowCenters, H);

    qreal sysCY = rowCenters[MAIN_ROW];
    qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : 160.0;
    qreal outputW = m_sysOutputWidget ? m_sysOutputWidget->width() : 160.0;
    qreal trackLeft  = MARGIN_X + inputW + 16.0;
    qreal trackRight = W - MARGIN_X - outputW - 16.0;
    qreal trackW     = trackRight - trackLeft;

    if (m_dragPlaceholderItem) m_dragPlaceholderItem->hide();

    // 1. Reflow main row (r == MAIN_ROW) first
    reflowRow(MAIN_ROW, rowCenters[MAIN_ROW], trackLeft, trackRight, trackW);

    // 2. Reflow level 1 side rows (r == 1 and r == 3)
    reflowRow(1, rowCenters[1], trackLeft, trackRight, trackW);
    reflowRow(3, rowCenters[3], trackLeft, trackRight, trackW);

    // 3. Reflow level 2 side rows (r == 0 and r == 4)
    reflowRow(0, rowCenters[0], trackLeft, trackRight, trackW);
    reflowRow(4, rowCenters[4], trackLeft, trackRight, trackW);
}

void NodeCanvas::reflowRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW) {
    const GridRow& row = m_rows[r];
    std::vector<int> occupied;
    for (int c = 0; c < NUM_COLS; ++c)
        if (row.plugins[c]) occupied.push_back(c);

    int n = (int)occupied.size();
    if (occupied.empty()) return;

    // Calculate child split/merge gaps on this row
    int numGaps = 0;
    for (int i = 0; i <= n; ++i) {
        bool hasSplitGap = false;
        bool hasMergeGap = false;
        for (int child : {0, 1, 3, 4}) {
            if (child != r && m_rows[child].hasSplitSection && m_rows[child].parentRow == r) {
                int splitCol = getSplitCol(child);
                int splitIdx = (splitCol < 0) ? 0 : splitCol + 1;
                if (splitIdx == i) hasSplitGap = true;

                int mergeCol = getMergeCol(child);
                int mergeIdx = (mergeCol < 0) ? n : mergeCol;
                if (mergeIdx == i) hasMergeGap = true;
            }
        }
        if (hasSplitGap) numGaps++;
        if (hasMergeGap) numGaps++;
    }
    qreal totalGapW = numGaps * 56.0;

    qreal startX = trackLeft;
    qreal endX = trackRight;

    if (r != MAIN_ROW) {
        startX = row.parentSplitX;
        qreal requiredW = 0.0;
        qreal totalNodeW1 = 0.0;
        for (int c : occupied) {
            qreal w = m_nodeWidgets[r][c] ? m_nodeWidgets[r][c]->width() : PLUG_NODE_W;
            totalNodeW1 += w;
        }
        qreal totalInsertW1 = (n + 1) * INSERT_BTN_W;
        int numSpaces1 = 2 * n + 2;
        requiredW = totalNodeW1 + totalInsertW1 + numSpaces1 * MIN_SPACING + totalGapW;
        requiredW = std::max(requiredW, 168.0);
        endX = std::max(row.parentMergeX, startX + requiredW);
    }

    qreal rowTrackW = std::max(50.0, endX - startX);

    qreal totalNodeW = 0.0;
    for (int c : occupied) {
        if (m_nodeWidgets[r][c]) {
            totalNodeW += m_nodeWidgets[r][c]->width();
        }
    }

    qreal totalInsertW = (n + 1) * INSERT_BTN_W;

    bool hasGap = (r == m_dragGapRow && m_dragGapCol >= 0 && m_dragGapCol <= n);
    qreal gapW = hasGap ? 160.0 : 0.0;

    int numSpaces = 2 * n + 2 + (hasGap ? 2 : 0);
    qreal availableSpacing = (rowTrackW - totalNodeW - totalInsertW - gapW - totalGapW) / (qreal)numSpaces;
    qreal spacing = std::clamp(availableSpacing, MIN_SPACING, MAX_SPACING);

    qreal contentW = totalNodeW + totalInsertW + gapW + numSpaces * spacing;
    qreal xCursor = startX + std::max(0.0, (rowTrackW - contentW - totalGapW) / 2.0);

    for (int i = 0; i <= n; ++i) {
        bool hasSplitGap = false;
        bool hasMergeGap = false;
        for (int child : {0, 1, 3, 4}) {
            if (child != r && m_rows[child].hasSplitSection && m_rows[child].parentRow == r) {
                int splitCol = getSplitCol(child);
                int splitIdx = (splitCol < 0) ? 0 : splitCol + 1;
                if (splitIdx == i) hasSplitGap = true;

                int mergeCol = getMergeCol(child);
                int mergeIdx = (mergeCol < 0) ? n : mergeCol;
                if (mergeIdx == i) hasMergeGap = true;
            }
        }

        int numGapsAtI = (hasSplitGap ? 1 : 0) + (hasMergeGap ? 1 : 0);

        if (numGapsAtI > 0) {
            // Spacing before plus button (left)
            xCursor += spacing;

            if (hasGap && i == m_dragGapCol) {
                if (m_dragPlaceholderItem) {
                    m_dragPlaceholderItem->setRect(0, 0, 160, 80);
                    m_dragPlaceholderItem->setPos(xCursor, cy - 40);
                    m_dragPlaceholderItem->show();
                }
                xCursor += 160.0;
                xCursor += spacing;
            }

            // Find all plus buttons at row r, index i
            std::vector<PlusButtonWidget*> buttons;
            for (auto* item : m_scene->items()) {
                if (auto* pb = dynamic_cast<PlusButtonWidget*>(item)) {
                    if (pb->getRow() == r && pb->getCol() == i) {
                        buttons.push_back(pb);
                    }
                }
            }
            std::sort(buttons.begin(), buttons.end(), [](PlusButtonWidget* a, PlusButtonWidget* b) {
                return a->scenePos().x() < b->scenePos().x();
            });

            qreal currentGapOffset = 56.0;

            if (buttons.size() > 0) {
                setItemTargetPos(buttons[0], QPointF(snapToGrid(xCursor + INSERT_BTN_W / 2.0), cy), true);
            }

            if (numGapsAtI > 1) {
                if (buttons.size() > 1) {
                    setItemTargetPos(buttons[1], QPointF(snapToGrid(xCursor + INSERT_BTN_W / 2.0 + 56.0), cy), true);
                }
                if (buttons.size() > 2) {
                    setItemTargetPos(buttons[2], QPointF(snapToGrid(xCursor + INSERT_BTN_W / 2.0 + 112.0), cy), true);
                }
                currentGapOffset = 112.0;
            } else {
                if (buttons.size() > 1) {
                    setItemTargetPos(buttons[1], QPointF(snapToGrid(xCursor + INSERT_BTN_W / 2.0 + 56.0), cy), true);
                }
            }

            xCursor += INSERT_BTN_W + currentGapOffset;

        } else {
            // Spacing before plus button
            xCursor += spacing;

            if (hasGap && i == m_dragGapCol) {
                if (m_dragPlaceholderItem) {
                    m_dragPlaceholderItem->setRect(0, 0, 160, 80);
                    m_dragPlaceholderItem->setPos(xCursor, cy - 40);
                    m_dragPlaceholderItem->show();
                }
                xCursor += 160.0;
                xCursor += spacing;
            }

            std::vector<PlusButtonWidget*> buttons;
            for (auto* item : m_scene->items()) {
                if (auto* pb = dynamic_cast<PlusButtonWidget*>(item)) {
                    if (pb->getRow() == r && pb->getCol() == i) {
                        buttons.push_back(pb);
                    }
                }
            }
            if (buttons.size() > 0) {
                setItemTargetPos(buttons[0], QPointF(snapToGrid(xCursor + INSERT_BTN_W / 2.0), cy), true);
            }
            xCursor += INSERT_BTN_W;
        }

        if (i < n) {
            // Spacing before node
            xCursor += spacing;

            int c = occupied[i];
            if (auto* nw = m_nodeWidgets[r][c]) {
                setItemTargetPos(nw, QPointF(xCursor, cy - nw->height() / 2.0), true);
                xCursor += nw->width();
            }
        }
    }
}

void NodeCanvas::movePluginToGap(int fromRow, int fromCol, int toRow, int toColGap, bool isSecondOfCol) {
    if (fromRow < 0 || fromRow >= NUM_ROWS || fromCol < 0 || fromCol >= NUM_COLS) return;
    if (toRow < 0 || toRow >= NUM_ROWS || toColGap < 0 || toColGap >= NUM_COLS) return;

    auto plugin = m_rows[fromRow].plugins[fromCol];
    if (!plugin) return;

    m_engine->suspendProcessing();

    m_rows[fromRow].plugins[fromCol] = nullptr;
    m_rows[toRow].plugins[toColGap] = plugin;

    if (toRow == 0 || toRow == 2) {
        m_rows[toRow].hasSplitSection = true;
        if (!m_rows[toRow].levelConfigured) setBranchDefaults(toRow, 0.25118864f);
        m_rows[toRow].enabled = true;
        updateBranchGains(toRow);
    }

    m_engine->resumeProcessing();

    applyRoutingChange();
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
