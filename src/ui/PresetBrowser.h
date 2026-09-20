#pragma once
#include <QDialog>

class PresetLibrary;
class PresetSlotTable;
class QLineEdit;
class QLabel;

// Popup grid of banks x slots. Emits requests; MainWindow owns the file
// operations and reloads the library afterwards.
class PresetBrowser : public QDialog {
    Q_OBJECT
public:
    PresetBrowser(PresetLibrary& library, int currentSlot, QWidget* parent = nullptr);

    void refresh();

signals:
    void slotActivated(int slot);
    void slotsSwapped(int from, int to);
    void saveCurrentToSlotRequested(int slot);
    void renameRequested(int slot);
    void duplicateRequested(int slot);
    void deleteRequested(int slot);
    void bankRenameRequested(int bank);

private:
    void applyFilter();
    void showSlotMenu(int slot, const QPoint& globalPos);
    int slotAt(int row, int col) const;

    PresetLibrary& m_library;
    int m_currentSlot;
    PresetSlotTable* m_table = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_hintLabel = nullptr;
};
