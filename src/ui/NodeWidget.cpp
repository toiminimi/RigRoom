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

static QString categoryNameFor(const std::shared_ptr<AudioNode>& node) {
    if (node->getType() == NodeType::SystemInput) return "SYSTEM IN";
    if (node->getType() == NodeType::SystemOutput) return "SYSTEM OUT";
    EffectIcon icon = effectIconFor(node);
    switch (icon) {
    case EffectIcon::Input: return "SYSTEM IN";
    case EffectIcon::Output: return "SYSTEM OUT";
    case EffectIcon::Dynamics: return "DYNAMICS";
    case EffectIcon::Delay: return "DELAY";
    case EffectIcon::Reverb: return "REVERB";
    case EffectIcon::Modulation: return "MODULATION";
    case EffectIcon::Amp: return "AMP";
    case EffectIcon::Utility: return "UTILITY";
    }
    return "PLUGIN";
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
    m_height = std::max(58, 36 + (maxPorts - 1) * 22);
    
    // Position port pins symmetrically centered vertically along node height
    const float midY = m_height / 2.0f;
    if (!m_inputPorts.empty()) {
        float inSpacing = 20.0f;
        float inStartY = midY - ((m_inputPorts.size() - 1) * inSpacing / 2.0f);
        for (size_t i = 0; i < m_inputPorts.size(); ++i) {
            m_inputPorts[i]->setPos(0, inStartY + i * inSpacing);
        }
    }
    if (!m_outputPorts.empty()) {
        float outSpacing = 20.0f;
        float outStartY = midY - ((m_outputPorts.size() - 1) * outSpacing / 2.0f);
        for (size_t i = 0; i < m_outputPorts.size(); ++i) {
            m_outputPorts[i]->setPos(m_width, outStartY + i * outSpacing);
        }
    }
    
    m_bypassRect = QRectF(4, 3, 16, 16);

    // Rich tooltip for complete info
    setToolTip(QString("Name: %1\nCategory: %2\nMode: %3\nURI: %4")
        .arg(QString::fromStdString(audioNode->getName()))
        .arg(categoryNameFor(audioNode))
        .arg(m_isStereo ? "Stereo" : "Mono")
        .arg(QString::fromStdString(audioNode->getPluginURI())));
}

QRectF NodeWidget::boundingRect() const {
    return QRectF(-10, -5, m_width + 20, m_height + 10);
}

