#pragma once

// Look shared by the browser windows (plugins, TONE3000), so they read as
// one application: sidebar, search field, chips, result list and buttons.
namespace BrowserStyle {

inline constexpr const char* kStyleSheet =
    "QDialog { background-color: #141417; }"
    "QLabel { color: #D8D8DE; background: transparent; }"
    "QLineEdit { background-color: #1F1F24; color: #EDEDF2; border: 1px solid #34343C; border-radius: 6px; padding: 7px 10px; font-size: 13px; }"
    "QLineEdit:focus { border-color: #00B0FF; }"
    "QListWidget#sidebar { background: #17171B; border: none; color: #C8C8D0; font-size: 12px; outline: none; }"
    "QListWidget#sidebar::item { padding: 6px 10px; border-radius: 5px; }"
    "QListWidget#sidebar::item:selected { background: #0B4F6C; color: white; }"
    "QListWidget#sidebar::item:hover:!selected { background: #202026; }"
    "QListWidget#sidebar[dropping=\"true\"]::item:selected { background: #00B0FF; color: #07121A; font-weight: bold; }"
    "QListView#results { background: #17171B; border: 1px solid #26262C; border-radius: 6px; outline: none; }"
    "QToolButton.formatChip { background: #1F1F24; color: #B8B8C4; border: 1px solid #34343C; border-radius: 12px; padding: 3px 12px; font-size: 11px; }"
    "QToolButton.formatChip:checked { background: #0B4F6C; color: white; border-color: #00B0FF; }"
    "QToolButton.formatChip:hover { border-color: #00B0FF; }"
    "QToolButton.formatChip::menu-indicator { image: none; width: 0; }"
    "QPushButton { background: #2A2A30; color: #E0E0E0; border: 1px solid #3A3A42; border-radius: 5px; padding: 6px 14px; }"
    "QPushButton:hover { background: #34343C; }"
    "QPushButton:disabled { color: #666; }"
    "QPushButton#addButton { background: #00897B; border: none; font-weight: bold; color: white; }"
    "QPushButton#addButton:hover { background: #009688; }"
    "QPushButton#addButton:disabled { background: #2A2A30; color: #666; }"
    "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
    "QScrollBar::handle:vertical { background: #34343C; border-radius: 4px; min-height: 32px; }"
    "QScrollBar::handle:vertical:hover { background: #45454F; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    "QMenu { background: #1F1F24; color: #D8D8DE; border: 1px solid #34343C; padding: 4px; }"
    "QMenu::item { padding: 5px 22px 5px 12px; border-radius: 4px; }"
    "QMenu::item:selected { background: #0B4F6C; color: white; }"
    "QMenu::separator { height: 1px; background: #34343C; margin: 4px 6px; }";

// The primary action button, styled on the button itself so the app-wide
// button style can't override it.
inline constexpr const char* kPrimaryButton =
    "QPushButton { background: #00897B; border: none; font-weight: bold; color: white; border-radius: 5px; padding: 7px 16px; }"
    "QPushButton:hover { background: #009688; }"
    "QPushButton:disabled { background: #2A2A30; color: #666; }";

inline constexpr const char* kSecondaryButton =
    "QPushButton { background: #2A2A30; color: #E0E0E0; border: 1px solid #3A3A42; border-radius: 5px; padding: 7px 12px; }"
    "QPushButton:hover { background: #34343C; }"
    "QPushButton:disabled { color: #666; }";

} // namespace BrowserStyle
