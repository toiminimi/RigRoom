#include "NodeWidget.h"
#include "PortWidget.h"
#include "../audio/AudioEngine.h"
#include "NodeCanvas.h"
#include "PlusButtonWidget.h"
#include "CanvasMetrics.h"
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

enum class EffectIcon {
    Input, Output, Tuner, Dynamics, Drive, Amp, Cabinet, Equalizer,
    Filter, Modulation, Delay, Reverb, Pitch, Utility
};

static QRectF bypassHitRect(qreal width) {
    return QRectF(width - 26.0, 2.0, 24.0, 24.0);
}

static EffectIcon effectIconFor(const std::shared_ptr<AudioNode>& node) {
    if (node->getType() == NodeType::SystemInput) return EffectIcon::Input;
    if (node->getType() == NodeType::SystemOutput) return EffectIcon::Output;

    const QString description = QString::fromStdString(node->getName() + " " + node->getPluginURI()).toLower();
    if (description.contains("tuner") || description.contains("tuning")) return EffectIcon::Tuner;
    if (description.contains("compress") || description.contains("gate") || description.contains("limit") || description.contains("expand")) return EffectIcon::Dynamics;
    if (description.contains("drive") || description.contains("dist") || description.contains("fuzz") || description.contains("muff") || description.contains("boost")) return EffectIcon::Drive;
    if (description.contains("cabinet") || description.contains("cab sim") || description.contains("speaker cab")) return EffectIcon::Cabinet;
    if (description.contains("equalizer") || description.contains(" eq") || description.startsWith("eq")) return EffectIcon::Equalizer;
    if (description.contains("filter") || description.contains("wah")) return EffectIcon::Filter;
    if (description.contains("pitch") || description.contains("octav") || description.contains("harmon") || description.contains("doubl")) return EffectIcon::Pitch;
    if (description.contains("delay") || description.contains("echo") || description.contains("tap")) return EffectIcon::Delay;
    if (description.contains("reverb") || description.contains("room") || description.contains("hall")) return EffectIcon::Reverb;
    if (description.contains("chorus") || description.contains("flanger") || description.contains("phaser") || description.contains("vibrato") || description.contains("tremolo")) return EffectIcon::Modulation;
    if (description.contains("amp") || description.contains("amplifier")) return EffectIcon::Amp;
    return EffectIcon::Utility;
}

static QColor categoryColor(EffectIcon icon) {
    switch (icon) {
    case EffectIcon::Input: return QColor("#9A52B5");
    case EffectIcon::Output: return QColor("#4D9B63");
    case EffectIcon::Tuner: return QColor("#58BFA3");
    case EffectIcon::Dynamics: return QColor("#D99A45");
    case EffectIcon::Drive: return QColor("#E06B3C");
    case EffectIcon::Amp: return QColor("#D65E5E");
    case EffectIcon::Cabinet: return QColor("#B88B5A");
    case EffectIcon::Equalizer: return QColor("#C9B94F");
    case EffectIcon::Filter: return QColor("#83B65A");
    case EffectIcon::Modulation: return QColor("#C45F9D");
    case EffectIcon::Delay: return QColor("#429FC8");
    case EffectIcon::Reverb: return QColor("#7379D4");
    case EffectIcon::Pitch: return QColor("#A46ACB");
    case EffectIcon::Utility: return QColor("#78879A");
    }
    return QColor("#78879A");
}

static QString categoryLabel(EffectIcon icon) {
    switch (icon) {
    case EffectIcon::Input: return "SYSTEM IN";
    case EffectIcon::Output: return "SYSTEM OUT";
    case EffectIcon::Tuner: return "TUNER";
    case EffectIcon::Dynamics: return "DYNAMICS";
    case EffectIcon::Drive: return "DRIVE";
    case EffectIcon::Amp: return "AMP";
    case EffectIcon::Cabinet: return "CAB";
    case EffectIcon::Equalizer: return "EQ";
    case EffectIcon::Filter: return "FILTER";
    case EffectIcon::Modulation: return "MOD";
    case EffectIcon::Delay: return "DELAY";
    case EffectIcon::Reverb: return "REVERB";
    case EffectIcon::Pitch: return "PITCH";
    case EffectIcon::Utility: return "UTILITY";
    }
    return "PLUGIN";
}

