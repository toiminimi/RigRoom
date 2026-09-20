#include "SettingsUi.h"

#include <QFormLayout>
#include <QPainter>
#include <QPixmap>
#include <cmath>
#include <QLabel>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>
#include <QFontMetrics>
#include <QToolTip>
#include <algorithm>

namespace SettingsUi {

Group::Group(const QString& title, QWidget* parent, const QString& help) : QGroupBox(title, parent) {
    setHelp(help);
}

void Group::setHelp(const QString& help) {
    m_help = help;
    if (help.isEmpty()) {
        delete m_info;
        m_info = nullptr;
        return;
    }
    if (!m_info) {
        m_info = new QToolButton(this);
        m_info->setText(QString::fromUtf8("i"));
        m_info->setCursor(Qt::WhatsThisCursor);
        m_info->setFocusPolicy(Qt::NoFocus);
        m_info->setFixedSize(18, 18);
        m_info->setStyleSheet(
            "QToolButton { color: #8A93A6; border: 1px solid #3A3F4B; border-radius: 9px;"
            " background: transparent; font-size: 11px; font-weight: bold; }"
            "QToolButton:hover { color: #E6EAF2; border-color: #00B0FF; }");
        connect(m_info, &QToolButton::clicked, this, [this]() {
            // A tooltip is enough: it reads like a footnote and goes away by itself.
            QToolTip::showText(m_info->mapToGlobal(QPoint(m_info->width(), m_info->height())),
                               QString("<div style='max-width:420px'>%1</div>").arg(m_help), m_info);
        });
    }
    m_info->setToolTip(help);
    m_info->raise();
}

void Group::resizeEvent(QResizeEvent* event) {
    QGroupBox::resizeEvent(event);
    if (!m_info) return;
    // On the heading line, right after the text and centred on it.
    QFont titleFont = font();
    titleFont.setBold(true);
    const QFontMetrics metrics(titleFont);
    const int x = 2 + metrics.horizontalAdvance(title()) + 6;
    const int y = (metrics.height() - m_info->height()) / 2;
    m_info->move(std::min(x, width() - m_info->width() - 8), std::max(0, y));
}

QIcon tabIcon(TabIcon which) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#B7C0D0"));
    pen.setWidthF(2.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (which) {
    case TabIcon::Audio: {   // a speaker with one wave
        QPolygonF cone{{8, 13}, {13, 13}, {18, 8}, {18, 24}, {13, 19}, {8, 19}};
        p.drawPolygon(cone);
        p.drawArc(QRectF(18, 10, 10, 12), -70 * 16, 140 * 16);
        break;
    }
    case TabIcon::Midi:      // a five-pin DIN socket
        p.drawEllipse(QRectF(6, 6, 20, 20));
        // Five pins across the top half, as on a DIN socket.
        for (int i = 0; i < 5; ++i) {
            const double angle = M_PI * static_cast<double>(i) / 4.0;
            const QPointF centre(16, 17);
            const QPointF pin = centre + QPointF(-7.0 * std::cos(angle), -7.0 * std::sin(angle));
            p.drawEllipse(pin, 1.5, 1.5);
        }
        break;
    case TabIcon::Plugins:   // stacked rows, like a plugin list
        for (int row = 0; row < 3; ++row) {
            const int y = 9 + row * 7;
            p.drawLine(7, y, 9, y);
            p.drawLine(13, y, 25, y);
        }
        break;
    case TabIcon::Cloud:     // a globe
        p.drawEllipse(QRectF(6, 6, 20, 20));
        p.drawLine(6, 16, 26, 16);
        p.drawEllipse(QRectF(11.5, 6, 9, 20));
        break;
    case TabIcon::Info:      // a circled i
        p.drawEllipse(QRectF(6, 6, 20, 20));
        p.drawPoint(QPointF(16, 11.5));
        p.drawLine(QPointF(16, 15), QPointF(16, 21));
        break;
    }
    p.end();
    return QIcon(pixmap);
}

QFormLayout* form(QWidget* parent) {
    auto* layout = new QFormLayout(parent);
    // Labels read from the left edge, like the checkboxes and headings above
    // them; the fields still line up because the label column is one column.
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(kGroupSpacing);
    layout->setContentsMargins(12, 10, 12, 10);
    return layout;
}

QLabel* hint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet(kHintCss);
    return label;
}

QLabel* subheading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setStyleSheet("color: #7F8798; font-size: 10px; font-weight: bold; letter-spacing: 1px;");
    return label;
}

}  // namespace SettingsUi
