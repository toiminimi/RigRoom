#pragma once
#include <QGraphicsItem>
#include <QPointF>
#include <QObject>

// A clickable '+' insertion point on the canvas.
// 'ghost'  = faded style for an empty slot.
// 'full'   = prominent center button when row is completely empty.
// 'insert' = thin marker between two blocks; adding here opens a new column
//            and shifts later blocks right.
class PlusButtonWidget : public QObject, public QGraphicsItem {
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    enum class Style { Full, Ghost, Insert };
    
    PlusButtonWidget(int row, int col, Style style = Style::Full);
    ~PlusButtonWidget() override = default;
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    int getRow() const { return m_row; }
    int getCol() const { return m_col; }
    Style getStyle() const { return m_style; }
    void setRoutingTarget(bool target) {
        if (m_routingTarget != target) {
            prepareGeometryChange();
            m_routingTarget = target;
            update();
        }
    }
    // NodeCanvas::InsertMode; 0 = fills the empty slot.
    void setInsertMode(int mode) { m_insert = mode; }
    int insertMode() const { return m_insert; }
    bool isInsert() const { return m_insert != 0; }
    // Where the marker belongs in the settled layout. Drops aim at this, so a
    // marker sliding in an insert preview cannot pull the drop target around.
    void setHomePos(const QPointF& pos) { m_homePos = pos; setPos(pos); }
    QPointF homePos() const { return m_homePos; }
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    
private:
    int m_row;
    int m_col;
    Style m_style;
    bool m_hovered = false;
    bool m_routingTarget = false;
    int m_insert = 0;
    QPointF m_homePos;
};
