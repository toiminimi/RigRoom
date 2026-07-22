#pragma once
#include <QGraphicsItem>

class NodeCanvas;
class PlusButtonWidget;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;

class RoutingHandleItem : public QGraphicsItem {
public:
    RoutingHandleItem(NodeCanvas* canvas, int branchRow, bool isSplit);
    ~RoutingHandleItem() override = default;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    int branchRow() const { return m_branchRow; }
    bool isSplitHandle() const { return m_isSplit; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    void updatePreview(PlusButtonWidget* target);
    void clearPreview();

    NodeCanvas* m_canvas;
    int m_branchRow; // 0 or 2
    bool m_isSplit;  // true for split, false for merge
    bool m_hovered = false;
    bool m_dragging = false;
    bool m_dragInvalid = false;
    QPointF m_dragStartPos;
    PlusButtonWidget* m_previewTarget = nullptr;
    QGraphicsPathItem* m_previewLine = nullptr;
    QGraphicsSimpleTextItem* m_previewText = nullptr;
};
