#pragma once
#include <QGraphicsPathItem>
#include "PortWidget.h"

class ConnectionWire : public QGraphicsPathItem {
public:
    ConnectionWire(PortWidget* srcPort, PortWidget* dstPort);
    ~ConnectionWire() override = default;
    
    void updatePath();
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    PortWidget* getSrcPort() const { return m_srcPort; }
    PortWidget* getDstPort() const { return m_dstPort; }
    
private:
    PortWidget* m_srcPort;
    PortWidget* m_dstPort;
};
