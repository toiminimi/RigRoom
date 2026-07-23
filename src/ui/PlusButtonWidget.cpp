#include "PlusButtonWidget.h"
#include "NodeCanvas.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QCursor>

PlusButtonWidget::PlusButtonWidget(int row, int col, Style style)
    : m_row(row), m_col(col), m_style(style) {
    setAcceptHoverEvents(true);
    setZValue(10);
}

QRectF PlusButtonWidget::boundingRect() const {
    qreal r = 20.0;
    return QRectF(-r, -r, r * 2, r * 2);
}

void PlusButtonWidget::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    if (m_routingTarget) {
        painter->setPen(QPen(QColor(255, 200, 50), 2.5));
        painter->setBrush(QColor(255, 200, 50, 45));
        painter->drawEllipse(QRectF(-16, -16, 32, 32));
        painter->setPen(QPen(QColor(255, 220, 90), 2.2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(-7, 0, 7, 0);
        painter->drawLine(0, -7, 0, 7);
        return;
    }
    
    if (m_style == Style::Full) {
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
        // Prominent slot button
        QColor bg     = m_hovered ? QColor(0, 176, 255, 60)  : QColor(26, 28, 36, 220);
        QColor border  = m_hovered ? QColor(0, 229, 255)      : QColor(0, 176, 255, 170);
        QColor cross   = m_hovered ? QColor(255, 255, 255)    : QColor(180, 230, 255, 230);
        
        painter->setPen(QPen(border, 1.8));
        painter->setBrush(bg);
        painter->drawEllipse(QRectF(-14, -14, 28, 28));
        
        painter->setPen(QPen(cross, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(-6, 0, 6, 0);
        painter->drawLine(0, -6, 0, 6);
    }
}

void PlusButtonWidget::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!scene() || scene()->views().isEmpty()) { event->accept(); return; }
    auto* canvas = dynamic_cast<NodeCanvas*>(scene()->views().first());
    if (canvas) {
        canvas->onPlusButtonClicked(m_row, m_col, QCursor::pos(), m_isSecondOfCol);
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
