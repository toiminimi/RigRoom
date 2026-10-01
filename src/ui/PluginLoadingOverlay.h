#pragma once
#include <QWidget>

class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

// Dims the window and shows which plugin is loading. Plugin loading blocks the
// GUI thread, so the fade-in is painted frame by frame before the caller
// blocks; the fade-out runs as a normal animation afterwards.
class PluginLoadingOverlay final : public QWidget {
public:
    explicit PluginLoadingOverlay(QWidget* parent);

    // Shows the overlay and gets it on screen before the caller blocks.
    void showLoading(const QString& pluginName, const QString& detail);
    void finish();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void layoutCard();
    void paintNow();

    QWidget* m_card = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_detail = nullptr;
    QGraphicsOpacityEffect* m_opacity = nullptr;
    QPropertyAnimation* m_fadeOut = nullptr;
    bool m_cursorOverridden = false;
};
