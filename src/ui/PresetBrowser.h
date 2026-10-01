#pragma once
#include <QDialog>
#include <utility>
#include <vector>

class PresetLibrary;
class PresetSlotTable;
class QLineEdit;
class QLabel;
class QPushButton;

// Popup grid of banks x slots. Emits requests; MainWindow owns the file
// operations and reloads the library afterwards. Everything that needs no
// dialog (move, duplicate, rename, delete confirmation) happens in the grid,
// so it stays open where it was.
class PresetBrowser : public QDialog {
    Q_OBJECT
public:
    enum class PickKind {
        Duplicate, // copy the source into a free slot; the grid stays open
        Move,      // move the source; a taken slot swaps; the grid stays open
        Choose,    // pick a free slot for the caller; the dialog closes
    };

    PresetBrowser(PresetLibrary& library, int currentSlot, QWidget* parent = nullptr);

    // keepScroll: rebuild without moving the view (after an edit made in the grid).
    void refresh(bool keepScroll = false);
    // An empty slot holding the unsaved blank board, shown under `name`.
    void setPendingSlot(int slot, const QString& name);
    // Same, plus the loaded preset's slot; no refresh (for changes made mid-signal).
    void setBoardSlots(int currentSlot, int pendingSlot, const QString& pendingName);
    // Sizes the grid to `window` and drops it down under `anchor`.
    void placeUnder(QWidget* anchor, QWidget* window);

    // Pick mode: the grid asks for a target slot. Esc / Cancel leaves it.
    void beginPick(PickKind kind, int sourceSlot, const QString& prompt);
    void endPick();
    int pickedSlot() const { return m_pickedSlot; }
    bool pickMode() const { return m_pickMode; }
    PickKind pickKind() const { return m_pickKind; }
    int pickSource() const { return m_pickSource; }
    bool isPickTarget(int slot) const;
    // While a cell is dragged: its slot and the slot under the cursor (-1 if none).
    int dragSource() const;
    int dropTarget() const;

signals:
    void slotActivated(int slot);
    void slotsSwapped(int from, int to);
    void blankRequested(int slot);
    void copyCurrentRequested(int slot);
    void savePendingRequested();
    void renameTo(int slot, const QString& name);
    void duplicateTo(int source, int target);
    // Already confirmed in the grid.
    void deleteRequested(int slot);
    void bankRenameRequested(int bank);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyFilter();
    void showSlotMenu(int slot, const QPoint& globalPos);
    void pickSlot(int slot);
    void moveSlot(int from, int to, bool recordUndo = true);
    void undoMove();
    void startRename(int slot);
    void confirmDelete(int slot);
    void showBanner(const QString& text, const QString& actionText = QString());
    void hideBanner();
    void setNormalHint();
    int slotAt(int row, int col) const;

    PresetLibrary& m_library;
    int m_currentSlot;
    int m_pendingSlot = -1;
    QString m_pendingName;
    bool m_pickMode = false;
    PickKind m_pickKind = PickKind::Choose;
    int m_pickSource = -1;
    int m_pickedSlot = -1;
    int m_deleteSlot = -1; // waiting for the banner's Delete button
    std::vector<std::pair<int, int>> m_moves; // for Undo, newest last
    PresetSlotTable* m_table = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QWidget* m_banner = nullptr;
    QLabel* m_bannerLabel = nullptr;
    QPushButton* m_bannerAction = nullptr;
    QWidget* m_undoBar = nullptr;
    QLabel* m_undoLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
};
