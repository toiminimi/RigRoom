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

// ─── Dimensions ───────────────────────────────────────────────────────────────
static constexpr qreal SYS_NODE_W  = 120.0;
static constexpr qreal SYS_NODE_H  = 54.0;
static constexpr qreal PLUG_NODE_W = 120.0;
static constexpr qreal PLUG_NODE_H = 54.0;
static constexpr qreal MARGIN_X    = 14.0;
static constexpr qreal MARGIN_Y    = 30.0;
// Width of the small '+' slot between nodes
static constexpr qreal INSERT_BTN_W = 18.0;
static constexpr qreal MIN_SPACING = 4.0;
static constexpr qreal MAX_SPACING = 12.0;
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

class BranchLabelItem final : public QGraphicsItem {
public:
    BranchLabelItem(NodeCanvas* canvas, int row, bool enabled)
        : m_canvas(canvas), m_row(row), m_enabled(enabled) {
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
        setZValue(8);
    }

    QRectF boundingRect() const override { return QRectF(0, 0, 66, 22); }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(m_enabled ? QColor(0, 176, 255) : QColor(75, 75, 85), 1));
        painter->setBrush(m_hovered ? QColor(38, 48, 58) : QColor(25, 25, 30));
        painter->drawRoundedRect(boundingRect(), 5, 5);
        painter->setPen(m_enabled ? QColor(90, 210, 255) : QColor(125, 125, 135));
        painter->drawText(boundingRect(), Qt::AlignCenter, m_row == 0 ? "PATH A" : "PATH B");
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
}

NodeCanvas::~NodeCanvas() {
    clearSceneItems();
}

// ─── Public API ───────────────────────────────────────────────────────────────
void NodeCanvas::insertPluginAt(int row, int col, std::shared_ptr<AudioNode> node) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    m_rows[row].plugins[col] = node;
    if (row == 0 || row == 2) {
        if (!m_rows[row].levelConfigured) m_rows[row].mix = 1.0f;
        m_rows[row].enabled = true;
        updateBranchGains(row);
    }
    m_engine->addNode(node);
    updateLayout();
    emit routingChanged();
}

// Insert before the current occupant at 'col' — shift everything right by one
void NodeCanvas::insertPluginBefore(int row, int col, std::shared_ptr<AudioNode> node) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    
    // Collect the current chain in order
    std::vector<std::shared_ptr<AudioNode>> chain;
    for (int c = 0; c < NUM_COLS; ++c)
        if (m_rows[row].plugins[c]) chain.push_back(m_rows[row].plugins[c]);
    
    // Figure out the insertion position in the chain
    // col here means "insert at position col within the chain" (0 = before first, chain.size() = after last)
    int insertPos = std::min(col, (int)chain.size());
    chain.insert(chain.begin() + insertPos, node);
    
    // Never silently discard an effect when a chain is full.
    if ((int)chain.size() > NUM_COLS) return;
    
    // Write back
    for (int c = 0; c < NUM_COLS; ++c)
        m_rows[row].plugins[c] = (c < (int)chain.size()) ? chain[c] : nullptr;
    if (row == 0 || row == 2) {
        if (!m_rows[row].levelConfigured) m_rows[row].mix = 1.0f;
        m_rows[row].enabled = true;
        updateBranchGains(row);
    }
    
    m_engine->addNode(node);
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::removePluginAt(int row, int col) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    if (m_rows[row].plugins[col]) {
        m_engine->removeNode(m_rows[row].plugins[col]->uniqueId);
        // Shift remaining nodes left so chain stays compact
        for (int c = col; c < NUM_COLS - 1; ++c)
            m_rows[row].plugins[c] = m_rows[row].plugins[c + 1];
        m_rows[row].plugins[NUM_COLS - 1] = nullptr;
    }
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::replacePluginAt(int row, int col, std::shared_ptr<AudioNode> newNode) {
    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) return;
    if (m_rows[row].plugins[col]) {
        m_engine->removeNode(m_rows[row].plugins[col]->uniqueId);
    }
    m_rows[row].plugins[col] = newNode;
    if (newNode) {
        if (row == 0 || row == 2) {
            if (!m_rows[row].levelConfigured) m_rows[row].mix = 1.0f;
            m_rows[row].enabled = true;
            updateBranchGains(row);
        }
        m_engine->addNode(newNode);
    }
    updateLayout();
    emit routingChanged();
}

