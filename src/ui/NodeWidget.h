#pragma once
#include <QGraphicsItem>
#include <vector>
#include <memory>
#include "../audio/AudioNode.h"

class PortWidget;

class NodeWidget : public QGraphicsItem {
public:
    NodeWidget(std::shared_ptr<AudioNode> audioNode);
    ~NodeWidget() override = default;
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    std::shared_ptr<AudioNode> getAudioNode() const { return m_audioNode; }
    
    int width() const { return m_width; }
    int height() const { return m_height; }
    
    std::vector<PortWidget*>& getInputPorts() { return m_inputPorts; }
    std::vector<PortWidget*>& getOutputPorts() { return m_outputPorts; }

    void toggleBypass();
    void setChannelMode(bool stereo) { m_isStereo = stereo; update(); }

    bool isDragging() const { return m_dragging; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    std::shared_ptr<AudioNode> m_audioNode;
    std::vector<PortWidget*> m_inputPorts;
    std::vector<PortWidget*> m_outputPorts;
    
    int m_width = 120;
    int m_height = 54;
    int m_gridSize = 20;
    bool m_isStereo = false;
    
    QRectF m_bypassRect;

    // Drag-and-drop
    bool m_dragging = false;
    QPointF m_dragStartPos;
};
