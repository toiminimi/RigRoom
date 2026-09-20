#include "FootswitchTile.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

FootswitchTile::FootswitchTile(QWidget* parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void FootswitchTile::setKey(const QString& key) {
    if (key == m_key) return;
    m_key = key;
    update();
}

void FootswitchTile::setName(const QString& name) {
    if (name == m_name) return;
    m_name = name;
    setAccessibleName(m_key + " " + name);
    update();
}

void FootswitchTile::setAccent(const QColor& accent) {
    if (accent == m_accent) return;
    m_accent = accent;
    update();
}

void FootswitchTile::setState(State state) {
    if (state == m_state) return;
    m_state = state;
    update();
}

QSize FootswitchTile::sizeHint() const { return {128, 44}; }
QSize FootswitchTile::minimumSizeHint() const { return {64, 44}; }

void FootswitchTile::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
        event->accept();
        return;
    }
    QAbstractButton::mouseDoubleClickEvent(event);
}

void FootswitchTile::enterEvent(QEnterEvent* event) {
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void FootswitchTile::leaveEvent(QEvent* event) {
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void FootswitchTile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor unsaved(0xFF, 0x98, 0x00);

    QColor bg("#232428");
    QColor border("#35363C");
    QColor keyColor = m_accent;
    QColor nameColor("#E4E4E8");
    Qt::PenStyle borderStyle = Qt::SolidLine;

    switch (m_state) {
    case State::Active:
        bg = m_accent;
        border = m_accent.lighter(120);
        keyColor = QColor(255, 255, 255, 220);
        nameColor = Qt::white;
        if (bg.lightnessF() > 0.62) { keyColor = QColor(16, 16, 18, 200); nameColor = QColor(16, 16, 18); }
        break;
    case State::Unsaved:
        bg = m_accent.darker(260);
        border = unsaved;
        keyColor = unsaved;
        nameColor = Qt::white;
        break;
    case State::Empty:
        bg = QColor("#19191C");
        border = QColor("#34343A");
        borderStyle = Qt::DashLine;
        keyColor = QColor("#4A4A52");
        nameColor = QColor("#55555D");
        break;
    case State::Normal:
        break;
    }
    if (m_hovered && m_state != State::Active) {
        bg = bg.lighter(125);
        border = m_state == State::Empty ? QColor("#60606A") : m_accent;
    }
    if (isDown()) bg = bg.darker(115);

    QPainterPath path;
    path.addRoundedRect(r, 5, 5);
    p.fillPath(path, bg);
    // Accent rail on the left edge, like a scribble-strip colour.
    if (m_state == State::Normal || m_state == State::Unsaved) {
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(r.left(), r.top(), 3, r.height()), m_accent);
        p.restore();
    }
    p.setPen(QPen(border, 1, borderStyle));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    QFont keyFont = font();
    keyFont.setBold(true);
    keyFont.setPixelSize(13);
    p.setFont(keyFont);
    p.setPen(keyColor);
    const QRectF keyRect(r.left() + 9, r.top() + 3, r.width() - 18, 17);
    p.drawText(keyRect, Qt::AlignLeft | Qt::AlignVCenter, m_key);

    if (m_state == State::Unsaved) {
        p.setPen(Qt::NoPen);
        p.setBrush(unsaved);
        p.drawEllipse(QPointF(r.right() - 9, r.top() + 11), 3.5, 3.5);
    }

    QFont nameFont = font();
    nameFont.setBold(m_state == State::Active || m_state == State::Unsaved);
    nameFont.setPixelSize(11);
    p.setFont(nameFont);
    p.setPen(nameColor);
    const QRectF nameRect(r.left() + 9, r.top() + 21, r.width() - 16, 18);
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(nameFont).elidedText(m_name, Qt::ElideRight, qRound(nameRect.width())));
}