void NodeCanvas::movePlugin(int fromRow, int fromCol, int toRow, int toCol) {
    if (fromRow < 0 || fromRow >= NUM_ROWS || fromCol < 0 || fromCol >= NUM_COLS) return;
    if (toRow < 0 || toRow >= NUM_ROWS || toCol < 0 || toCol >= NUM_COLS) return;
    if (fromRow == toRow && fromCol == toCol) return;

    auto movingNode = m_rows[fromRow].plugins[fromCol];
    if (!movingNode) return;

    if (fromRow == toRow) {
        // Same row: shift chain elements to swap positions
        std::vector<std::shared_ptr<AudioNode>> chain;
        for (int c = 0; c < NUM_COLS; ++c)
            if (m_rows[fromRow].plugins[c]) chain.push_back(m_rows[fromRow].plugins[c]);
        
        // Find positions in chain
        auto itFrom = std::find(chain.begin(), chain.end(), movingNode);
        auto destNode = m_rows[toRow].plugins[toCol];
        auto itTo = destNode ? std::find(chain.begin(), chain.end(), destNode) : chain.end();
        
        if (itFrom != chain.end()) {
            chain.erase(itFrom);
            if (itTo != chain.end()) {
                // Re-find after erase
                itTo = std::find(chain.begin(), chain.end(), destNode);
                if (itTo != chain.end()) {
                    chain.insert(itTo, movingNode);
                } else {
                    chain.push_back(movingNode);
                }
            } else {
                chain.push_back(movingNode);
            }
        }
        
        for (int c = 0; c < NUM_COLS; ++c)
            m_rows[fromRow].plugins[c] = (c < (int)chain.size()) ? chain[c] : nullptr;
    } else {
        // Different row: simple swap of slots
        std::swap(m_rows[fromRow].plugins[fromCol], m_rows[toRow].plugins[toCol]);
        if (toRow == 0 || toRow == 2) {
            if (!m_rows[toRow].levelConfigured) m_rows[toRow].mix = 1.0f;
            m_rows[toRow].enabled = true;
            updateBranchGains(toRow);
        }
    }

    updateLayout();
    emit routingChanged();
}

void NodeCanvas::clearCanvas() {
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < NUM_COLS; ++c)
            m_rows[r].plugins[c] = nullptr;
        if (r == 0 || r == 2) {
            m_rows[r].splitCol = -1;
            m_rows[r].mergeCol = -1;
            m_rows[r].mix = 1.0f;
            m_rows[r].pan = 0.0f;
            m_rows[r].enabled = false;
            m_rows[r].levelConfigured = false;
            updateBranchGains(r);
        }
    }
    m_engine->clearGraph();
    clearSceneItems();
    for (int r = 0; r < NUM_ROWS; ++r)
        for (int c = 0; c < NUM_COLS; ++c)
            m_nodeWidgets[r][c] = nullptr;
    updateLayout();
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

void NodeCanvas::nodeDoubleClicked(NodeWidget* node) {
    emit editPluginUI(node->getAudioNode());
}

void NodeCanvas::setSystemChannelModes(bool inputStereo, bool outputStereo) {
    if (m_sysInputWidget) m_sysInputWidget->setChannelMode(inputStereo);
    if (m_sysOutputWidget) m_sysOutputWidget->setChannelMode(outputStereo);
}

void NodeCanvas::onPlusButtonClicked(int row, int col, QPoint screenPos) {
    emit plusButtonClicked(row, col, screenPos);
}