void NodeWidget::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    bool bypass = m_audioNode->isBypassed();
    bool selected = isSelected();
    bool isMissing = m_audioNode->isMissing();
    bool isSystemNode = m_audioNode->getType() == NodeType::SystemInput || m_audioNode->getType() == NodeType::SystemOutput;
    
    // 1. Draw card background
    QColor cardBg = isMissing ? QColor(42, 20, 20) : (bypass ? QColor(36, 36, 40) : QColor(24, 24, 28));
    
    QPen borderPen;
    if (m_dragging) {
        borderPen = QPen(QColor(0, 200, 255), 2);
        cardBg = QColor(20, 40, 55);
    } else if (isMissing) {
        borderPen = QPen(selected ? QColor(255, 82, 82) : QColor(211, 47, 47), selected ? 2 : 1.5, Qt::DashLine);
    } else {
        borderPen = QPen(selected ? QColor(0, 176, 255) : QColor(60, 60, 68), selected ? 2 : 1);
    }
    
    painter->setPen(borderPen);
    painter->setBrush(cardBg);
    painter->drawRoundedRect(QRectF(0, 0, m_width, m_height), 6, 6);
    
    // 2. Draw Header bar background
    EffectIcon iconType = effectIconFor(m_audioNode);
    QColor headerColor;
    if (isMissing) headerColor = QColor(183, 28, 28); // Red
    else if (m_audioNode->getType() == NodeType::SystemInput) headerColor = QColor(142, 36, 170); // Purple
    else if (m_audioNode->getType() == NodeType::SystemOutput) headerColor = QColor(46, 125, 50); // Green
    else if (m_dragging) headerColor = QColor(0, 130, 160);
    else if (bypass) headerColor = QColor(50, 50, 56);
    else {
        switch (iconType) {
        case EffectIcon::Dynamics: headerColor = QColor(230, 81, 0); break;   // Orange
        case EffectIcon::Delay: headerColor = QColor(0, 131, 143); break;     // Cyan Dark
        case EffectIcon::Reverb: headerColor = QColor(0, 105, 92); break;     // Teal
        case EffectIcon::Modulation: headerColor = QColor(194, 24, 91); break;// Pink
        case EffectIcon::Amp: headerColor = QColor(216, 67, 21); break;       // Rust
        default: headerColor = QColor(69, 90, 100); break;                     // Slate
        }
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(headerColor);
    painter->drawRoundedRect(QRectF(0, 0, m_width, 22), 6, 6);
    painter->drawRect(QRectF(0, 14, m_width, 8));

    // 3. Header Bar Controls (Power button + Category Title + Drag handle)
    if (!isSystemNode) {
        // Power Switch button
        painter->setPen(QPen(bypass ? QColor(160, 160, 160) : QColor(255, 60, 60), 1.8));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QRectF(6, 5, 10, 10));
        painter->drawLine(11, 3, 11, 8);

        // Drag hint grab lines (≡)
        painter->setPen(QPen(QColor(255, 255, 255, 120), 1.2));
        qreal gx = m_width - 14;
        painter->drawLine(QPointF(gx, 6), QPointF(gx + 7, 6));
        painter->drawLine(QPointF(gx, 10), QPointF(gx + 7, 10));
        painter->drawLine(QPointF(gx, 14), QPointF(gx + 7, 14));
    }

    // Category Label in Header
    QFont headerFont = painter->font();
    headerFont.setBold(true);
    headerFont.setPixelSize(8.5);
    headerFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    painter->setFont(headerFont);
    painter->setPen(QColor(255, 255, 255, 230));
    qreal catX = isSystemNode ? 6 : 22;
    qreal catW = isSystemNode ? m_width - 12 : m_width - 38;
    QString catName = isMissing ? "⚠️ MISSING" : categoryNameFor(m_audioNode);
    painter->drawText(QRectF(catX, 0, catW, 22), Qt::AlignCenter, catName);

    // 4. Main Body: Display FULL Plugin Name (larger, bold 11-12px white text)
    QString title = QString::fromStdString(m_audioNode->getName());
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    
    // Scale font size based on title length so longer titles fit cleanly
    if (title.length() > 24) titleFont.setPixelSize(10.0);
    else if (title.length() > 14) titleFont.setPixelSize(11.0);
    else titleFont.setPixelSize(12.0);

    painter->setFont(titleFont);
    painter->setPen(isMissing ? QColor(255, 138, 128) : (bypass ? QColor(160, 160, 165) : QColor(255, 255, 255)));

    QRectF titleRect(8, 22, m_width - 16, m_height - 36);
    QTextOption optionText;
    optionText.setWrapMode(QTextOption::WordWrap);
    optionText.setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    painter->drawText(titleRect, title, optionText);

    // 5. Footer Row: Category Icon + STEREO/MONO Badge
    drawEffectIcon(painter, iconType, QPointF(14, m_height - 10));

    QString channelLabel = isMissing ? "MISSING" : (m_isStereo ? "STEREO" : "MONO");
    QFont badgeFont = painter->font();
    badgeFont.setBold(true);
    badgeFont.setPixelSize(7);
    painter->setFont(badgeFont);
    QFontMetrics badgeMetrics(badgeFont);
    qreal badgeWidth = badgeMetrics.horizontalAdvance(channelLabel) + 8;
    QRectF badgeRect(m_width - badgeWidth - 6, m_height - 15, badgeWidth, 11);
    painter->setPen(Qt::NoPen);
    painter->setBrush(isMissing ? QColor(211, 47, 47) : (m_isStereo ? QColor(0, 137, 123) : QColor(69, 90, 100)));
    painter->drawRoundedRect(badgeRect, 3, 3);
    painter->setPen(QColor(255, 255, 255, 230));
    painter->drawText(badgeRect, Qt::AlignCenter, channelLabel);
    
    // 6. Dragging ghost overlay
    if (m_dragging) {
        painter->setBrush(QColor(0, 160, 200, 30));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(QRectF(0, 0, m_width, m_height), 6, 6);
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
