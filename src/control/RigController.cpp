#include "RigController.h"

bool RigController::selectSlot(int slot) {
    return m_backend.loadSlot && m_backend.loadSlot(slot);
}

bool RigController::selectBankSlot(int bank, int indexInBank) {
    if (!m_backend.slotFor) return false;
    return selectSlot(m_backend.slotFor(bank, indexInBank));
}

bool RigController::stepPreset(int dir) {
    if (!m_backend.nextOccupiedSlot || !m_backend.currentSlot) return false;
    const int current = m_backend.currentSlot();
    const int slot = m_backend.nextOccupiedSlot(current, dir);
    return slot >= 0 && slot != current && selectSlot(slot);
}

void RigController::stepBank(int dir) {
    if (!m_backend.viewBank || !m_backend.setViewBank || !m_backend.bankCount) return;
    const int count = m_backend.bankCount();
    if (count < 1) return;
    m_backend.setViewBank(((m_backend.viewBank() + (dir < 0 ? -1 : 1)) % count + count) % count);
}

bool RigController::selectInBank(int indexInBank) {
    if (!m_backend.viewBank) return false;
    return selectBankSlot(m_backend.viewBank(), indexInBank);
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

bool RigController::setParam(const std::string& nodeId, uint32_t index, float normalized) {
    return m_backend.setParam && m_backend.setParam(nodeId, index, normalized);
}