// ─── Layout Engine ────────────────────────────────────────────────────────────
void NodeCanvas::updateLayout() {
    clearSceneItems();

    qreal W = std::max(400.0, (qreal)viewport()->width());
    int longestChain = 0;
    for (const auto& row : m_rows) longestChain = std::max(longestChain, row.count());
    qreal minimumChainW = longestChain * PLUG_NODE_W + (longestChain + 1) * (INSERT_BTN_W + 2 * MIN_SPACING);
    W = std::max(W, 2 * MARGIN_X + 2 * SYS_NODE_W + 32.0 + minimumChainW);
    qreal H = std::max(200.0, (qreal)viewport()->height());
    m_scene->setSceneRect(0, 0, W, H);

    // Row centres (evenly divide vertical space)
    qreal rowH = (H - 2 * MARGIN_Y) / (qreal)NUM_ROWS;
    qreal rowCenters[NUM_ROWS];
    for (int r = 0; r < NUM_ROWS; ++r)
        rowCenters[r] = snapToGrid(MARGIN_Y + (r + 0.5) * rowH);

    qreal sysCY = rowCenters[1];

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

    // 1. Layout middle row (r == 1) first so that side rows can query its node positions
    layoutRow(1, rowCenters[1], trackLeft, trackRight, trackW, sysRightX, sysLeftX, sysMidY);

    // 2. Layout side rows (r == 0 and r == 2)
    layoutRow(0, rowCenters[0], trackLeft, trackRight, trackW, sysRightX, sysLeftX, sysMidY);
    layoutRow(2, rowCenters[2], trackLeft, trackRight, trackW, sysRightX, sysLeftX, sysMidY);
    
    // ── If row[1] is empty but other rows have blocks, draw trunk ──────────
    if (m_rows[1].isEmpty()) {
        bool othersHaveBlocks = !m_rows[0].isEmpty() || !m_rows[2].isEmpty();
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

    // ── Rebuild audio engine connections ──────────────────────────────────
    rebuildAudioConnections();

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
}

void NodeCanvas::layoutRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW, qreal sysRightX, qreal sysLeftX, qreal sysMidY) {
    const GridRow& row = m_rows[r];
    std::vector<int> occupied;
    for (int c = 0; c < NUM_COLS; ++c)
        if (row.plugins[c]) occupied.push_back(c);

    // Determine starting and ending X coordinates for this row based on split and merge configs
    qreal startX = trackLeft;
    qreal endX = trackRight;

    if (r == 0 || r == 2) {
        auto getPlusButtonX = [&](int plusIdx) -> qreal {
            for (auto* item : m_scene->items()) {
                if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                    if (plus->getRow() == 1 && plus->getCol() == plusIdx) {
                        return plus->scenePos().x();
                    }
                }
            }
            if (plusIdx == 0) return snapToGrid(sysRightX + 16.0);
            return snapToGrid(sysLeftX - 16.0);
        };

        // Get occupied columns in row 1
        std::vector<int> occupied1;
        for (int c = 0; c < NUM_COLS; ++c) {
            if (m_rows[1].plugins[c]) occupied1.push_back(c);
        }
        int n1 = (int)occupied1.size();

        // Split X
        int splitPlusIdx = 0;
        if (row.splitCol >= 0) {
            auto it = std::find(occupied1.begin(), occupied1.end(), row.splitCol);
            if (it != occupied1.end()) {
                int idx = std::distance(occupied1.begin(), it);
                splitPlusIdx = idx + 1;
            }
        }
        startX = getPlusButtonX(splitPlusIdx);

        // Merge X
        int mergePlusIdx = n1;
        if (row.mergeCol >= 0) {
            auto it = std::find(occupied1.begin(), occupied1.end(), row.mergeCol);
            if (it != occupied1.end()) {
                int idx = std::distance(occupied1.begin(), it);
                mergePlusIdx = idx;
            }
        }
        endX = getPlusButtonX(mergePlusIdx);
    }

    qreal rowTrackW = std::max(50.0, endX - startX);

    if (r == 0 || r == 2) {
        auto* branchLabel = new BranchLabelItem(this, r, row.enabled);
        branchLabel->setPos(snapToGrid(trackLeft) + 6.0, cy - 36.0);
        m_scene->addItem(branchLabel);
        m_dynamicItems.push_back(branchLabel);
    }

    if (occupied.empty()) {
        // Empty row
        if (r == 1) {
            auto* wire = new QGraphicsPathItem();
            QPainterPath path;
            path.moveTo(sysRightX, sysMidY);
            path.lineTo(sysLeftX, sysMidY);
            wire->setPath(path);
            wire->setPen(QPen(QColor(0, 176, 255, 70), 2, Qt::SolidLine, Qt::RoundCap));
            wire->setZValue(-2);
            m_scene->addItem(wire);
            m_dynamicItems.push_back(wire);
        } else {
            auto* wire = new QGraphicsPathItem();
            QPainterPath path;
            path.moveTo(startX, cy);
            path.lineTo(endX, cy);
            wire->setPath(path);
            const QColor color = row.enabled ? QColor(0, 176, 255, 130) : QColor(60, 60, 70, 40);
            wire->setPen(QPen(color, row.enabled ? 2.0 : 1.0, row.enabled ? Qt::SolidLine : Qt::DashLine));
            wire->setZValue(-3);
            m_scene->addItem(wire);
            m_dynamicItems.push_back(wire);
            if (row.enabled) {
                auto* splitWire = new QGraphicsPathItem();
                QPainterPath splitPath;
                splitPath.moveTo(startX, sysMidY);
                splitPath.lineTo(startX, cy);
                splitWire->setPath(splitPath);
                splitWire->setPen(QPen(QColor(0, 176, 255, 130), 1.5, Qt::DashLine));
                splitWire->setZValue(-3);
                m_scene->addItem(splitWire);
                m_dynamicItems.push_back(splitWire);

                auto* mergeWire = new QGraphicsPathItem();
                QPainterPath mergePath;
                mergePath.moveTo(endX, cy);
                mergePath.lineTo(endX, sysMidY);
                mergeWire->setPath(mergePath);
                mergeWire->setPen(QPen(QColor(0, 176, 255, 130), 1.5, Qt::DashLine));
                mergeWire->setZValue(-3);
                m_scene->addItem(mergeWire);
                m_dynamicItems.push_back(mergeWire);

                auto* splitHandle = new RoutingHandleItem(this, r, true);
                splitHandle->setPos(startX, (sysMidY + cy) / 2.0);
                m_scene->addItem(splitHandle);
                m_dynamicItems.push_back(splitHandle);

                auto* mergeHandle = new RoutingHandleItem(this, r, false);
                mergeHandle->setPos(endX, (sysMidY + cy) / 2.0);
                m_scene->addItem(mergeHandle);
                m_dynamicItems.push_back(mergeHandle);
            }
        }

        // ONE centred plus button per empty row
        const qreal cx = row.enabled
            ? snapToGrid((startX + endX) / 2.0)
            : snapToGrid(trackLeft + trackW / 2.0);
        auto* plus = new PlusButtonWidget(r, 0, PlusButtonWidget::Style::Full);
        plus->setPos(cx, cy);
        m_scene->addItem(plus);
        m_dynamicItems.push_back(plus);

    } else {
        // Occupied row
        int n = (int)occupied.size();

        // Instantiate any missing widgets and calculate actual total width of all nodes
        qreal totalNodeW = 0.0;
        for (int c : occupied) {
            if (!m_nodeWidgets[r][c]) {
                m_nodeWidgets[r][c] = new NodeWidget(row.plugins[c]);
                m_scene->addItem(m_nodeWidgets[r][c]);
            }
            totalNodeW += m_nodeWidgets[r][c]->width();
        }

        qreal totalInsertW = (n + 1) * INSERT_BTN_W;
        int numSpaces = 2 * n + 2;
        qreal availableSpacing = (rowTrackW - totalNodeW - totalInsertW) / (qreal)numSpaces;
        qreal spacing = std::clamp(availableSpacing, MIN_SPACING, MAX_SPACING);

        // Draw horizontal wire segment
        {
            qreal segStartX = (r == 1) ? sysRightX : startX;
            qreal segEndX   = (r == 1) ? sysLeftX  : endX;
            auto* seg = new QGraphicsPathItem();
            QPainterPath path;
            path.moveTo(segStartX, cy);
            path.lineTo(segEndX, cy);
            seg->setPath(path);
            QColor color = (r == 1 || row.enabled) ? QColor(0, 176, 255, 160) : QColor(70, 70, 80, 70);
            seg->setPen(QPen(color, 2, row.enabled || r == 1 ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap));
            seg->setZValue(-2);
            m_scene->addItem(seg);
            m_dynamicItems.push_back(seg);
        }

        // Draw branch connectors (vertical split/merge lines) for side rows
        if (r != 1) {
            // Split connector (vertical line down from cy to sysMidY)
            auto* branch = new QGraphicsPathItem();
            QPainterPath bpath;
            if (row.splitCol < 0) {
                bpath.moveTo(startX - 8, sysMidY);
                bpath.lineTo(startX - 8, cy);
                bpath.lineTo(startX, cy);
            } else {
                bpath.moveTo(startX, sysMidY);
                bpath.lineTo(startX, cy);
            }
            branch->setPath(bpath);
            branch->setPen(QPen(QColor(0, 176, 255, 100), 1.5, Qt::DashLine, Qt::RoundCap));
            branch->setZValue(-3);
            branch->setToolTip("Branch Split point. Drag the handle on the vertical line to change routing.");
            m_scene->addItem(branch);
            m_dynamicItems.push_back(branch);

            // Add Split point draggable handle
            auto* splitHandle = new RoutingHandleItem(this, r, true);
            qreal splitY = (sysMidY + cy) / 2.0;
            splitHandle->setPos(startX, splitY);
            splitHandle->setToolTip(r == 0 ? "Drag the handle on the vertical line to change top branch split point" : "Drag the handle on the vertical line to change bottom branch split point");
            m_scene->addItem(splitHandle);
            m_dynamicItems.push_back(splitHandle);

            // Merge connector
            auto* branchR = new QGraphicsPathItem();
            QPainterPath bpathR;
            if (row.mergeCol < 0) {
                bpathR.moveTo(endX, cy);
                bpathR.lineTo(endX + 8, cy);
                bpathR.lineTo(endX + 8, sysMidY);
            } else {
                bpathR.moveTo(endX, cy);
                bpathR.lineTo(endX, sysMidY);
            }
            branchR->setPath(bpathR);
            branchR->setPen(QPen(QColor(0, 176, 255, 100), 1.5, Qt::DashLine, Qt::RoundCap));
            branchR->setZValue(-3);
            branchR->setToolTip("Branch Merge point. Drag the handle on the vertical line to change routing.");
            m_scene->addItem(branchR);
            m_dynamicItems.push_back(branchR);

            // Add Merge point draggable handle
            auto* mergeHandle = new RoutingHandleItem(this, r, false);
            qreal mergeY = (sysMidY + cy) / 2.0;
            mergeHandle->setPos(endX, mergeY);
            mergeHandle->setToolTip(r == 0 ? "Drag the handle on the vertical line to change top branch merge point" : "Drag the handle on the vertical line to change bottom branch merge point");
            m_scene->addItem(mergeHandle);
            m_dynamicItems.push_back(mergeHandle);
        }

        // Clear stale node widgets not in this row's current chain
        for (int c = 0; c < NUM_COLS; ++c) {
            if (!row.plugins[c] && m_nodeWidgets[r][c]) {
                m_scene->removeItem(m_nodeWidgets[r][c]);
                delete m_nodeWidgets[r][c];
                m_nodeWidgets[r][c] = nullptr;
            }
        }

        // Place nodes and insert-buttons interleaved:
        // Pattern: [spacing] [BTN 0] [spacing] [NODE 0] [spacing] [BTN 1] ... [BTN n]
        qreal contentW = totalNodeW + totalInsertW + numSpaces * spacing;
        qreal xCursor = startX + std::max(0.0, (rowTrackW - contentW) / 2.0);
        for (int i = 0; i <= n; ++i) {
            // 1. Spacing before plus button
            xCursor += spacing;

            // 2. Place plus button
            auto* plus = new PlusButtonWidget(r, i, PlusButtonWidget::Style::Ghost);
            plus->setPos(snapToGrid(xCursor + INSERT_BTN_W / 2.0), cy);
            m_scene->addItem(plus);
            m_dynamicItems.push_back(plus);
            xCursor += INSERT_BTN_W;

            if (i < n) {
                // 3. Spacing before node
                xCursor += spacing;

                // 4. Place node
                int c = occupied[i];
                NodeWidget* nw = m_nodeWidgets[r][c];
                nw->setPos(xCursor, cy - nw->height() / 2.0);
                nw->show();
                xCursor += nw->width();
            }
        }
    }
}

