#include "PlusButtonWidget.h"
#include "NodeCanvas.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QCursor>

PlusButtonWidget::PlusButtonWidget(int row, int col, Style style)
    : m_row(row), m_col(col), m_style(style), m_insert(style == Style::Insert ? 1 : 0) {
    setAcceptHoverEvents(true);
    setZValue(10);
    if (style == Style::Insert) setToolTip("Insert a block here (later blocks move right)");
}

QRectF PlusButtonWidget::boundingRect() const {
    if (m_style == Style::Insert && !m_routingTarget) {
        // The narrow gap between two cards; at a junction two markers share it
        // and reach a little over the card edges.
        return m_insert >= 2 ? QRectF(-7, -22, 14, 44) : QRectF(-8, -22, 16, 44);
    }
    qreal r = 16.0; // generous invisible hit target for every logical empty slot
    return QRectF(-r, -r, r * 2, r * 2);
}

void PlusButtonWidget::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    if (m_routingTarget) {
        painter->setPen(QPen(QColor(160, 144, 206), 2.0));
        painter->setBrush(QColor(120, 104, 175, 50));
        painter->drawEllipse(QRectF(-13, -13, 26, 26));
        painter->setPen(QPen(QColor(196, 184, 232), 2.0, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(-7, 0, 7, 0);
        painter->drawLine(0, -7, 0, 7);
        return;
    }
    
    if (m_style == Style::Insert) {
        // A quiet seam between two cards; on hover it becomes an insert handle.
        if (!m_hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(142, 148, 160, 120));
            painter->drawRoundedRect(QRectF(-1, -10, 2, 20), 1, 1);
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 176, 255, 150));
            painter->drawRoundedRect(QRectF(-1.5, -20, 3, 40), 1.5, 1.5);
            painter->setPen(QPen(QColor(143, 166, 218), 1.5));
            painter->setBrush(QColor(68, 79, 105, 230));
            painter->drawEllipse(QRectF(-7, -7, 14, 14));
            painter->setPen(QPen(QColor(235, 240, 250), 1.6, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(-3.5, 0), QPointF(3.5, 0));
            painter->drawLine(QPointF(0, -3.5), QPointF(0, 3.5));
        }
    } else if (m_style == Style::Full) {
        // Bright, inviting full button
        QColor bg     = m_hovered ? QColor(0, 176, 255, 70)  : QColor(30, 32, 40, 220);
        QColor border = m_hovered ? QColor(0, 229, 255)      : QColor(0, 176, 255, 200);
        QColor cross  = m_hovered ? QColor(255, 255, 255)    : QColor(0, 220, 255);
        
        painter->setPen(QPen(border, 2.0));
        painter->setBrush(bg);
        painter->drawEllipse(QRectF(-16, -16, 32, 32));
        
        painter->setPen(QPen(cross, 2.4, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(-7, 0, 7, 0);
        painter->drawLine(0, -7, 0, 7);
    } else {
        // Ghost targets stay discoverable without competing with signal cards.
        if (!m_hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(142, 148, 160, 70));
            painter->drawEllipse(QRectF(-2, -2, 4, 4));
        } else {
            painter->setPen(QPen(QColor(143, 166, 218), 1.5));
            painter->setBrush(QColor(68, 79, 105, 210));
            painter->drawEllipse(QRectF(-10, -10, 20, 20));
            painter->setPen(QPen(QColor(225, 230, 240), 1.8, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(-4, 0, 4, 0);
            painter->drawLine(0, -4, 0, 4);
        }
    }
}

void PlusButtonWidget::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!scene() || scene()->views().isEmpty()) { event->accept(); return; }
    auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
    if (canvas) {
        canvas->onPlusButtonClicked(m_row, m_col, QCursor::pos(), m_insert);
    }
    event->accept();
}

void PlusButtonWidget::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    m_hovered = true;
    update();
}

void PlusButtonWidget::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    m_hovered = false;
    update();
}
