#pragma once
#include <QGroupBox>
#include <QIcon>
#include <QString>

class QFormLayout;
class QLabel;
class QToolButton;
class QWidget;

// Shared pieces for the Settings pages, so every tab looks the same: one
// heading style, one hint style, and long explanations tucked behind an "i"
// button instead of sitting in the middle of the controls.
namespace SettingsUi {

// Colours and sizes every settings page uses.
inline constexpr const char* kHintColor = "#8A93A6";
inline constexpr const char* kHintCss = "color: #8A93A6; font-size: 11px;";
inline constexpr const char* kValueCss = "color: #C9CDD5; font-size: 12px;";
// Settings pages say three things in colour and nothing else: a quiet hint,
// something that needs attention, and something that went well.
inline constexpr const char* kWarningCss = "color: #E0A34A; font-size: 11px;";
inline constexpr const char* kOkCss = "color: #4CAF50; font-size: 11px;";
inline constexpr int kGroupSpacing = 8;
// The frame every settings group wears, so the pages match.
inline constexpr const char* kGroupStyle =
    "QGroupBox { font-weight: bold; color: #6FBEEA; border: 1px solid #26262E; border-radius: 6px;"
    " margin-top: 22px; padding: 12px; background: transparent; }"
    "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 2px;"
    " top: 0px; padding: 0; }";
// Height of the heading strip above the frame, which the "i" button lines up with.
inline constexpr int kTitleStrip = 22;
inline constexpr int kLabelColumnWidth = 150;

// A group box whose long explanation lives in a popup on its "i" button.
class Group : public QGroupBox {
public:
    Group(const QString& title, QWidget* parent, const QString& help = QString());
    void setHelp(const QString& help);

protected:
    void resizeEvent(class QResizeEvent* event) override;

private:
    QToolButton* m_info = nullptr;
    QString m_help;
};

// Icons for the settings tabs. Drawn here rather than taken from the platform
// theme, whose stock icons differ in colour and weight from one to the next.
enum class TabIcon { Audio, Midi, Plugins, Cloud, Info };
QIcon tabIcon(TabIcon which);

// A form whose labels line up in one column across every page.
QFormLayout* form(QWidget* parent);
// One line of quiet explanation, for the few that are worth keeping in view.
QLabel* hint(const QString& text, QWidget* parent);
// The heading above a block of rows inside a group.
QLabel* subheading(const QString& text, QWidget* parent);

}  // namespace SettingsUi
