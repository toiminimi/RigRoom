#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <map>

// Numbered bank x slot index over the board preset files.
// Slots are flat 0-based numbers (slot 0 = "01A"), which later map 1:1 to
// MIDI Program Change numbers. Preset files stay in the presets folder; the
// library only records which file lives in which slot.
class PresetLibrary {
public:
    static constexpr int kDefaultSlotsPerBank = 4;
    static constexpr int kDefaultNumBanks = 32;

    explicit PresetLibrary(QString presetDir = {}, QString indexPath = {});

    void setPaths(const QString& presetDir, const QString& indexPath);
    const QString& presetDir() const { return m_presetDir; }

    // Reads the index (migrating a flat folder on first run) and reconciles it
    // with the files on disk. Returns true if the index changed and was saved.
    bool load();
    bool save() const;

    int slotsPerBank() const { return m_slotsPerBank; }
    int numBanks() const { return m_numBanks; }
    int slotCount() const { return m_slotsPerBank * m_numBanks; }
    void setLayout(int slotsPerBank, int numBanks);

    QString slotLabel(int slot) const;

    // Optional bank names ("Floyd", "Gig set 1") for organising.
    QString bankName(int bank) const;
    void setBankName(int bank, const QString& name);
    // "04" or "04 Floyd"
    QString bankLabel(int bank) const;

    // Scene names stored in the preset in `slot`. Empty for single-scene or
    // older presets. Cached by file modification time.
    QStringList sceneNamesAt(int slot) const;
    int bankOf(int slot) const { return slot / m_slotsPerBank; }
    int indexInBank(int slot) const { return slot % m_slotsPerBank; }
    int slotFor(int bank, int indexInBank) const { return bank * m_slotsPerBank + indexInBank; }

    bool isValidSlot(int slot) const { return slot >= 0 && slot < slotCount(); }
    bool isOccupied(int slot) const { return m_slots.count(slot) > 0; }
    // File name (e.g. "Clean.json") or empty.
    QString presetAt(int slot) const;
    // Display name without extension, or empty.
    QString nameAt(int slot) const;
    QString pathAt(int slot) const;
    int slotOf(const QString& fileName) const;
    int slotOfName(const QString& presetName) const;

    void assign(int slot, const QString& fileName);
    void clear(int slot);
    void swap(int a, int b);
    // Moves a to b; whatever was in b goes to a.
    void move(int from, int to) { swap(from, to); }
    void renameFile(const QString& oldFileName, const QString& newFileName);

    // Next occupied slot in direction dir (+1/-1), wrapping. -1 if none.
    int nextOccupied(int from, int dir) const;
    // Same position in the next/previous bank, wrapping.
    int stepBank(int from, int dir) const;
    int firstFreeSlot(int startAt = 0) const;
    int occupiedCount() const { return static_cast<int>(m_slots.size()); }
    const std::map<int, QString>& entries() const { return m_slots; }

    // Adds files that aren't indexed to free slots and drops entries whose file
    // is gone. Returns true if anything changed.
    bool reconcileWithDisk();

private:
    QStringList presetFilesOnDisk() const;

    QString m_presetDir;
    QString m_indexPath;
    int m_slotsPerBank = kDefaultSlotsPerBank;
    int m_numBanks = kDefaultNumBanks;
    std::map<int, QString> m_slots;
    std::map<int, QString> m_bankNames;

    struct SceneNameCache {
        QDateTime modified;
        QStringList names;
    };
    mutable std::map<QString, SceneNameCache> m_sceneNameCache;
};
