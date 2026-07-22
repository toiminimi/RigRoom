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

    const int parentRow = m_canvas->getSplitParentRow(m_branchRow);
    const int plusIndex = target->getCol();

    int firstPluginCol = 999;
    int lastPluginCol = -1;
    for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
        if (m_canvas->getPluginAt(m_branchRow, c)) {
            firstPluginCol = std::min(firstPluginCol, c);
            lastPluginCol = std::max(lastPluginCol, c);
        }
    }

    bool isValid = true;
    QString invalidReason;
    if (m_isSplit) {
        int candidateSplitCol = plusIndex - 1;
        if (firstPluginCol < 999 && candidateSplitCol >= firstPluginCol) {
            isValid = false;
            invalidReason = "Cannot split after plugins on this path";
        }
    } else {
        int candidateMergeCol = (plusIndex >= NodeCanvas::NUM_COLS) ? -1 : plusIndex;
        if (lastPluginCol >= 0 && candidateMergeCol != -1 && candidateMergeCol <= lastPluginCol) {
            isValid = false;
            invalidReason = "Cannot merge before plugins on this path";
        }
    }

    if (!isValid) {
        m_previewLine->setPen(QPen(QColor(255, 60, 60), 2.5, Qt::DashLine, Qt::RoundCap));
        m_previewText->setBrush(QColor(255, 90, 90));
        m_previewText->setText("❌ " + invalidReason);
    } else {
        m_previewLine->setPen(QPen(QColor(255, 200, 50), 2, Qt::DashLine, Qt::RoundCap));
        m_previewText->setBrush(QColor(255, 220, 100));
        QString text;
        if (m_isSplit) {
            auto prevPlug = (plusIndex > 0 && plusIndex - 1 < NodeCanvas::NUM_COLS) ? m_canvas->getPluginAt(parentRow, plusIndex - 1) : nullptr;
            text = prevPlug ? ("Split after " + QString::fromStdString(prevPlug->getName())) : (plusIndex == 0 ? "Split from System Input" : "Split point");
        } else {
            auto nextPlug = (plusIndex >= 0 && plusIndex < NodeCanvas::NUM_COLS) ? m_canvas->getPluginAt(parentRow, plusIndex) : nullptr;
            text = nextPlug ? ("Merge before " + QString::fromStdString(nextPlug->getName())) : (plusIndex >= NodeCanvas::NUM_COLS ? "Merge to System Output" : "Merge point");
        }
        m_previewText->setText(text);
    }
    m_previewText->setPos(10, targetPoint.y() / 2.0 - 10);
    m_previewText->show();
}

void RoutingHandleItem::clearPreview() {
    if (m_previewTarget) m_previewTarget->setRoutingTarget(false);
    m_previewTarget = nullptr;
    m_dragInvalid = false;
    m_previewLine->hide();
    m_previewText->hide();
    update();
}

QRectF RoutingHandleItem::boundingRect() const {
    return QRectF(-23, -17, 46, 34);
}

void RoutingHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);

    const QColor accent = m_dragInvalid ? QColor(255, 60, 60)
        : (m_dragging ? QColor(255, 200, 50)
        : (isSelected() || m_hovered ? QColor(53, 199, 255) : QColor(75, 85, 99)));
    painter->setPen(QPen(accent, isSelected() || m_hovered || m_dragging || m_dragInvalid ? 2.0 : 1.2));
    painter->setBrush(m_dragInvalid ? QColor(45, 18, 18) : QColor(29, 31, 37));
    painter->drawRoundedRect(QRectF(-21, -15, 42, 30), 6, 6);

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_dragInvalid ? QColor(220, 38, 38)
        : (m_isSplit ? QColor(37, 99, 235) : QColor(126, 70, 180)));
    painter->drawRoundedRect(QRectF(-21, -15, 42, 13), 6, 6);
    painter->drawRect(QRectF(-21, -8, 42, 6));

    QFont labelFont = painter->font();
    labelFont.setPixelSize(8);
    labelFont.setBold(true);
    painter->setFont(labelFont);
    painter->setPen(QColor(245, 247, 250));
    painter->drawText(QRectF(-21, -15, 42, 13), Qt::AlignCenter, m_isSplit ? "SPLIT" : "MIX");

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

    painter->drawText(QRectF(-21, -2, 42, 16), Qt::AlignCenter, detail);

    painter->setPen(QPen(accent, 1.4));
    painter->setBrush(QColor(14, 16, 20));
    painter->drawEllipse(QRectF(-25, -4, 8, 8));
    painter->drawEllipse(QRectF(17, -4, 8, 8));
    const qreal branchY = m_branchRow == 0 ? -19.0 : 11.0;
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
        int parentRow = m_canvas->getSplitParentRow(m_branchRow);

        int plusIndex = 0;
        qreal minDist = 999999.0;
        for (int k = 0; k <= NodeCanvas::NUM_COLS; ++k) {
            qreal dist = std::abs(currentMouseX - m_canvas->getGapX(k));
            if (dist < minDist) {
                minDist = dist;
                plusIndex = k;
            }
        }

        qreal snapX = m_canvas->getGapX(plusIndex);

        // Keep handle on its own Y coordinate during drag
        setPos(snapX, pos().y());

        // Find parent Y for dashed connection preview
        qreal parentY = m_canvas->getRowCenterY(parentRow);

        // Draw vertical connection preview line from handle to parent row
        QPointF targetPoint = mapFromScene(QPointF(snapX, parentY));
        QPainterPath previewPath;
        previewPath.moveTo(0, 0);
        previewPath.lineTo(targetPoint);
        m_previewLine->setPath(previewPath);
        m_previewLine->show();

        int firstPluginCol = 999;
        int lastPluginCol = -1;
        for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
            if (m_canvas->getPluginAt(m_branchRow, c)) {
                firstPluginCol = std::min(firstPluginCol, c);
                lastPluginCol = std::max(lastPluginCol, c);
            }
        }

        int candidateSplitCol = plusIndex - 1;
        int candidateMergeCol = (plusIndex >= NodeCanvas::NUM_COLS) ? -1 : plusIndex;

        int candidateSplitGap = plusIndex;
        int candidateMergeGap = plusIndex;

        int parentSplitGap = (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && m_canvas->getSplitCol(parentRow) >= 0)
            ? (m_canvas->getSplitCol(parentRow) + 1) : 0;
        int parentMergeGap = (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && m_canvas->getMergeCol(parentRow) >= 0)
            ? m_canvas->getMergeCol(parentRow) : NodeCanvas::NUM_COLS;

        int ownSplitGap = (m_canvas->getSplitCol(m_branchRow) >= 0) ? (m_canvas->getSplitCol(m_branchRow) + 1) : 0;
        int ownMergeGap = (m_canvas->getMergeCol(m_branchRow) >= 0) ? m_canvas->getMergeCol(m_branchRow) : NodeCanvas::NUM_COLS;

        bool isValid = true;
        QString invalidReason;
        if (m_isSplit) {
            if (firstPluginCol < 999 && candidateSplitCol >= firstPluginCol) {
                isValid = false;
                invalidReason = "Cannot split after plugins on this path";
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateSplitGap < parentSplitGap) {
                isValid = false;
                invalidReason = "Cannot split before parent branch split point";
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateSplitGap >= parentMergeGap) {
                isValid = false;
                invalidReason = "Cannot split at or after parent branch merge point";
            } else if (m_canvas->getMergeCol(m_branchRow) >= 0 && candidateSplitGap >= ownMergeGap) {
                isValid = false;
                invalidReason = "Cannot split at or after branch merge point";
            }
        } else {
            if (lastPluginCol >= 0 && candidateMergeCol != -1 && candidateMergeCol <= lastPluginCol) {
                isValid = false;
                invalidReason = "Cannot merge before plugins on this path";
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateMergeGap > parentMergeGap) {
                isValid = false;
                invalidReason = "Cannot merge after parent branch merge point";
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateMergeGap <= parentSplitGap) {
                isValid = false;
                invalidReason = "Cannot merge at or before parent branch split point";
            } else if (m_canvas->getSplitCol(m_branchRow) >= 0 && candidateMergeGap <= ownSplitGap) {
                isValid = false;
                invalidReason = "Cannot merge at or before branch split point";
            }
        }

        if (!isValid) {
            m_dragInvalid = true;
            m_previewLine->setPen(QPen(QColor(255, 60, 60), 2.5, Qt::DashLine, Qt::RoundCap));
            m_previewText->setBrush(QColor(255, 90, 90));
            m_previewText->setText("❌ " + invalidReason);
        } else {
            m_dragInvalid = false;
            m_previewLine->setPen(QPen(QColor(255, 200, 50), 2, Qt::DashLine, Qt::RoundCap));
            m_previewText->setBrush(QColor(255, 220, 100));
            QString text;
            if (m_isSplit) {
                auto prevPlug = (plusIndex > 0 && plusIndex - 1 < NodeCanvas::NUM_COLS) ? m_canvas->getPluginAt(parentRow, plusIndex - 1) : nullptr;
                text = prevPlug ? ("Split after " + QString::fromStdString(prevPlug->getName())) : (plusIndex == 0 ? "Split from System Input" : "Split point");
            } else {
                auto nextPlug = (plusIndex >= 0 && plusIndex < NodeCanvas::NUM_COLS) ? m_canvas->getPluginAt(parentRow, plusIndex) : nullptr;
                text = nextPlug ? ("Merge before " + QString::fromStdString(nextPlug->getName())) : (plusIndex >= NodeCanvas::NUM_COLS ? "Merge to System Output" : "Merge point");
            }
            m_previewText->setText(text);
        }
        update();
        qreal textW = m_previewText->boundingRect().width();
        m_previewText->setPos(-textW / 2.0, targetPoint.y() / 2.0 - 10);
        m_previewText->show();
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
        int targetParentRow = m_canvas->getSplitParentRow(m_branchRow);

        int plusIndex = 0;
        qreal minDist = 999999.0;
        for (int k = 0; k <= NodeCanvas::NUM_COLS; ++k) {
            qreal dist = std::abs(currentMouseX - m_canvas->getGapX(k));
            if (dist < minDist) {
                minDist = dist;
                plusIndex = k;
            }
        }

        int targetColumn = m_isSplit ? (plusIndex - 1) : ((plusIndex >= NodeCanvas::NUM_COLS) ? -1 : plusIndex);

        int firstPluginCol = 999;
        int lastPluginCol = -1;
        for (int c = 0; c < NodeCanvas::NUM_COLS; ++c) {
            if (m_canvas->getPluginAt(m_branchRow, c)) {
                firstPluginCol = std::min(firstPluginCol, c);
                lastPluginCol = std::max(lastPluginCol, c);
            }
        }

        int parentRow = m_canvas->getSplitParentRow(m_branchRow);
        int candidateSplitCol = plusIndex - 1;
        int candidateMergeCol = (plusIndex >= NodeCanvas::NUM_COLS) ? -1 : plusIndex;

        int candidateSplitGap = plusIndex;
        int candidateMergeGap = plusIndex;

        int parentSplitGap = (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && m_canvas->getSplitCol(parentRow) >= 0)
            ? (m_canvas->getSplitCol(parentRow) + 1) : 0;
        int parentMergeGap = (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && m_canvas->getMergeCol(parentRow) >= 0)
            ? m_canvas->getMergeCol(parentRow) : NodeCanvas::NUM_COLS;

        int ownSplitGap = (m_canvas->getSplitCol(m_branchRow) >= 0) ? (m_canvas->getSplitCol(m_branchRow) + 1) : 0;
        int ownMergeGap = (m_canvas->getMergeCol(m_branchRow) >= 0) ? m_canvas->getMergeCol(m_branchRow) : NodeCanvas::NUM_COLS;

        bool isValid = true;
        if (m_isSplit) {
            if (firstPluginCol < 999 && targetColumn >= firstPluginCol) {
                isValid = false;
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateSplitGap < parentSplitGap) {
                isValid = false;
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateSplitGap >= parentMergeGap) {
                isValid = false;
            } else if (m_canvas->getMergeCol(m_branchRow) >= 0 && candidateSplitGap >= ownMergeGap) {
                isValid = false;
            }
        } else {
            if (lastPluginCol >= 0 && targetColumn != -1 && targetColumn <= lastPluginCol) {
                isValid = false;
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateMergeGap > parentMergeGap) {
                isValid = false;
            } else if (parentRow >= 0 && parentRow != NodeCanvas::MAIN_ROW && candidateMergeGap <= parentSplitGap) {
                isValid = false;
            } else if (m_canvas->getSplitCol(m_branchRow) >= 0 && candidateMergeGap <= ownSplitGap) {
                isValid = false;
            }
        }

        clearPreview();
        if (wasClick) m_canvas->selectRoutingNode(m_branchRow, m_isSplit);
        if (isValid && targetColumn != -2 && !wasClick && targetParentRow >= 0) {
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
        } else if (!isValid && !wasClick) {
            m_canvas->updateLayout();
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
