#pragma once
#include "CaptureSounds.h"
#include <QDialog>
#include <optional>

// What to add to the chain, category first (Helix / Quad Cortex style): the
// sound categories open a gallery; "Plugin" opens the plugin browser.
class AddBlockMenu : public QDialog {
    Q_OBJECT
public:
    explicit AddBlockMenu(QWidget* parent = nullptr);
    // Shows it at `globalPos` (kept on screen); returns the choice.
    // std::nullopt with pluginChosen() false means cancelled.
    std::optional<CaptureSounds::Category> choose(const QPoint& globalPos);
    bool pluginChosen() const { return m_plugin; }

private:
    std::optional<CaptureSounds::Category> m_choice;
    bool m_plugin = false;
};
