#include "NodeWidget.h"
#include "PortWidget.h"
#include "../audio/AudioEngine.h"
#include "NodeCanvas.h"
#include "PlusButtonWidget.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsScene>
#include <QApplication>
#include <QString>
#include <QTimer>
#include <cmath>
#include <iostream>

static constexpr int DRAG_THRESHOLD = 8; // pixels before drag starts

enum class EffectIcon { Input, Output, Dynamics, Delay, Reverb, Modulation, Amp, Utility };

static EffectIcon effectIconFor(const std::shared_ptr<AudioNode>& node) {
    if (node->getType() == NodeType::SystemInput) return EffectIcon::Input;
    if (node->getType() == NodeType::SystemOutput) return EffectIcon::Output;

    QString description = QString::fromStdString(node->getName() + " " + node->getPluginURI()).toLower();
    if (description.contains("compress") || description.contains("gate") || description.contains("limit") || description.contains("expand")) return EffectIcon::Dynamics;
    if (description.contains("delay") || description.contains("echo") || description.contains("tap")) return EffectIcon::Delay;
    if (description.contains("reverb") || description.contains("room") || description.contains("hall")) return EffectIcon::Reverb;
    if (description.contains("chorus") || description.contains("flanger") || description.contains("phaser") || description.contains("vibrato") || description.contains("tremolo")) return EffectIcon::Modulation;
    if (description.contains("amp") || description.contains("cab") || description.contains("dist") || description.contains("overdrive") || description.contains("fuzz")) return EffectIcon::Amp;
    return EffectIcon::Utility;
}

static void drawEffectIcon(QPainter* painter, EffectIcon icon, const QPointF& center) {
    painter->save();
    painter->translate(center);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor(255, 255, 255, 210), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);

    switch (icon) {
    case EffectIcon::Input:
        painter->drawLine(-6, 0, 5, 0);
        painter->drawLine(2, -3, 5, 0);
        painter->drawLine(2, 3, 5, 0);
        break;
    case EffectIcon::Output:
        painter->drawLine(-5, 0, 6, 0);
        painter->drawLine(3, -3, 6, 0);
        painter->drawLine(3, 3, 6, 0);
        break;
    case EffectIcon::Dynamics:
        painter->drawLine(-5, 4, -5, -1);
        painter->drawLine(-1, 4, -1, -5);
        painter->drawLine(3, 4, 3, 1);
        break;
    case EffectIcon::Delay:
        painter->drawEllipse(QRectF(-5, -5, 10, 10));
        painter->drawLine(0, 0, 0, -3);
        painter->drawLine(0, 0, 3, 2);
        break;
    case EffectIcon::Reverb:
        painter->drawLine(-5, -3, 5, 3);
        painter->drawLine(-5, 1, 1, 5);
        painter->drawLine(0, -5, 5, -2);
        break;
    case EffectIcon::Modulation: {
        QPainterPath wave;
        wave.moveTo(-6, 0);
        wave.cubicTo(-3, -5, 0, 5, 6, 0);
        painter->drawPath(wave);
        break;
    }
    case EffectIcon::Amp:
        painter->drawRoundedRect(QRectF(-6, -4, 12, 8), 1.5, 1.5);
        painter->drawEllipse(QRectF(-3, -1, 2, 2));
        painter->drawEllipse(QRectF(1, -1, 2, 2));
        break;
    case EffectIcon::Utility:
        painter->drawLine(-5, -3, 5, -3);
        painter->drawLine(-5, 3, 5, 3);
        painter->drawEllipse(QRectF(-2, -5, 4, 4));
        painter->drawEllipse(QRectF(1, 1, 4, 4));
        break;
    }
    painter->restore();
}

NodeWidget::NodeWidget(std::shared_ptr<AudioNode> audioNode)
    : m_audioNode(audioNode) {
    setFlags(QGraphicsItem::ItemIsSelectable);
    
    // Group and instantiate visual port pins
    int inCount = 0;
    int outCount = 0;
    
    auto& ports = audioNode->getPorts();
    for (size_t i = 0; i < ports.size(); ++i) {
        m_isStereo = m_isStereo || ports[i].isStereo;
        if (ports[i].isInput) {
            if (ports[i].isStereo) {
                if (ports[i].channelIdx == 0) {
                    auto* port = new PortWidget(this, i, true, true);
                    m_inputPorts.push_back(port);
                    inCount++;
                }
            } else {
                auto* port = new PortWidget(this, i, true, false);
                m_inputPorts.push_back(port);
                inCount++;
            }
        } else {
            if (ports[i].isStereo) {
                if (ports[i].channelIdx == 0) {
                    auto* port = new PortWidget(this, i, false, true);
                    m_outputPorts.push_back(port);
                    outCount++;
                }
            } else {
                auto* port = new PortWidget(this, i, false, false);
                m_outputPorts.push_back(port);
                outCount++;
            }
        }
    }
    
    // Compute box height dynamically based on port count
    int maxPorts = std::max(inCount, outCount);
    m_height = std::max(54, 34 + maxPorts * 24);
    
    // Position port pins along edges
    for (size_t i = 0; i < m_inputPorts.size(); ++i) {
        m_inputPorts[i]->setPos(0, 34 + i * 24);
    }
    for (size_t i = 0; i < m_outputPorts.size(); ++i) {
        m_outputPorts[i]->setPos(m_width, 34 + i * 24);
    }
    
    m_bypassRect = QRectF(8, 7, 16, 16);
}