void NodeCanvas::rebuildAudioConnections() {
    m_engine->clearGraph();
    
    // Connect dry bypass for the primary middle row (r == 1) if it contains no active blocks
    bool middleRowEmpty = true;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[1].plugins[c]) {
            middleRowEmpty = false;
            break;
        }
    }
    if (middleRowEmpty) {
        m_engine->connectPorts("system_input", 0, "system_output", 0);
        m_engine->connectPorts("system_input", 1, "system_output", 1);
    }
    
    // Re-add all active nodes to the audio engine first
    for (int r = 0; r < NUM_ROWS; ++r) {
        for (int c = 0; c < NUM_COLS; ++c) {
            if (m_rows[r].plugins[c]) {
                m_engine->addNode(m_rows[r].plugins[c]);
            }
        }
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

    // ─── Connect Main Row (Row 1) ───
    std::vector<std::shared_ptr<AudioNode>> mainChain;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[1].plugins[c]) mainChain.push_back(m_rows[1].plugins[c]);
    }

    if (!mainChain.empty()) {
        connect("system_input", 0, mainChain[0]->uniqueId, 0);
        if (mainChain[0]->getAudioInputCount() > 1)
            connect("system_input", 1, mainChain[0]->uniqueId, 1);

        for (size_t i = 0; i + 1 < mainChain.size(); ++i) {
            connectNodes(mainChain[i], mainChain[i+1]);
        }

        auto& last = mainChain.back();
        int outs = last->getAudioOutputCount();
        connect(last->uniqueId, 0, "system_output", 0);
        if (outs >= 2)
            connect(last->uniqueId, 1, "system_output", 1);
        else
            connect(last->uniqueId, 0, "system_output", 1);
    }

    // Helpers to find routing source / destination nodes in Row 1
    auto findSplitSourceNode = [&](int splitCol) -> std::shared_ptr<AudioNode> {
        if (splitCol >= 0) {
            for (int c = splitCol; c >= 0; --c) {
                if (m_rows[1].plugins[c]) return m_rows[1].plugins[c];
            }
        }
        return nullptr;
    };

    auto findMergeDestNode = [&](int mergeCol) -> std::shared_ptr<AudioNode> {
        if (mergeCol >= 0) {
            for (int c = mergeCol; c < NUM_COLS; ++c) {
                if (m_rows[1].plugins[c]) return m_rows[1].plugins[c];
            }
        }
        return nullptr;
    };

    struct RouteEndpoint {
        std::string id;
        int channels;
    };
    auto connectBalanced = [&](const RouteEndpoint& src, const RouteEndpoint& dst, const BranchGainControls& gains) {
        if (src.channels >= 2 && dst.channels >= 2) {
            connect(src.id, 0, dst.id, 0, 1.0f, gains.left);
            connect(src.id, 1, dst.id, 1, 1.0f, gains.right);
        } else if (src.channels >= 2 && dst.channels == 1) {
            connect(src.id, 0, dst.id, 0, 1.0f, gains.monoHalf);
            connect(src.id, 1, dst.id, 0, 1.0f, gains.monoHalf);
        } else if (dst.channels >= 2) {
            connect(src.id, 0, dst.id, 0, 1.0f, gains.left);
            connect(src.id, 0, dst.id, 1, 1.0f, gains.right);
        } else {
            connect(src.id, 0, dst.id, 0, 1.0f, gains.mono);
        }
    };

    // ─── Connect Side Rows (Row 0 and Row 2) ───
    for (int r : {0, 2}) {
        const GridRow& row = m_rows[r];
        if (!row.enabled) continue;
        updateBranchGains(r);
        std::vector<std::shared_ptr<AudioNode>> chain;
        for (int c = 0; c < NUM_COLS; ++c) {
            if (row.plugins[c]) chain.push_back(row.plugins[c]);
        }

        auto splitSrc = findSplitSourceNode(row.splitCol);
        auto mergeDst = findMergeDestNode(row.mergeCol);

        // A branch must move forward through the main chain to avoid feedback.
        if (splitSrc && mergeDst) {
            const auto splitPosition = findNode(splitSrc);
            const auto mergePosition = findNode(mergeDst);
            if (splitPosition.second >= mergePosition.second) continue;
        }

        const RouteEndpoint source{
            splitSrc ? splitSrc->uniqueId : "system_input",
            splitSrc ? splitSrc->getAudioOutputCount() : 2
        };
        const RouteEndpoint destination{
            mergeDst ? mergeDst->uniqueId : "system_output",
            mergeDst ? mergeDst->getAudioInputCount() : 2
        };

        if (chain.empty()) {
            connectBalanced(source, destination, m_branchGains[r]);
            continue;
        }

        // Connect chain internal links
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            connectNodes(chain[i], chain[i+1]);
        }

        // Connect first node input (Split point)
        if (splitSrc) {
            connectNodes(splitSrc, chain[0]);
        } else {
            connect("system_input", 0, chain[0]->uniqueId, 0);
            if (chain[0]->getAudioInputCount() > 1)
                connect("system_input", 1, chain[0]->uniqueId, 1);
        }

        // Connect last node output (Merge point) with return mix gain
        const auto& last = chain.back();
        connectBalanced({last->uniqueId, last->getAudioOutputCount()}, destination, m_branchGains[r]);
    }

    m_engine->rebuildGraph();
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
    if (row == 0 || row == 2) {
        m_rows[row].splitCol = col;
        rebuildAudioConnections();
        QTimer::singleShot(0, [this]() {
            updateLayout();
            emit routingChanged();
        });
    }
}