static void drawEffectIcon(QPainter* painter, EffectIcon icon, const QPointF& center, const QColor& color) {
    painter->save();
    painter->translate(center);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);

    switch (icon) {
    case EffectIcon::Input:
    case EffectIcon::Output:
        painter->drawLine(-5, 0, 5, 0);
        painter->drawLine(2, -3, 5, 0);
        painter->drawLine(2, 3, 5, 0);
        break;
    case EffectIcon::Tuner:
        painter->drawArc(QRectF(-5, -4, 10, 10), 20 * 16, 140 * 16);
        painter->drawLine(0, 1, 3, -2);
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
    case EffectIcon::Drive:
        painter->drawLine(-5, 3, -2, -3);
        painter->drawLine(-2, -3, 1, 3);
        painter->drawLine(1, 3, 5, -3);
        break;
    case EffectIcon::Amp:
    case EffectIcon::Cabinet:
        painter->drawRoundedRect(QRectF(-5, -4, 10, 8), 1.5, 1.5);
        painter->drawEllipse(QRectF(-2, -2, 4, 4));
        break;
    case EffectIcon::Equalizer:
        painter->drawLine(-5, -3, 5, -3);
        painter->drawLine(-5, 1, 5, 1);
        painter->drawLine(-5, 5, 5, 5);
        break;
    case EffectIcon::Filter: {
        QPainterPath response;
        response.moveTo(-5, -3);
        response.lineTo(-1, -3);
        response.cubicTo(2, -3, 2, 4, 5, 4);
        painter->drawPath(response);
        break;
    }
    case EffectIcon::Pitch:
        painter->drawLine(-5, 3, 4, -4);
        painter->drawLine(1, -4, 4, -4);
        painter->drawLine(4, -4, 4, -1);
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
    return categoryLabel(effectIconFor(node));
}

