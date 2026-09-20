#pragma once
#include <QObject>
#include <cstdint>
#include <functional>
#include <string>

// Single entry point for performance actions (preset slots, scenes, blocks).
// UI shortcuts and buttons call these today; a MIDI layer can later map
// Program Change -> selectSlot, a CC -> selectScene, CCs -> toggleBlock/setParam
// without knowing about MainWindow.
class RigController : public QObject {
    Q_OBJECT
public:
    // Implemented by the window that owns the board.
    struct Backend {
        // remote = triggered by MIDI: never block on an unsaved-changes prompt.
        std::function<bool(int slot, bool remote)> loadSlot;
        std::function<int()> currentSlot;
        std::function<int(int from, int dir)> nextOccupiedSlot;
        std::function<int()> viewBank;
        std::function<void(int bank)> setViewBank;
        std::function<int()> bankCount;
        std::function<int(int bank, int indexInBank)> slotFor;
        std::function<bool(int slot)> slotOccupied;
        std::function<int()> slotsPerBank;
        // True when previous/next preset should stay inside the shown bank.
        std::function<bool()> stepWithinBank;
        std::function<void(int scene)> selectScene;
        std::function<int()> activeScene;
        std::function<int()> sceneCount;
        std::function<bool(const std::string& nodeId)> toggleBlock;
        std::function<bool(const std::string& nodeId, bool enabled)> setBlockEnabled;
        std::function<bool(const std::string& nodeId, uint32_t index, float normalized)> setParam;
    };

    explicit RigController(QObject* parent = nullptr) : QObject(parent) {}
    void setBackend(Backend backend) { m_backend = std::move(backend); }

public slots:
    // `remote` (MIDI) switches discard unsaved edits instead of asking.
    bool selectSlot(int slot, bool remote = false);
    bool selectBankSlot(int bank, int indexInBank, bool remote = false);
    bool stepPreset(int dir, bool remote = false);
    // Like bank up/down on a floor unit: changes the shown bank, loads nothing.
    void stepBank(int dir);
    // Footswitch A, B, C... in the shown bank.
    bool selectInBank(int indexInBank, bool remote = false);
    void selectScene(int scene);
    void stepScene(int dir);
    bool toggleBlock(const std::string& nodeId);
    bool setBlockEnabled(const std::string& nodeId, bool enabled);
    // normalized is 0..1 across the parameter's range (what a MIDI CC maps to).
    bool setParam(const std::string& nodeId, uint32_t index, float normalized);

signals:
    // Emitted by the owning window after a state change, whatever caused it.
    void slotChanged(int slot);
    void sceneChanged(int scene);
    void blockToggled(const QString& nodeId, bool bypassed);

private:
    Backend m_backend;
};