QRectF NodeWidget::boundingRect() const {
    return QRectF(-10, -5, m_width + 20, m_height + 10);
}

void NodeWidget::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    bool bypass = m_audioNode->isBypassed();
    bool selected = isSelected();
    
    // 1. Draw card background
    QColor cardBg = bypass ? QColor(45, 45, 45) : QColor(32, 32, 32);
    
    QPen borderPen;
    if (m_dragging) {
        borderPen = QPen(QColor(0, 200, 255), 2);
        cardBg = QColor(20, 40, 55);
    } else {
        borderPen = QPen(selected ? QColor(0, 176, 255) : QColor(64, 64, 64), selected ? 2 : 1);
    }
    
    painter->setPen(borderPen);
    painter->setBrush(cardBg);
    painter->drawRoundedRect(QRectF(0, 0, m_width, m_height), 8, 8);
    
    // 2. Draw Title bar background
    QColor headerColor = m_audioNode->getType() == NodeType::SystemInput ? QColor(100, 30, 30) :
                         m_audioNode->getType() == NodeType::SystemOutput ? QColor(30, 100, 30) :
                         m_dragging ? QColor(0, 130, 160) :
                         bypass ? QColor(58, 58, 58) :
                         (m_audioNode->getPluginURI() == "builtin:bypass" ? QColor(84, 110, 122) : QColor(0, 150, 136));
    painter->setPen(Qt::NoPen);
    painter->setBrush(headerColor);
    painter->drawRoundedRect(QRectF(0, 0, m_width, 26), 8, 8);
    painter->drawRect(QRectF(0, 18, m_width, 8));

    // Drag hint: draw a small grab icon (≡ lines) on the right side of header
    if (m_audioNode->getType() != NodeType::SystemInput && m_audioNode->getType() != NodeType::SystemOutput) {
        painter->setPen(QPen(QColor(255, 255, 255, 100), 1.5));
        qreal gx = m_width - 16;
        painter->drawLine(QPointF(gx, 8), QPointF(gx + 8, 8));
        painter->drawLine(QPointF(gx, 13), QPointF(gx + 8, 13));
        painter->drawLine(QPointF(gx, 18), QPointF(gx + 8, 18));
    }
    
    // 3. Draw Title text
    painter->setPen(QColor(255, 255, 255));
    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    bool isSystemNode = m_audioNode->getType() == NodeType::SystemInput || m_audioNode->getType() == NodeType::SystemOutput;
    qreal titleWidth = isSystemNode ? m_width - 34 : m_width - 48;
    painter->drawText(QRectF(28, 0, titleWidth, 26), Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(m_audioNode->getName()));
    
    // 4. Draw Bypass button (Power Switch)
    if (m_audioNode->getType() != NodeType::SystemInput && m_audioNode->getType() != NodeType::SystemOutput) {
        painter->setPen(QPen(bypass ? QColor(180, 180, 180) : QColor(255, 52, 52), 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QRectF(7, 7, 12, 12));
        painter->drawLine(13, 5, 13, 11);
    }

    // The body communicates block type and channel format once, rather than on every socket.
    drawEffectIcon(painter, effectIconFor(m_audioNode), QPointF(m_width / 2.0, m_height - 14));

    QString channelLabel = m_isStereo ? "STEREO" : "MONO";
    QFont badgeFont = painter->font();
    badgeFont.setBold(true);
    badgeFont.setPixelSize(7);
    painter->setFont(badgeFont);
    QFontMetrics badgeMetrics(badgeFont);
    qreal badgeWidth = badgeMetrics.horizontalAdvance(channelLabel) + 10;
    QRectF badgeRect(m_width - badgeWidth - 6, m_height - 17, badgeWidth, 12);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_isStereo ? QColor(0, 130, 100) : QColor(75, 82, 90));
    painter->drawRoundedRect(badgeRect, 3, 3);
    painter->setPen(QColor(255, 255, 255, 220));
    painter->drawText(badgeRect, Qt::AlignCenter, channelLabel);
    
    // 5. Dragging ghost overlay
    if (m_dragging) {
        painter->setBrush(QColor(0, 160, 200, 30));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(QRectF(0, 0, m_width, m_height), 8, 8);
    }
}