void NodeCanvas::setMergeCol(int row, int col) {
    if (row == 0 || row == 2) {
        m_rows[row].mergeCol = col;
        rebuildAudioConnections();
        QTimer::singleShot(0, [this]() {
            updateLayout();
            emit routingChanged();
        });
    }
}

void NodeCanvas::updateBranchGains(int row) {
    if (row != 0 && row != 2) return;
    const float level = std::clamp(m_rows[row].mix, 0.0f, 1.0f);
    const float pan = std::clamp(m_rows[row].pan, -1.0f, 1.0f);
    m_branchGains[row].left->store(level * (pan > 0.0f ? 1.0f - pan : 1.0f), std::memory_order_relaxed);
    m_branchGains[row].right->store(level * (pan < 0.0f ? 1.0f + pan : 1.0f), std::memory_order_relaxed);
    m_branchGains[row].mono->store(level, std::memory_order_relaxed);
    m_branchGains[row].monoHalf->store(level * 0.5f, std::memory_order_relaxed);
}

void NodeCanvas::setMix(int row, float val) {
    if (row != 0 && row != 2) return;
    m_rows[row].mix = std::clamp(val, 0.0f, 1.0f);
    m_rows[row].levelConfigured = true;
    updateBranchGains(row);
    emit routingChanged();
}

