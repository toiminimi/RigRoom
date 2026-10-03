#pragma once
#include "CaptureSounds.h"
#include <QWidget>
#include <memory>

class CaptureNode;
class QHBoxLayout;
class QPushButton;

// Inspector for the built-in capture block, drawn as one device at a fixed
// height: the sound (amp, pedal, full rig, or the IR of a cab / room block)
// with its controls, and for amps a cab section with a quick cab menu.
class CapturePanel : public QWidget {
    Q_OBJECT
public:
    explicit CapturePanel(std::shared_ptr<CaptureNode> node, QWidget* parent = nullptr);

    // For the block in the chain: "AMP + CAB", "PEDAL", "ROOM"... and its colour.
    static QString blockLabel(const CaptureNode& node);
    static QColor blockAccent(const CaptureNode& node);
    // Which gallery "Change" opens for what the block holds.
    static CaptureSounds::Category changeCategory(const CaptureNode& node, bool ir);

signals:
    void galleryRequested(CaptureSounds::Category category);
    void detailsRequested();
    // Something the panel changed itself (cab picked from the menu, cab removed).
    void changed();
    // A knob or switch moved.
    void edited();

private:
    QWidget* buildSound(bool irIsMain);
    QWidget* buildCab();
    QWidget* buildNoCab();
    QPushButton* cabMenuButton(QWidget* parent, const QString& text);
    QWidget* knob(QWidget* parent, const QString& name, uint32_t param, double min, double max,
                  const QString& unit, int decimals, double display, const QColor& accent);

    std::shared_ptr<CaptureNode> m_node;
};
