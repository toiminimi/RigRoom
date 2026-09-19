#pragma once
#include <QAbstractButton>
#include <QColor>

// A switch drawn like a footswitch scribble strip: a large key ("A", "2")
// with the name underneath. Preset slots and scenes both use it, so they read
// as the same kind of control.
class FootswitchTile : public QAbstractButton {
    Q_OBJECT
public:
    enum class State { Normal, Active, Unsaved, Empty };

    explicit FootswitchTile(QWidget* parent = nullptr);

    void setKey(const QString& key);
    void setName(const QString& name);
    void setAccent(const QColor& accent);
    void setState(State state);
    State state() const { return m_state; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString m_key;
    QString m_name;
    QColor m_accent{0x00, 0xB0, 0xFF};
    State m_state = State::Normal;
    bool m_hovered = false;
};