void NodeCanvas::setPan(int row, float val) {
    if (row != 0 && row != 2) return;
    m_rows[row].pan = std::clamp(val, -1.0f, 1.0f);
    updateBranchGains(row);
    emit routingChanged();
}

void NodeCanvas::setBranchEnabled(int row, bool enabled) {
    if (row != 0 && row != 2) return;
    if (enabled && !m_rows[row].enabled && m_rows[row].isEmpty() && !m_rows[row].levelConfigured) {
        m_rows[row].mix = 0.0f;
        m_rows[row].levelConfigured = true;
        updateBranchGains(row);
    }
    m_rows[row].enabled = enabled;
    updateLayout();
    emit routingChanged();
}

int NodeCanvas::getBranchOutputChannels(int row) const {
    if (row != 0 && row != 2) return 0;
    for (int c = NUM_COLS - 1; c >= 0; --c) {
        if (m_rows[row].plugins[c]) return m_rows[row].plugins[c]->getAudioOutputCount();
    }
    if (m_rows[row].splitCol >= 0) {
        for (int c = m_rows[row].splitCol; c >= 0; --c) {
            if (m_rows[1].plugins[c]) return m_rows[1].plugins[c]->getAudioOutputCount();
        }
    }
    return 2;
}

int NodeCanvas::getBranchDestinationChannels(int row) const {
    if (row != 0 && row != 2) return 0;
    if (m_rows[row].mergeCol >= 0) {
        for (int c = m_rows[row].mergeCol; c < NUM_COLS; ++c) {
            if (m_rows[1].plugins[c]) return m_rows[1].plugins[c]->getAudioInputCount();
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

void NodeCanvas::setDragGap(int row, int plusIdx) {
    // Find currently dragged node widget
    NodeWidget* draggedNode = nullptr;
    for (auto* item : m_scene->items()) {
        if (auto* nw = dynamic_cast<NodeWidget*>(item)) {
            if (nw->isDragging()) {
                draggedNode = nw;
                break;
            }
        }
    }

    if (draggedNode) {
        auto [fromRow, fromCol] = findNode(draggedNode->getAudioNode());
        if (fromRow == row && (plusIdx == fromCol || plusIdx == fromCol + 1)) {
            // Current position in the chain - do not open a gap!
            clearDragGap();
            return;
        }
    }

    if (m_dragGapRow != row || m_dragGapCol != plusIdx) {
        m_dragGapRow = row;
        m_dragGapCol = plusIdx;
        reflowLayoutWithDragGap();
    }
}

void NodeCanvas::clearDragGap() {
    if (m_dragGapRow != -1 || m_dragGapCol != -1) {
        m_dragGapRow = -1;
        m_dragGapCol = -1;
        if (m_dragPlaceholderItem) m_dragPlaceholderItem->hide();
        reflowLayoutWithDragGap();
    }
}

void NodeCanvas::reflowLayoutWithDragGap() {
    qreal W = std::max(400.0, (qreal)viewport()->width());
    int longestChain = 0;
    for (const auto& row : m_rows) longestChain = std::max(longestChain, row.count());
    qreal minimumChainW = longestChain * PLUG_NODE_W + (longestChain + 1) * (INSERT_BTN_W + 2 * MIN_SPACING);
    W = std::max(W, 2 * MARGIN_X + 2 * SYS_NODE_W + 32.0 + minimumChainW);
    qreal H = std::max(200.0, (qreal)viewport()->height());

    // Row centres (evenly divide vertical space)
    qreal rowH = (H - 2 * MARGIN_Y) / (qreal)NUM_ROWS;
    qreal rowCenters[NUM_ROWS];
    for (int r = 0; r < NUM_ROWS; ++r)
        rowCenters[r] = snapToGrid(MARGIN_Y + (r + 0.5) * rowH);

    qreal sysCY = rowCenters[1];
    qreal inputW = m_sysInputWidget ? m_sysInputWidget->width() : 160.0;
    qreal outputW = m_sysOutputWidget ? m_sysOutputWidget->width() : 160.0;
    qreal trackLeft  = MARGIN_X + inputW + 16.0;
    qreal trackRight = W - MARGIN_X - outputW - 16.0;
    qreal trackW     = trackRight - trackLeft;

    if (m_dragPlaceholderItem) m_dragPlaceholderItem->hide();

    for (int r = 0; r < NUM_ROWS; ++r) {
        reflowRow(r, rowCenters[r], trackLeft, trackRight, trackW);
    }
}

void NodeCanvas::reflowRow(int r, qreal cy, qreal trackLeft, qreal trackRight, qreal trackW) {
    const GridRow& row = m_rows[r];
    std::vector<int> occupied;
    for (int c = 0; c < NUM_COLS; ++c)
        if (row.plugins[c]) occupied.push_back(c);

    int n = (int)occupied.size();
    if (occupied.empty()) return;

    qreal startX = trackLeft;
    qreal endX = trackRight;

    if (r == 0 || r == 2) {
        auto getPlusButtonX = [&](int plusIdx) -> qreal {
            if (auto* plus = findPlusButton(1, plusIdx)) {
                return plus->scenePos().x();
            }
            if (plusIdx == 0) return snapToGrid(trackLeft);
            return snapToGrid(trackRight);
        };

        std::vector<int> occupied1;
        for (int c = 0; c < NUM_COLS; ++c) {
            if (m_rows[1].plugins[c]) occupied1.push_back(c);
        }
        int n1 = (int)occupied1.size();

        int splitPlusIdx = 0;
        if (row.splitCol >= 0) {
            auto it = std::find(occupied1.begin(), occupied1.end(), row.splitCol);
            if (it != occupied1.end()) {
                int idx = std::distance(occupied1.begin(), it);
                splitPlusIdx = idx + 1;
            }
        }
        startX = getPlusButtonX(splitPlusIdx);

        int mergePlusIdx = n1;
        if (row.mergeCol >= 0) {
            auto it = std::find(occupied1.begin(), occupied1.end(), row.mergeCol);
            if (it != occupied1.end()) {
                int idx = std::distance(occupied1.begin(), it);
                mergePlusIdx = idx;
            }
        }
        endX = getPlusButtonX(mergePlusIdx);
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
    qreal availableSpacing = (rowTrackW - totalNodeW - totalInsertW - gapW) / (qreal)numSpaces;
    qreal spacing = std::clamp(availableSpacing, MIN_SPACING, MAX_SPACING);

    qreal contentW = totalNodeW + totalInsertW + gapW + numSpaces * spacing;
    qreal xCursor = startX + std::max(0.0, (rowTrackW - contentW) / 2.0);
    for (int i = 0; i <= n; ++i) {
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

        if (auto* pb = findPlusButton(r, i)) {
            setItemTargetPos(pb, QPointF(snapToGrid(xCursor + INSERT_BTN_W / 2.0), cy), true);
        }
        xCursor += INSERT_BTN_W;

        if (i < n) {
            xCursor += spacing;
            int c = occupied[i];
            if (auto* nw = m_nodeWidgets[r][c]) {
                setItemTargetPos(nw, QPointF(xCursor, cy - nw->height() / 2.0), true);
                xCursor += nw->width();
            }
        }
    }
}

void NodeCanvas::movePluginToGap(int fromRow, int fromCol, int toRow, int toColGap) {
    if (fromRow < 0 || fromRow >= NUM_ROWS || fromCol < 0 || fromCol >= NUM_COLS) return;
    if (toRow < 0 || toRow >= NUM_ROWS) return;

    auto plugin = m_rows[fromRow].plugins[fromCol];
    if (!plugin) return;

    m_engine->suspendProcessing();

    m_rows[fromRow].plugins[fromCol] = nullptr;
    std::vector<std::shared_ptr<AudioNode>> fromChain;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[fromRow].plugins[c]) {
            fromChain.push_back(m_rows[fromRow].plugins[c]);
        }
    }
    for (int c = 0; c < NUM_COLS; ++c) {
        m_rows[fromRow].plugins[c] = (c < (int)fromChain.size()) ? fromChain[c] : nullptr;
    }

    if (fromRow == toRow && toColGap > fromCol) {
        toColGap--;
    }

    std::vector<std::shared_ptr<AudioNode>> toChain;
    for (int c = 0; c < NUM_COLS; ++c) {
        if (m_rows[toRow].plugins[c]) {
            toChain.push_back(m_rows[toRow].plugins[c]);
        }
    }

    if (toColGap >= 0 && toColGap <= (int)toChain.size()) {
        toChain.insert(toChain.begin() + toColGap, plugin);
    } else {
        toChain.push_back(plugin);
    }

    for (int c = 0; c < NUM_COLS; ++c) {
        m_rows[toRow].plugins[c] = (c < (int)toChain.size()) ? toChain[c] : nullptr;
    }
    if (toRow == 0 || toRow == 2) {
        if (!m_rows[toRow].levelConfigured) m_rows[toRow].mix = 1.0f;
        m_rows[toRow].enabled = true;
        updateBranchGains(toRow);
    }

    m_engine->resumeProcessing();

    rebuildAudioConnections();
    updateLayout();
    emit routingChanged();
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
