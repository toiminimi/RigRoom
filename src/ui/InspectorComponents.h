#pragma once

#include <QColor>
#include <QDial>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <vector>

class InspectorCard final : public QFrame {
public:
  explicit InspectorCard(QWidget *parent = nullptr);
};

class InspectorSectionHeader final : public QLabel {
public:
  explicit InspectorSectionHeader(const QString &text,
                                  QWidget *parent = nullptr);
};

class FlowLayout final : public QLayout {
public:
  explicit FlowLayout(QWidget *parent = nullptr, int spacing = 8);
  ~FlowLayout() override;
  void addItem(QLayoutItem *item) override;
  int count() const override;
  QLayoutItem *itemAt(int index) const override;
  QLayoutItem *takeAt(int index) override;
  Qt::Orientations expandingDirections() const override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;
  QSize minimumSize() const override;
  QSize sizeHint() const override;
  void setGeometry(const QRect &rect) override;

private:
  int layoutItems(const QRect &rect, bool testOnly) const;
  std::vector<QLayoutItem *> m_items;
};

class InspectorKnob final : public QDial {
public:
  explicit InspectorKnob(QWidget *parent = nullptr);
  void setDefaultValue(int value);
  void setAccentColor(const QColor &color);
  void setAccessibleValueText(const QString &value);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  int m_defaultValue = 500;
  int m_dragStartValue = 0;
  QPointF m_dragStartPosition;
  QColor m_accent = QColor("#55B8E8");
  bool m_dragging = false;
};
