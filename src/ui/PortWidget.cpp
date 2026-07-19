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
    
    painter->setPen(QPen(QColor(12, 12, 14), 1.5));
    painter->setBrush(QColor(140, 145, 150));
    painter->drawEllipse(QRectF(-4, -4, 8, 8));
}

QPointF PortWidget::getAnchorPos() const {
    return mapToScene(QPointF(0, 0));
}

void PortWidget::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    event->ignore();
}
