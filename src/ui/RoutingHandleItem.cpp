#include "RoutingHandleItem.h"
#include "NodeCanvas.h"
#include "PlusButtonWidget.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QCursor>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <iostream>

RoutingHandleItem::RoutingHandleItem(NodeCanvas* canvas, int branchRow, bool isSplit)
    : m_canvas(canvas), m_branchRow(branchRow), m_isSplit(isSplit) {
    setAcceptHoverEvents(true);
    setFlags(QGraphicsItem::ItemIsMovable);
    setZValue(20); // Top layer so it's always draggable
    m_previewLine = new QGraphicsPathItem(this);
    m_previewLine->setPen(QPen(QColor(255, 200, 50), 2, Qt::DashLine, Qt::RoundCap));
    m_previewLine->setZValue(-1);
    m_previewLine->hide();
    m_previewText = new QGraphicsSimpleTextItem(this);
    m_previewText->setBrush(QColor(255, 220, 100));
    m_previewText->setFont(QFont("", 9, QFont::DemiBold));
    m_previewText->hide();
}

void RoutingHandleItem::updatePreview(PlusButtonWidget* target) {
    if (target != m_previewTarget) {
        if (m_previewTarget) m_previewTarget->setRoutingTarget(false);
        m_previewTarget = target;
        if (m_previewTarget) m_previewTarget->setRoutingTarget(true);
    }
    if (!target) {
        m_previewLine->hide();
        m_previewText->hide();
        return;
    }

    const QPointF targetPoint = mapFromScene(target->scenePos());
    QPainterPath previewPath;
    previewPath.moveTo(0, 0);
    previewPath.lineTo(targetPoint);
    m_previewLine->setPath(previewPath);
    m_previewLine->show();

    std::vector<int> occupied;
    for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
        if (m_canvas->getPluginAt(1, c)) occupied.push_back(c);
    }
    const int plusIndex = target->getCol();
    QString text;
    if (m_isSplit) {
        text = plusIndex == 0
            ? "Split from System Input"
            : "Split after " + QString::fromStdString(m_canvas->getPluginAt(1, occupied[plusIndex - 1])->getName());
    } else {
        text = plusIndex >= static_cast<int>(occupied.size())
            ? "Merge to System Output"
            : "Merge before " + QString::fromStdString(m_canvas->getPluginAt(1, occupied[plusIndex])->getName());
    }
    m_previewText->setText(text);
    m_previewText->setPos(10, targetPoint.y() / 2.0 - 10);
    m_previewText->show();
}

void RoutingHandleItem::clearPreview() {
    if (m_previewTarget) m_previewTarget->setRoutingTarget(false);
    m_previewTarget = nullptr;
    m_previewLine->hide();
    m_previewText->hide();
}

QRectF RoutingHandleItem::boundingRect() const {
    return QRectF(-12, -12, 24, 24);
}

void RoutingHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);

    QColor color = (m_hovered || m_dragging) ? QColor(255, 200, 50) : QColor(0, 176, 255);
    
    // Draw outer circle
    painter->setPen(QPen(color, 2));
    painter->setBrush(QColor(18, 18, 22));
    painter->drawEllipse(QRectF(-7, -7, 14, 14));

    // Draw inner handle dot/icon
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    if (m_isSplit) {
        // Draw split symbol: small '+' or dot
        painter->drawEllipse(QRectF(-3, -3, 6, 6));
    } else {
        // Draw merge symbol: small square or triangle
        QPointF points[3] = { QPointF(-2, -3), QPointF(3, 0), QPointF(-2, 3) };
        painter->drawPolygon(points, 3);
    }
}

void RoutingHandleItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    m_dragging = true;
    m_dragStartPos = event->scenePos();
    setCursor(Qt::SizeHorCursor);
    update();
    event->accept();
}

void RoutingHandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragging) {
        qreal currentMouseX = event->scenePos().x();
        
        // Find all PlusButtonWidgets in row 1 (the main row)
        PlusButtonWidget* nearestPlus = nullptr;
        qreal minDistance = 999999.0;
        for (auto* item : scene()->items()) {
            if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                if (plus->getRow() == 1) {
                    qreal dx = std::abs(plus->scenePos().x() - currentMouseX);
                    if (dx < minDistance) {
                        minDistance = dx;
                        nearestPlus = plus;
                    }
                }
            }
        }

        if (nearestPlus) {
            // Visually snap this handle to the nearest PlusButton's X coordinate
            setPos(nearestPlus->scenePos().x(), pos().y());
            updatePreview(nearestPlus);
            
            // Re-render scene highlighting during drag
            // Optional: we can trigger live preview, but let's just snap position
        } else {
            // Fallback: move freely horizontally
            setPos(currentMouseX, pos().y());
            updatePreview(nullptr);
        }
    }
    event->accept();
}

void RoutingHandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        update();
        
        qreal currentMouseX = event->scenePos().x();
        
        // Find nearest PlusButton in row 1
        PlusButtonWidget* nearestPlus = nullptr;
        qreal minDistance = 999999.0;
        for (auto* item : scene()->items()) {
            if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                if (plus->getRow() == 1) {
                    qreal dx = std::abs(plus->scenePos().x() - currentMouseX);
                    if (dx < minDistance) {
                        minDistance = dx;
                        nearestPlus = plus;
                    }
                }
            }
        }

        if (nearestPlus) {
            int plusIndex = nearestPlus->getCol(); // index 0..n of the gap in row 1
            
            // Get occupied nodes in row 1
            std::vector<int> occupied;
            for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
                if (m_canvas->getPluginAt(1, c)) occupied.push_back(c);
            }
            
            if (m_isSplit) {
                // Split after occupied[plusIndex - 1] (or -1 if plusIndex == 0)
                int targetSplitCol = -1;
                if (plusIndex > 0 && plusIndex - 1 < (int)occupied.size()) {
                    targetSplitCol = occupied[plusIndex - 1];
                }
                m_canvas->setSplitCol(m_branchRow, targetSplitCol);
            } else {
                // Merge before occupied[plusIndex] (or -1 if plusIndex == size)
                int targetMergeCol = -1;
                if (plusIndex < (int)occupied.size()) {
                    targetMergeCol = occupied[plusIndex];
                }
                m_canvas->setMergeCol(m_branchRow, targetMergeCol);
            }
        }
        clearPreview();
        
        // setSplitCol and setMergeCol automatically trigger deferred updateLayout() and rebuild connections.
        // So we don't need any additional updateLayout calls here!
    }
    event->accept();
}

void RoutingHandleItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    m_hovered = true;
    setCursor(Qt::OpenHandCursor);
    update();
    event->accept();
}

void RoutingHandleItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    m_hovered = false;
    setCursor(Qt::ArrowCursor);
    update();
    if (!m_dragging) clearPreview();
    event->accept();
}