void NodeWidget::toggleBypass() {
    if (m_audioNode->getType() != NodeType::SystemInput && m_audioNode->getType() != NodeType::SystemOutput) {
        m_audioNode->setBypassed(!m_audioNode->isBypassed());
        update();
    }
}

QVariant NodeWidget::itemChange(GraphicsItemChange change, const QVariant &value) {
    return QGraphicsItem::itemChange(change, value);
}

void NodeWidget::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // System nodes can't be dragged
    if (m_audioNode->getType() == NodeType::SystemInput || m_audioNode->getType() == NodeType::SystemOutput) {
        QGraphicsItem::mousePressEvent(event);
        auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
        if (canvas) emit canvas->nodeSelected(m_audioNode);
        return;
    }

    // If click is on the bypass button, trigger toggle
    if (m_bypassRect.contains(event->pos())) {
        toggleBypass();
        event->accept();
        return;
    }
    
    // Record drag start
    m_dragStartPos = event->scenePos();
    m_dragging = false;
    
    QGraphicsItem::mousePressEvent(event);
    
    auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
    if (canvas) emit canvas->nodeSelected(m_audioNode);
}

void NodeWidget::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_audioNode->getType() == NodeType::SystemInput || m_audioNode->getType() == NodeType::SystemOutput) {
        QGraphicsItem::mouseMoveEvent(event);
        return;
    }

    QPointF delta = event->scenePos() - m_dragStartPos;
    qreal dist = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
    
    if (!m_dragging && dist > DRAG_THRESHOLD) {
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
        setZValue(10);
        update();
    }
    
    if (m_dragging) {
        // Move this widget visually with the mouse
        setPos(pos() + event->scenePos() - event->lastScenePos());
        
        // Find nearest PlusButtonWidget to mouse cursor to visually split nodes and show the slot
        auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
        if (canvas) {
            PlusButtonWidget* closestPb = nullptr;
            qreal minDist = 1e9;
            QPointF mousePos = event->scenePos();

            for (auto* item : scene()->items()) {
                if (auto* pb = dynamic_cast<PlusButtonWidget*>(item)) {
                    QPointF pbPos = pb->scenePos();
                    qreal dx = mousePos.x() - pbPos.x();
                    qreal dy = mousePos.y() - pbPos.y();
                    qreal d = std::sqrt(dx * dx + dy * dy);
                    
                    if (std::abs(dy) < 80.0 && d < minDist) {
                        minDist = d;
                        closestPb = pb;
                    }
                }
            }

            if (closestPb) {
                canvas->setDragGap(closestPb->getRow(), closestPb->getCol(), closestPb->isSecondOfCol());
            } else {
                canvas->clearDragGap();
            }
        }
    }
    
    event->accept();
}

void NodeWidget::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_audioNode->getType() == NodeType::SystemInput || m_audioNode->getType() == NodeType::SystemOutput) {
        QGraphicsItem::mouseReleaseEvent(event);
        return;
    }

    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        setZValue(0);
        update();
        
        auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
        if (canvas) {
            int toRow = canvas->getDragGapRow();
            int toCol = canvas->getDragGapCol();
            bool isSecondOfCol = canvas->getDragGapIsSecondOfCol();
            
            // Clear gap visualization first
            canvas->clearDragGap();
            
            if (toRow != -1 && toCol != -1) {
                const auto audioNode = m_audioNode;
                QTimer::singleShot(0, canvas, [canvas, audioNode, toRow, toCol, isSecondOfCol]() {
                    auto [fromRow, fromCol] = canvas->findNode(audioNode);
                    if (fromRow != -1 && fromCol != -1) {
                        canvas->movePluginToGap(fromRow, fromCol, toRow, toCol, isSecondOfCol);
                    } else {
                        canvas->updateLayout();
                    }
                });
            } else {
                // Scene rebuilds delete node widgets, so wait until this event returns.
                QTimer::singleShot(0, canvas, [canvas]() {
                    canvas->updateLayout();
                });
            }
        }
    } else {
        QGraphicsItem::mouseReleaseEvent(event);
    }
    event->accept();
}

void NodeWidget::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
    if (canvas) {
        canvas->nodeDoubleClicked(this);
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
}

void NodeWidget::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    if (m_audioNode->getType() == NodeType::SystemInput || m_audioNode->getType() == NodeType::SystemOutput) {
        event->accept();
        return;
    }
    
    auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
    if (canvas) {
        auto [row, col] = canvas->findNode(m_audioNode);
        if (row != -1 && col != -1) {
            emit canvas->nodeContextMenuRequested(row, col, event->screenPos());
        }
    }
    event->accept();
}
