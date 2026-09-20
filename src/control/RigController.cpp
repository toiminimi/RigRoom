#include "RigController.h"

bool RigController::selectSlot(int slot, bool remote) {
    return m_backend.loadSlot && m_backend.loadSlot(slot, remote);
}

bool RigController::selectBankSlot(int bank, int indexInBank, bool remote) {
    if (!m_backend.slotFor) return false;
    return selectSlot(m_backend.slotFor(bank, indexInBank), remote);
}

bool RigController::stepPreset(int dir, bool remote) {
    if (!m_backend.currentSlot) return false;
    const int current = m_backend.currentSlot();
    const int step = dir < 0 ? -1 : 1;

    // Staying in the bank keeps a footswitch from wandering off mid-set: the
    // bank only changes when bank up/down says so, and the ends wrap around.
    if (m_backend.stepWithinBank && m_backend.stepWithinBank() && m_backend.viewBank
        && m_backend.slotFor && m_backend.slotOccupied && m_backend.slotsPerBank) {
        const int bank = m_backend.viewBank();
        const int perBank = m_backend.slotsPerBank();
        if (perBank < 1) return false;
        const int first = m_backend.slotFor(bank, 0);
        // Start from the loaded preset when it is in this bank, otherwise from
        // the edge, so the first press lands on the bank's first or last preset.
        const bool inBank = current >= first && current < first + perBank;
        int index = inBank ? current - first : (step > 0 ? -1 : perBank);
        for (int i = 0; i < perBank; ++i) {
            index = ((index + step) % perBank + perBank) % perBank;
            const int slot = m_backend.slotFor(bank, index);
            if (slot >= 0 && m_backend.slotOccupied(slot) && slot != current) {
                return selectSlot(slot, remote);
            }
        }
        return false;
    }

    if (!m_backend.nextOccupiedSlot) return false;
    const int slot = m_backend.nextOccupiedSlot(current, step);
    return slot >= 0 && slot != current && selectSlot(slot, remote);
}

void RigController::stepBank(int dir) {
    if (!m_backend.viewBank || !m_backend.setViewBank || !m_backend.bankCount) return;
    const int count = m_backend.bankCount();
    if (count < 1) return;
    m_backend.setViewBank(((m_backend.viewBank() + (dir < 0 ? -1 : 1)) % count + count) % count);
}

bool RigController::selectInBank(int indexInBank, bool remote) {
    if (!m_backend.viewBank) return false;
    return selectBankSlot(m_backend.viewBank(), indexInBank, remote);
}

void RigController::selectScene(int scene) {
    if (m_backend.selectScene) m_backend.selectScene(scene);
}

void RigController::stepScene(int dir) {
    if (!m_backend.sceneCount || !m_backend.activeScene) return;
    const int count = m_backend.sceneCount();
    if (count < 2) return;
    selectScene(((m_backend.activeScene() + dir) % count + count) % count);
}

bool RigController::toggleBlock(const std::string& nodeId) {
    return m_backend.toggleBlock && m_backend.toggleBlock(nodeId);
}

bool RigController::setBlockEnabled(const std::string& nodeId, bool enabled) {
    return m_backend.setBlockEnabled && m_backend.setBlockEnabled(nodeId, enabled);
}

bool RigController::setParam(const std::string& nodeId, uint32_t index, float normalized) {
    return m_backend.setParam && m_backend.setParam(nodeId, index, normalized);
}
