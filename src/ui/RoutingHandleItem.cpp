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
#include <QLineF>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <iostream>

RoutingHandleItem::RoutingHandleItem(NodeCanvas* canvas, int branchRow, bool isSplit)
    : m_canvas(canvas), m_branchRow(branchRow), m_isSplit(isSplit) {
    setAcceptHoverEvents(true);
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
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
    const int parentRow = m_canvas->getSplitParentRow(m_branchRow);
    for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
        if (m_canvas->getPluginAt(parentRow, c)) occupied.push_back(c);
    }
    const int plusIndex = target->getCol();
    QString text;
    if (m_isSplit) {
        text = plusIndex == 0
            ? "Split from System Input"
            : "Split after " + QString::fromStdString(m_canvas->getPluginAt(parentRow, occupied[plusIndex - 1])->getName());
    } else {
        text = plusIndex >= static_cast<int>(occupied.size())
            ? "Merge to System Output"
            : "Merge before " + QString::fromStdString(m_canvas->getPluginAt(parentRow, occupied[plusIndex])->getName());
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
    return QRectF(-30, -19, 60, 38);
}

void RoutingHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);

    const QColor accent = m_dragging ? QColor(255, 200, 50)
        : (isSelected() || m_hovered ? QColor(53, 199, 255) : QColor(75, 85, 99));
    painter->setPen(QPen(accent, isSelected() || m_hovered || m_dragging ? 2.0 : 1.2));
    painter->setBrush(QColor(29, 31, 37));
    painter->drawRoundedRect(QRectF(-28, -17, 56, 34), 7, 7);

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_isSplit ? QColor(37, 99, 235) : QColor(126, 70, 180));
    painter->drawRoundedRect(QRectF(-28, -17, 56, 14), 7, 7);
    painter->drawRect(QRectF(-28, -10, 56, 7));

    QFont labelFont = painter->font();
    labelFont.setPixelSize(9);
    labelFont.setBold(true);
    painter->setFont(labelFont);
    painter->setPen(QColor(245, 247, 250));
    painter->drawText(QRectF(-26, -17, 52, 14), Qt::AlignCenter, m_isSplit ? "SPLIT" : "MIX");

    QFont detailFont = painter->font();
    detailFont.setPixelSize(8);
    detailFont.setBold(false);
    painter->setFont(detailFont);
    painter->setPen(QColor(180, 185, 195));
    const QString pathName = m_canvas->getBranchName(m_branchRow);
    QString pathLetter = pathName;
    if (pathLetter.startsWith("Path ")) {
        pathLetter = pathLetter.mid(5);
    }

    QString detail;
    if (m_isSplit) {
        detail = QString("%1 · %2").arg(pathLetter).arg(
            m_canvas->getSplitMode(m_branchRow) == GridRow::SplitMode::AB ? "A/B" : "COPY");
    } else {
        const float level = m_canvas->getMix(m_branchRow);
        QString lvlStr = level <= 0.0f ? "-inf" : QString("%1dB").arg(qRound(20.0f * std::log10(level)));
        const float pan = m_canvas->getPan(m_branchRow);
        if (std::abs(pan) < 0.05f) {
            detail = QString("%1 · %2").arg(pathLetter).arg(lvlStr);
        } else {
            QString panStr = pan < 0.0f ? QString("L%1").arg(qRound(-pan * 100.0f))
                                        : QString("R%1").arg(qRound(pan * 100.0f));
            detail = QString("%1 · %2 %3").arg(pathLetter).arg(lvlStr).arg(panStr);
        }
    }

    // Set tooltip dynamically
    QString tooltipText;
    if (m_isSplit) {
        tooltipText = QString("Split Section %1\n").arg(pathLetter);
        if (m_canvas->getSplitMode(m_branchRow) == GridRow::SplitMode::Copy) {
            tooltipText += "Mode: Copy (Parallel)";
        } else {
            float pos = m_canvas->getSplitPosition(m_branchRow);
            tooltipText += QString("Mode: A/B Split (%1)").arg(
                pos == 0.0f ? "A = B" : (pos < 0.0f ? QString("%1% to A").arg(qRound(-pos * 100.0f))
                                                    : QString("%1% to B").arg(qRound(pos * 100.0f)))
            );
        }
    } else {
        tooltipText = QString("Mixer Section %1\n").arg(pathLetter);
        float level = m_canvas->getMix(m_branchRow);
        tooltipText += QString("Level: %1 dB\n").arg(level <= 0.0f ? "-inf" : QString::number(20.0f * std::log10(level), 'f', 1));
        float pan = m_canvas->getPan(m_branchRow);
        tooltipText += QString("Pan/Balance: %1").arg(pan == 0.0f ? "Center" : (pan < 0.0f ? QString("%1% Left").arg(qRound(-pan * 100.0f))
                                                                                       : QString("%1% Right").arg(qRound(pan * 100.0f))));
    }
    const_cast<RoutingHandleItem*>(this)->setToolTip(tooltipText);

    painter->drawText(QRectF(-26, -2, 52, 16), Qt::AlignCenter, detail);

    painter->setPen(QPen(accent, 1.4));
    painter->setBrush(QColor(14, 16, 20));
    painter->drawEllipse(QRectF(-32, -4, 8, 8));
    painter->drawEllipse(QRectF(24, -4, 8, 8));
    const qreal branchY = m_branchRow == 0 ? -21.0 : 13.0;
    painter->drawEllipse(QRectF(-4, branchY, 8, 8));
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
        qreal currentMouseY = event->scenePos().y();

        auto isValidParent = [&](int parentRow) -> bool {
            if (parentRow == m_branchRow) return false;
            if (parentRow == NodeCanvas::MAIN_ROW) return true;
            if (parentRow >= 0 && parentRow < NodeCanvas::NUM_ROWS) {
                if (!m_canvas->hasSplitSection(parentRow)) return false;
                if (m_canvas->getSplitParentRow(parentRow) == m_branchRow) return false;
                int p = parentRow;
                while (p != NodeCanvas::MAIN_ROW) {
                    p = m_canvas->getSplitParentRow(p);
                    if (p == m_branchRow) return false;
                }
                return true;
            }
            return false;
        };

        PlusButtonWidget* nearestPlus = nullptr;
        qreal minDistance = 999999.0;
        for (auto* item : scene()->items()) {
            if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                if (isValidParent(plus->getRow())) {
                    QPointF plusPos = plus->scenePos();
                    qreal dx = plusPos.x() - currentMouseX;
                    qreal dy = plusPos.y() - currentMouseY;
                    qreal dist = std::sqrt(dx * dx + dy * dy);
                    if (dist < minDistance) {
                        minDistance = dist;
                        nearestPlus = plus;
                    }
                }
            }
        }

        if (nearestPlus) {
            qreal branchY = pos().y();
            for (auto* item : scene()->items()) {
                if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                    if (plus->getRow() == m_branchRow) {
                        branchY = plus->scenePos().y();
                        break;
                    }
                }
            }
            qreal parentY = nearestPlus->scenePos().y();
            qreal snapY = (parentY + branchY) / 2.0;

            setPos(nearestPlus->scenePos().x(), snapY);
            updatePreview(nearestPlus);
        } else {
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

        const bool wasClick = QLineF(m_dragStartPos, event->scenePos()).length() < 4.0;
        qreal currentMouseX = event->scenePos().x();
        qreal currentMouseY = event->scenePos().y();

        auto isValidParent = [&](int parentRow) -> bool {
            if (parentRow == m_branchRow) return false;
            if (parentRow == NodeCanvas::MAIN_ROW) return true;
            if (parentRow >= 0 && parentRow < NodeCanvas::NUM_ROWS) {
                if (!m_canvas->hasSplitSection(parentRow)) return false;
                if (m_canvas->getSplitParentRow(parentRow) == m_branchRow) return false;
                int p = parentRow;
                while (p != NodeCanvas::MAIN_ROW) {
                    p = m_canvas->getSplitParentRow(p);
                    if (p == m_branchRow) return false;
                }
                return true;
            }
            return false;
        };

        PlusButtonWidget* nearestPlus = nullptr;
        qreal minDistance = 999999.0;
        for (auto* item : scene()->items()) {
            if (auto* plus = dynamic_cast<PlusButtonWidget*>(item)) {
                if (isValidParent(plus->getRow())) {
                    QPointF plusPos = plus->scenePos();
                    qreal dx = plusPos.x() - currentMouseX;
                    qreal dy = plusPos.y() - currentMouseY;
                    qreal dist = std::sqrt(dx * dx + dy * dy);
                    if (dist < minDistance) {
                        minDistance = dist;
                        nearestPlus = plus;
                    }
                }
            }
        }

        int targetColumn = -2;
        int targetParentRow = -1;
        if (nearestPlus) {
            targetParentRow = nearestPlus->getRow();
            int plusIndex = nearestPlus->getCol();

            std::vector<int> occupied;
            for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
                if (m_canvas->getPluginAt(targetParentRow, c)) occupied.push_back(c);
            }

            if (m_isSplit) {
                targetColumn = -1;
                if (plusIndex > 0 && plusIndex - 1 < (int)occupied.size()) {
                    targetColumn = occupied[plusIndex - 1];
                }
            } else {
                targetColumn = -1;
                if (plusIndex < (int)occupied.size()) {
                    targetColumn = occupied[plusIndex];
                }
            }
        }

        clearPreview();
        if (wasClick) m_canvas->selectRoutingNode(m_branchRow, m_isSplit);
        if (targetColumn != -2 && !wasClick && targetParentRow >= 0) {
            NodeCanvas* canvas = m_canvas;
            const int row = m_branchRow;
            const bool split = m_isSplit;
            QTimer::singleShot(0, canvas, [canvas, row, split, targetColumn, targetParentRow] {
                canvas->beginRoutingUpdate();
                canvas->setSplitParentRow(row, targetParentRow);
                if (split) canvas->setSplitCol(row, targetColumn);
                else canvas->setMergeCol(row, targetColumn);
                canvas->endRoutingUpdate();
                canvas->selectRoutingNode(row, split);
            });
        }
    }
    event->accept();
}

void RoutingHandleItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    m_hovered = true;
    setCursor(Qt::OpenHandCursor);
    setToolTip(m_isSplit
        ? "Split point. Drag horizontally to choose where this path receives signal."
        : "Mixer point. Drag horizontally to choose where this path returns.");
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

void RoutingHandleItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    QAction* deleteAction = menu.addAction("Delete Split Section");

    deleteAction->setEnabled(true);

    QAction* selected = menu.exec(event->screenPos());
    if (selected == deleteAction) {
        auto* canvas = m_canvas;
        int branchRow = m_branchRow;
        bool isSplit = m_isSplit;
        QTimer::singleShot(0, canvas, [canvas, branchRow, isSplit]() {
            canvas->removeSplitSection(branchRow);
            canvas->selectRoutingNode(-1, isSplit);
        });
    }
    event->accept();
}
