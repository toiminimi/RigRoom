#pragma once
#include <QGraphicsItem>
#include "../audio/AudioNode.h"

class NodeWidget;

class PortWidget : public QGraphicsItem {
public:
    PortWidget(NodeWidget* parentNode, int portIdx, bool isInput, bool isStereo);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    bool isInput() const { return m_isInput; }
    bool isStereo() const { return m_isStereo; }
    int getPortIndex() const { return m_portIdx; }
    NodeWidget* getParentNode() const { return m_parentNode; }
    
    QPointF getAnchorPos() const; // Scene-space position for connecting wires

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    NodeWidget* m_parentNode;
    int m_portIdx;
    bool m_isInput;
    bool m_isStereo;
};
