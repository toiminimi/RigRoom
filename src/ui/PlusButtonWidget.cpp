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
    qreal r = (m_style == Style::Full) ? 18.0 : 13.0;
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
        QColor bg    = m_hovered ? QColor(0, 176, 255, 60)  : QColor(30, 30, 35, 200);
        QColor border = m_hovered ? QColor(0, 200, 255)      : QColor(80, 80, 90);
        QColor cross  = m_hovered ? QColor(0, 220, 255)      : QColor(160, 160, 170);
        
        painter->setPen(QPen(border, 1.8));
        painter->setBrush(bg);
        painter->drawEllipse(QRectF(-16, -16, 32, 32));
        
        painter->setPen(QPen(cross, 2.2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(-7, 0, 7, 0);
        painter->drawLine(0, -7, 0, 7);
    } else {
        // Subtle ghost button — appears only on hover of the row track
        QColor bg     = m_hovered ? QColor(0, 176, 255, 30)  : QColor(0, 0, 0, 0);
        QColor border  = m_hovered ? QColor(0, 176, 255, 180) : QColor(70, 70, 80, 120);
        QColor cross   = m_hovered ? QColor(0, 200, 255, 220) : QColor(100, 100, 110, 100);
        
        painter->setPen(QPen(border, 1.2));
        painter->setBrush(bg);
        painter->drawEllipse(QRectF(-11, -11, 22, 22));
        
        painter->setPen(QPen(cross, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(-5, 0, 5, 0);
        painter->drawLine(0, -5, 0, 5);
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