NodeWidget::NodeWidget(std::shared_ptr<AudioNode> audioNode)
    : m_audioNode(audioNode) {
    setFlags(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
    m_width = qRound(CanvasMetrics::cardWidth);
    m_height = qRound(CanvasMetrics::cardHeight);
    
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
    m_height = std::max(qRound(CanvasMetrics::cardHeight), 34 + (maxPorts - 1) * 18);
    
    // Position port pins symmetrically centered vertically along node height
    const float midY = m_height / 2.0f;
    if (!m_inputPorts.empty()) {
        float inSpacing = 16.0f;
        float inStartY = midY - ((m_inputPorts.size() - 1) * inSpacing / 2.0f);
        for (size_t i = 0; i < m_inputPorts.size(); ++i) {
            m_inputPorts[i]->setPos(0, inStartY + i * inSpacing);
        }
    }
    if (!m_outputPorts.empty()) {
        float outSpacing = 16.0f;
        float outStartY = midY - ((m_outputPorts.size() - 1) * outSpacing / 2.0f);
        for (size_t i = 0; i < m_outputPorts.size(); ++i) {
            m_outputPorts[i]->setPos(m_width, outStartY + i * outSpacing);
        }
    }
    
    m_bypassRect = bypassHitRect(m_width);

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
    QColor cardBg = isMissing ? QColor(42, 25, 28) : (bypass ? QColor(35, 36, 40) : QColor(28, 29, 33));
    
    QPen borderPen;
    if (m_dragging) {
        borderPen = QPen(QColor(112, 150, 212), 2);
        cardBg = QColor(31, 37, 48);
    } else if (isMissing) {
        borderPen = QPen(selected ? QColor(255, 82, 82) : QColor(211, 47, 47), selected ? 2 : 1.5, Qt::DashLine);
    } else {
        borderPen = QPen(selected ? QColor(123, 145, 202) : QColor(64, 66, 73), selected ? 2 : 1);
    }
    
    painter->setPen(borderPen);
    painter->setBrush(cardBg);
    painter->drawRoundedRect(QRectF(0, 0, m_width, m_height), 6, 6);
    
    // 2. Narrow category rail: semantic colour without a saturated header.
    EffectIcon iconType = effectIconFor(m_audioNode);
    QColor headerColor = categoryColor(iconType);
    if (isMissing) headerColor = QColor(183, 28, 28); // Red
    else if (m_dragging) headerColor = QColor(0, 130, 160);

    painter->setPen(Qt::NoPen);
    painter->setBrush(bypass ? headerColor.darker(175) : headerColor);
    painter->drawRoundedRect(QRectF(0, 0, 4, m_height), 2, 2);

    // The bypass target owns the upper-right corner and never competes with the title.
    if (!isSystemNode) {
        const QColor controlColor = bypass ? QColor(225, 126, 126)
            : QColor(190, 198, 210, (m_hovered || selected) ? 205 : 105);
        if (m_hovered || selected || bypass) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(bypass ? QColor(126, 48, 52, 90) : QColor(255, 255, 255, 12));
            painter->drawRoundedRect(m_bypassRect.adjusted(2, 2, -2, -2), 4, 4);
        }
        const QPointF controlCenter = m_bypassRect.center();
        painter->setPen(QPen(controlColor, 1.4));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QRectF(controlCenter.x() - 4.5, controlCenter.y() - 3.5, 9, 9));
        painter->drawLine(QPointF(controlCenter.x(), controlCenter.y() - 7),
                          QPointF(controlCenter.x(), controlCenter.y() - 1));
    }

    // 3. One-line title; the full identity remains available in the tooltip.
    QString title = QString::fromStdString(m_audioNode->getName());
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    
    titleFont.setPixelSize(11);

    painter->setFont(titleFont);
    painter->setPen(isMissing ? QColor(255, 138, 128) : (bypass ? QColor(160, 160, 165) : QColor(255, 255, 255)));

    const qreal titleRight = isSystemNode ? m_width - 8.0 : m_bypassRect.left() - 4.0;
    QRectF titleRect(11, 6, titleRight - 11.0, 18);
    painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, qRound(titleRect.width())));

    // Explicit category text is primary; color and icon provide faster scanning.
    QColor footerColor = headerColor;
    footerColor.setAlpha(bypass ? 10 : 24);
    QPainterPath cardClip;
    cardClip.addRoundedRect(QRectF(0.5, 0.5, m_width - 1.0, m_height - 1.0), 5.5, 5.5);
    painter->save();
    painter->setClipPath(cardClip);
    painter->setPen(Qt::NoPen);
    painter->setBrush(footerColor);
    painter->drawRect(QRectF(4, m_height - 20, m_width - 4, 20));
    painter->restore();

    QString channelLabel = isMissing ? "MISSING" : (m_isStereo ? "STEREO" : "MONO");
    QFont badgeFont = painter->font();
    badgeFont.setBold(true);
    badgeFont.setPixelSize(6.5);
    painter->setFont(badgeFont);
    QFontMetrics badgeMetrics(badgeFont);
    qreal badgeWidth = badgeMetrics.horizontalAdvance(channelLabel) + 8;
    QRectF badgeRect(m_width - badgeWidth - 6, m_height - 16, badgeWidth, 10);

    const QColor categoryTextColor = bypass ? QColor(125, 128, 135) : headerColor.lighter(125);
    drawEffectIcon(painter, iconType, QPointF(13, m_height - 10), categoryTextColor);
    QFont categoryFont = painter->font();
    categoryFont.setBold(true);
    categoryFont.setPixelSize(7);
    categoryFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    painter->setFont(categoryFont);
    painter->setPen(categoryTextColor);
    QRectF categoryRect(23, m_height - 17, badgeRect.left() - 27, 12);
    painter->drawText(categoryRect, Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetrics(categoryFont).elidedText(categoryLabel(iconType), Qt::ElideRight,
                                                            qRound(categoryRect.width())));

    painter->setFont(badgeFont);
    painter->setPen(Qt::NoPen);
    painter->setBrush(isMissing ? QColor(150, 62, 68) : QColor(57, 60, 68));
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

void NodeWidget::hoverEnterEvent(QGraphicsSceneHoverEvent* event) { m_hovered = true; update(); event->accept(); }
void NodeWidget::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) { m_hovered = false; update(); event->accept(); }

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
    m_dragStartItemPos = pos();
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
            const auto [fromRow, fromCol] = canvas->findNode(m_audioNode);
            const QPointF originCenter = m_dragStartItemPos + QPointF(m_width / 2.0, m_height / 2.0);
            const QPointF originDelta = event->scenePos() - originCenter;
            if (fromRow >= 0 && std::abs(originDelta.x()) <= m_width / 2.0
                && std::abs(originDelta.y()) <= m_height / 2.0 + 16.0) {
                canvas->setDragGap(fromRow, fromCol);
                event->accept();
                return;
            }

            PlusButtonWidget* closestPb = nullptr;
            qreal minDist = 1e9;
            QPointF mousePos = event->scenePos();

            for (auto* item : scene()->items()) {
                if (auto* pb = dynamic_cast<PlusButtonWidget*>(item)) {
                    QPointF pbPos = pb->scenePos();
                    qreal dx = mousePos.x() - pbPos.x();
                    qreal dy = mousePos.y() - pbPos.y();
                    qreal d = std::sqrt(dx * dx + dy * dy);
                    
                    if (std::abs(dy) < 80.0 && d < 96.0 && d < minDist) {
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
