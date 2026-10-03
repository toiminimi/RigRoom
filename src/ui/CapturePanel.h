#pragma once
#include <QWidget>
#include <memory>

class CaptureNode;
class QHBoxLayout;

// Inspector for the built-in capture block: an amp / pedal / capture card for
// the NAM model and a cab / room / effect card for the IR, each shaped by what
// is loaded. Choosing files and saving is left to MainWindow (signals).
class CapturePanel : public QWidget {
    Q_OBJECT
public:
    explicit CapturePanel(std::shared_ptr<CaptureNode> node, QWidget* parent = nullptr);

    // What the block holds, from the capture's and the IR's gear types.
    enum class Kind { Empty, Amp, AmpCab, Pedal, Outboard, Capture, IrOnly };
    static Kind kindOf(const CaptureNode& node);
    // "Amp", "Amp + Cab", "Pedal", "Cab", "Room"... for chips and the chain.
    static QString modelTypeLabel(const CaptureNode& node);
    static QString irTypeLabel(const CaptureNode& node);
    static QColor modelAccent(const CaptureNode& node);
    static QColor irAccent(const CaptureNode& node);
    // For the block in the chain: "AMP + CAB", "PEDAL", "ROOM"... and its colour.
    static QString blockLabel(const CaptureNode& node);
    static QColor blockAccent(const CaptureNode& node);

signals:
    void browseRequested(bool ir);
    void removeRequested(bool ir);
    void detailsRequested();
    void edited();

private:
    QWidget* buildModelCard();
    QWidget* buildIrCard(bool alone);
    QWidget* buildGhost(const QString& title, const QString& text, bool ir, bool wide);
    // min/max in parameter units; `display` scales the shown value (100 for %).
    QWidget* knob(QWidget* parent, const QString& name, uint32_t param, double min, double max,
                  const QString& unit, int decimals, double display, const QColor& accent);

    std::shared_ptr<CaptureNode> m_node;
};
