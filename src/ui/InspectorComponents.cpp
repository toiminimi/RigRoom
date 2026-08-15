#include "InspectorComponents.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

InspectorCard::InspectorCard(QWidget *parent) : QFrame(parent) {
  setObjectName("inspectorCard");
  setFrameShape(QFrame::NoFrame);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  setStyleSheet("QFrame#inspectorCard { background:#22242A; border:1px solid "
                "#363941; border-radius:8px; }");
}

InspectorSectionHeader::InspectorSectionHeader(const QString &text,
                                               QWidget *parent)
    : QLabel(text, parent) {
  setStyleSheet("color:#99A2B0; font-size:10px; font-weight:bold; "
                "letter-spacing:1.1px; background:transparent; border:none;");
}

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout(parent) {
  setContentsMargins(0, 0, 0, 0);
  setSpacing(spacing);
}
FlowLayout::~FlowLayout() {
  while (QLayoutItem *item = takeAt(0))
    delete item;
}
void FlowLayout::addItem(QLayoutItem *item) { m_items.push_back(item); }
int FlowLayout::count() const { return static_cast<int>(m_items.size()); }
QLayoutItem *FlowLayout::itemAt(int index) const {
  return index >= 0 && index < count() ? m_items[index] : nullptr;
}
QLayoutItem *FlowLayout::takeAt(int index) {
  if (index < 0 || index >= count())
    return nullptr;
  auto *item = m_items[index];
  m_items.erase(m_items.begin() + index);
  return item;
}
Qt::Orientations FlowLayout::expandingDirections() const { return {}; }
bool FlowLayout::hasHeightForWidth() const { return true; }
int FlowLayout::heightForWidth(int width) const {
  return layoutItems(QRect(0, 0, width, 0), true);
}
QSize FlowLayout::minimumSize() const {
  QSize size;
  for (auto *item : m_items)
    size = size.expandedTo(item->minimumSize());
  const auto margins = contentsMargins();
  return size + QSize(margins.left() + margins.right(),
                      margins.top() + margins.bottom());
}
QSize FlowLayout::sizeHint() const { return minimumSize(); }
void FlowLayout::setGeometry(const QRect &rect) {
  QLayout::setGeometry(rect);
  layoutItems(rect, false);
}
int FlowLayout::layoutItems(const QRect &rect, bool testOnly) const {
  const auto margins = contentsMargins();
  const QRect area = rect.adjusted(margins.left(), margins.top(),
                                   -margins.right(), -margins.bottom());
  int x = area.x(), y = area.y(), rowHeight = 0;
  for (auto *item : m_items) {
    const QSize size = item->sizeHint();
    if (x + size.width() > area.right() + 1 && rowHeight > 0) {
      x = area.x();
      y += rowHeight + spacing();
      rowHeight = 0;
    }
    if (!testOnly)
      item->setGeometry(QRect(QPoint(x, y), size));
    x += size.width() + spacing();
    rowHeight = std::max(rowHeight, size.height());
  }
  return y + rowHeight - rect.y() + margins.bottom();
}

InspectorKnob::InspectorKnob(QWidget *parent) : QDial(parent) {
  setFixedSize(52, 52);
  setNotchesVisible(false);
  setWrapping(false);
  setCursor(Qt::SizeVerCursor);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName("Parameter control");
}
void InspectorKnob::setDefaultValue(int value) {
  m_defaultValue = value;
  update();
}
void InspectorKnob::setAccentColor(const QColor &color) {
  m_accent = color;
  update();
}
void InspectorKnob::setAccessibleValueText(const QString &value) {
  setAccessibleDescription(value);
}
void InspectorKnob::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragStartPosition = event->position();
    m_dragStartValue = this->value();
    m_dragging = true;
    setSliderDown(true);
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QDial::mousePressEvent(event);
}
void InspectorKnob::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging) {
    const double travel =
        event->modifiers().testFlag(Qt::ShiftModifier) ? 1600.0 : 160.0;
    const int range = maximum() - minimum();
    setValue(std::clamp(m_dragStartValue + qRound((m_dragStartPosition.y() -
                                                   event->position().y()) *
                                                  range / travel),
                        minimum(), maximum()));
    event->accept();
    return;
  }
  QDial::mouseMoveEvent(event);
}
void InspectorKnob::mouseReleaseEvent(QMouseEvent *event) {
  if (m_dragging && event->button() == Qt::LeftButton) {
    m_dragging = false;
    setSliderDown(false);
    setCursor(Qt::SizeVerCursor);
    event->accept();
    return;
  }
  QDial::mouseReleaseEvent(event);
}
void InspectorKnob::mouseDoubleClickEvent(QMouseEvent *event) {
  setValue(m_defaultValue);
  event->accept();
}
void InspectorKnob::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const int side = qMin(width(), height());
  const QRectF r((width() - side) / 2.0 + 3, (height() - side) / 2.0 + 3,
                 side - 6, side - 6);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#15171B"));
  p.drawEllipse(r);
  const int range = std::max(1, maximum() - minimum());
  const double ratio =
      std::clamp(double(value() - minimum()) / range, 0.0, 1.0);
  constexpr double pi = 3.14159265358979323846;
  const double start = 225.0;
  const double span = -270.0 * ratio;
  const double def =
      (start - 270.0 * std::clamp(double(m_defaultValue - minimum()) / range,
                                  0.0, 1.0)) *
      pi / 180.0;
  const auto c = r.center();
  const double radius = r.width() / 2.0 - 1.5;
  p.setPen(QPen(QColor("#6B7280"), 1.4, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(c + QPointF((radius - 4) * cos(def), -(radius - 4) * sin(def)),
             c + QPointF(radius * cos(def), -radius * sin(def)));
  QPen arc(m_accent, 3);
  arc.setCapStyle(Qt::RoundCap);
  p.setPen(arc);
  p.setBrush(Qt::NoBrush);
  p.drawArc(r.adjusted(1.5, 1.5, -1.5, -1.5), start * 16, span * 16);
  const QRectF inner = r.adjusted(4, 4, -4, -4);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#30333A"));
  p.drawEllipse(inner);
  const double a = (start + span) * pi / 180.0;
  const double ir = inner.width() / 2.0;
  p.setBrush(Qt::white);
  p.drawEllipse(QPointF(inner.center().x() + (ir - 2.5) * cos(a),
                        inner.center().y() - (ir - 2.5) * sin(a)),
                1.5, 1.5);
  if (hasFocus()) {
    p.setPen(QPen(QColor("#B5E7FF"), 1.2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r.adjusted(-1, -1, 1, 1));
  }
}
