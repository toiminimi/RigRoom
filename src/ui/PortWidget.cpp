#include "PortWidget.h"
#include "NodeWidget.h"
#include "NodeCanvas.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QDrag>
#include <QMimeData>
#include <QApplication>

PortWidget::PortWidget(NodeWidget* parentNode, int portIdx, bool isInput, bool isStereo)
    : QGraphicsItem(parentNode), m_parentNode(parentNode), m_portIdx(portIdx), m_isInput(isInput), m_isStereo(isStereo) {
    setAcceptHoverEvents(true);
}

QRectF PortWidget::boundingRect() const {
    return QRectF(-8, -8, 16, 16);
}

void PortWidget::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    painter->setPen(QPen(QColor(15, 16, 19), 1.2));
    painter->setBrush(m_isStereo ? QColor(119, 132, 158) : QColor(104, 108, 116));
    painter->drawEllipse(QRectF(-3.5, -3.5, 7, 7));
}

QPointF PortWidget::getAnchorPos() const {
    return mapToScene(QPointF(0, 0));
}

void PortWidget::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    event->ignore();
}
