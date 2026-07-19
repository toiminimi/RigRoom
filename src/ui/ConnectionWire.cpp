#include "ConnectionWire.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

ConnectionWire::ConnectionWire(PortWidget* srcPort, PortWidget* dstPort)
    : m_srcPort(srcPort), m_dstPort(dstPort) {
    setZValue(-1); // Make sure wires draw behind nodes
    setFlags(QGraphicsItem::ItemIsSelectable);
    updatePath();
}

void ConnectionWire::updatePath() {
    QPointF p1 = m_srcPort->getAnchorPos();
    QPointF p2 = m_dstPort->getAnchorPos();
    
    QPainterPath path;
    path.moveTo(p1);
    
    // Smooth Bezier controls
    qreal dx = std::max(20.0, std::abs(p2.x() - p1.x()) * 0.5);
    QPointF c1 = p1 + QPointF(dx, 0);
    QPointF c2 = p2 - QPointF(dx, 0);
    
    path.cubicTo(c1, c2, p2);
    setPath(path);
}

void ConnectionWire::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    bool isSrcStereo = m_srcPort->isStereo();
    bool isDstStereo = m_dstPort->isStereo();
    
    QPainterPath curPath = path();
    
    // Draw selection glow halo
    if (isSelected()) {
        painter->setPen(QPen(QColor(0, 176, 255, 120), 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
    }
    
    if (isSrcStereo && isDstStereo) {
        // STEREO TO STEREO: Double-conductor line
        // 1. Dark backing
        painter->setPen(QPen(QColor(18, 18, 18), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
        
        // 2. Green core
        painter->setPen(QPen(QColor(0, 230, 118), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
        
        // 3. Center splitter
        painter->setPen(QPen(QColor(18, 18, 18), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
    } 
    else if (!isSrcStereo && !isDstStereo) {
        // MONO TO MONO: Silver single line
        // 1. Shadow/backing
        painter->setPen(QPen(QColor(18, 18, 18), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
        
        // 2. Silver core
        painter->setPen(QPen(QColor(224, 224, 224), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
    } 
    else {
        // MIXED MONO/STEREO: Split or Merge visualization
        // Draw a neat glowing orange/cyan transition gradient path
        QColor startColor = isSrcStereo ? QColor(0, 230, 118) : QColor(224, 224, 224);
        QColor endColor = isDstStereo ? QColor(0, 230, 118) : QColor(224, 224, 224);
        
        QPointF p1 = m_srcPort->getAnchorPos();
        QPointF p2 = m_dstPort->getAnchorPos();
        QLinearGradient grad(p1, p2);
        grad.setColorAt(0.0, startColor);
        grad.setColorAt(1.0, endColor);
        
        painter->setPen(QPen(QColor(18, 18, 18), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
        
        painter->setPen(QPen(QBrush(grad), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(curPath);
    }
}
