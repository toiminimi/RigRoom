#pragma once
#include <QGraphicsItem>
#include <QObject>

// A clickable '+' insertion point on the canvas.
// 'ghost' = faded style used when slots exist nearby.
// 'full'  = prominent center button when row is completely empty.
class PlusButtonWidget : public QObject, public QGraphicsItem {
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    enum class Style { Full, Ghost };
    
    PlusButtonWidget(int row, int col, Style style = Style::Full);
    ~PlusButtonWidget() override = default;
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    int getRow() const { return m_row; }
    int getCol() const { return m_col; }
    Style getStyle() const { return m_style; }
    void setRoutingTarget(bool target) { m_routingTarget = target; update(); }
    
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
};
