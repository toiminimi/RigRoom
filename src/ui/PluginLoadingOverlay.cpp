#include "PluginLoadingOverlay.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QThread>
#include <QVBoxLayout>

namespace {
constexpr int kFadeInFrames = 7;      // ~120 ms at 60 Hz, drawn before loading blocks
constexpr int kFadeOutMs = 220;
}

PluginLoadingOverlay::PluginLoadingOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NoSystemBackground);
    hide();

    auto* card = new QFrame(this);
    card->setObjectName("loadingCard");
    card->setStyleSheet(
        "QFrame#loadingCard { background:#22242A; border:1px solid #363941; border-radius:10px; }");
    card->setFixedWidth(360);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(6);

    auto* header = new QLabel("LOADING PLUGIN", card);
    header->setStyleSheet("color:#00B0FF; font-size:10px; font-weight:bold; letter-spacing:1.2px;"
                          " background:transparent; border:none;");
    m_title = new QLabel(card);
    m_title->setStyleSheet("color:#E0E0E0; font-size:16px; font-weight:bold; background:transparent; border:none;");
    m_title->setWordWrap(true);
    m_detail = new QLabel(card);
    m_detail->setStyleSheet("color:#99A2B0; font-size:11px; background:transparent; border:none;");
    m_detail->setWordWrap(true);

    auto* accent = new QFrame(card);
    accent->setFixedHeight(3);
    accent->setStyleSheet("background:qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                          " stop:0 #00B0FF, stop:1 #009688); border:none; border-radius:1px;");

    layout->addWidget(header);
    layout->addWidget(m_title);
    layout->addSpacing(4);
    layout->addWidget(accent);
    layout->addSpacing(4);
    layout->addWidget(m_detail);
    m_card = card;

    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(0.0);
    setGraphicsEffect(m_opacity);

    m_fadeOut = new QPropertyAnimation(m_opacity, "opacity", this);
    m_fadeOut->setDuration(kFadeOutMs);
    m_fadeOut->setEasingCurve(QEasingCurve::OutCubic);
    m_fadeOut->setEndValue(0.0);
    connect(m_fadeOut, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacity->opacity() <= 0.0) hide();
    });

    parent->installEventFilter(this);
}

void PluginLoadingOverlay::showLoading(const QString& pluginName, const QString& detail) {
    // Nothing to show while the window itself is hidden (e.g. the preset
    // loaded at startup); showing now would leave the overlay up once the
    // window appears.
    if (!parentWidget()->isVisible()) return;

    m_title->setText(pluginName);
    m_detail->setText(detail);
    m_detail->setVisible(!detail.isEmpty());
    layoutCard();

    if (!m_cursorOverridden) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_cursorOverridden = true;
    }

    // Already up (or still fading out): just switch the text, no flicker.
    const bool alreadyShown = isVisible() && m_opacity->opacity() > 0.0;
    m_fadeOut->stop();
    raise();
    show();
    if (alreadyShown) {
        m_opacity->setOpacity(1.0);
        paintNow();
        return;
    }

    // Draw the fade-in ourselves: the event loop will be blocked while the
    // plugin loads, so no animation could run. No processEvents() here,
    // since timers (MIDI, scenes) must not run in the middle of a preset load.
    for (int frame = 1; frame <= kFadeInFrames; ++frame) {
        const double t = static_cast<double>(frame) / kFadeInFrames;
        m_opacity->setOpacity(1.0 - (1.0 - t) * (1.0 - t)); // ease-out
        paintNow();
        if (frame < kFadeInFrames) QThread::msleep(16);
    }
}

void PluginLoadingOverlay::finish() {
    if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    if (isHidden()) return;
    if (!parentWidget()->isVisible()) {
        m_fadeOut->stop();
        m_opacity->setOpacity(0.0);
        hide();
        return;
    }
    m_fadeOut->stop();
    m_fadeOut->setStartValue(m_opacity->opacity());
    m_fadeOut->start();
}

void PluginLoadingOverlay::paintNow() {
    repaint();
    QGuiApplication::sync();
}

void PluginLoadingOverlay::layoutCard() {
    setGeometry(parentWidget()->rect());
    m_card->adjustSize();
    m_card->move((width() - m_card->width()) / 2, (height() - m_card->height()) / 2);
}

void PluginLoadingOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(10, 12, 16, 150));
}

bool PluginLoadingOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) layoutCard();
    return QWidget::eventFilter(watched, event);
}
