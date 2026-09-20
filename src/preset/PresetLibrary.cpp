#include "PresetLibrary.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <set>

PresetLibrary::PresetLibrary(QString presetDir, QString indexPath)
    : m_presetDir(std::move(presetDir)), m_indexPath(std::move(indexPath)) {}

void PresetLibrary::setPaths(const QString& presetDir, const QString& indexPath) {
    m_presetDir = presetDir;
    m_indexPath = indexPath;
}

bool PresetLibrary::load() {
    m_slots.clear();
    m_bankNames.clear();
    bool changed = false;

    QFile file(m_indexPath);
    if (file.open(QFile::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        // Older builds let "slotsPerBank" change; slot numbers were kept flat,
        // so reading them with 4 per bank puts every preset back in place.
        m_numBanks = std::clamp(obj["numBanks"].toInt(kDefaultNumBanks), 1, 128);
        const QJsonObject slotsObj = obj["slots"].toObject();
        for (auto it = slotsObj.constBegin(); it != slotsObj.constEnd(); ++it) {
            bool ok = false;
            const int slot = it.key().toInt(&ok);
            const QString fileName = it.value().toString();
            if (ok && slot >= 0 && !fileName.isEmpty() && slotOf(fileName) < 0) {
                m_slots[slot] = fileName;
            }
        }
        const QJsonObject namesObj = obj["bankNames"].toObject();
        for (auto it = namesObj.constBegin(); it != namesObj.constEnd(); ++it) {
            bool ok = false;
            const int bank = it.key().toInt(&ok);
            const QString name = it.value().toString().trimmed();
            if (ok && bank >= 0 && bank < m_numBanks && !name.isEmpty()) m_bankNames[bank] = name;
        }
    } else {
        // First run: place existing presets alphabetically from 01A.
        changed = true;
    }

    // Entries that no longer fit the layout are re-homed by reconcile.
    for (auto it = m_slots.begin(); it != m_slots.end();) {
        if (!isValidSlot(it->first)) { it = m_slots.erase(it); changed = true; }
        else ++it;
    }

    changed |= reconcileWithDisk();
    if (changed) save();
    return changed;
}

bool PresetLibrary::save() const {
    if (m_indexPath.isEmpty()) return false;
    QDir().mkpath(QFileInfo(m_indexPath).absolutePath());
    QJsonObject slotsObj;
    for (const auto& [slot, fileName] : m_slots) {
        slotsObj[QString::number(slot)] = fileName;
    }
    QJsonObject obj;
    obj["version"] = 1;
    obj["slotsPerBank"] = kSlotsPerBank;
    obj["numBanks"] = m_numBanks;
    obj["slots"] = slotsObj;
    QJsonObject namesObj;
    for (const auto& [bank, name] : m_bankNames) namesObj[QString::number(bank)] = name;
    obj["bankNames"] = namesObj;

    QSaveFile file(m_indexPath);
    if (!file.open(QFile::WriteOnly)) return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return file.commit();
}

void PresetLibrary::setNumBanks(int numBanks) {
    m_numBanks = std::clamp(numBanks, minBanks(), 128);
}

int PresetLibrary::minBanks() const {
    return m_slots.empty() ? 1 : bankOf(m_slots.rbegin()->first) + 1;
}

QString PresetLibrary::bankName(int bank) const {
    auto it = m_bankNames.find(bank);
    return it != m_bankNames.end() ? it->second : QString();
}

void PresetLibrary::setBankName(int bank, const QString& name) {
    if (bank < 0 || bank >= m_numBanks) return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) m_bankNames.erase(bank);
    else m_bankNames[bank] = trimmed;
}

QString PresetLibrary::bankLabel(int bank) const {
    const QString number = QString("%1").arg(bank + 1, 2, 10, QChar('0'));
    const QString name = bankName(bank);
    return name.isEmpty() ? number : number + " " + name;
}

QStringList PresetLibrary::sceneNamesAt(int slot) const {
    const QString path = pathAt(slot);
    if (path.isEmpty()) return {};
    const QFileInfo info(path);
    if (!info.exists()) return {};
    auto cached = m_sceneNameCache.find(path);
    if (cached != m_sceneNameCache.end() && cached->second.modified == info.lastModified()) {
        return cached->second.names;
    }

    QStringList names;
    QFile file(path);
    if (file.open(QFile::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonArray list = obj["scenes"].toObject()["list"].toArray();
        if (list.size() > 1) {
            for (const QJsonValue& scene : list) names << scene.toObject()["name"].toString();
        }
    }
    m_sceneNameCache[path] = {info.lastModified(), names};
    return names;
}

QString PresetLibrary::slotLabel(int slot) const {
    if (slot < 0) return QString();
    const int bank = bankOf(slot) + 1;
    const int idx = indexInBank(slot);
    return QString("%1%2").arg(bank, 2, 10, QChar('0')).arg(QChar('A' + idx));
}

QString PresetLibrary::presetAt(int slot) const {
    auto it = m_slots.find(slot);
    return it != m_slots.end() ? it->second : QString();
}

QString PresetLibrary::nameAt(int slot) const {
    const QString fileName = presetAt(slot);
    return fileName.isEmpty() ? QString() : QFileInfo(fileName).completeBaseName();
}

QString PresetLibrary::pathAt(int slot) const {
    const QString fileName = presetAt(slot);
    return fileName.isEmpty() ? QString() : QDir(m_presetDir).filePath(fileName);
}

int PresetLibrary::slotOf(const QString& fileName) const {
    for (const auto& [slot, name] : m_slots) {
        if (name == fileName) return slot;
    }
    return -1;
}

int PresetLibrary::slotOfName(const QString& presetName) const {
    return slotOf(presetName + ".json");
}

void PresetLibrary::assign(int slot, const QString& fileName) {
    if (!isValidSlot(slot) || fileName.isEmpty()) return;
    const int existing = slotOf(fileName);
    if (existing >= 0) m_slots.erase(existing);
    m_slots[slot] = fileName;
}

void PresetLibrary::clear(int slot) {
    m_slots.erase(slot);
}

void PresetLibrary::swap(int a, int b) {
    if (a == b || !isValidSlot(a) || !isValidSlot(b)) return;
    const QString fa = presetAt(a);
    const QString fb = presetAt(b);
    m_slots.erase(a);
    m_slots.erase(b);
    if (!fa.isEmpty()) m_slots[b] = fa;
    if (!fb.isEmpty()) m_slots[a] = fb;
}

void PresetLibrary::renameFile(const QString& oldFileName, const QString& newFileName) {
    const int slot = slotOf(oldFileName);
    if (slot >= 0) m_slots[slot] = newFileName;
}

int PresetLibrary::nextOccupied(int from, int dir) const {
    if (m_slots.empty()) return -1;
    const int count = slotCount();
    dir = dir < 0 ? -1 : 1;
    int slot = from;
    if (!isValidSlot(slot)) slot = dir > 0 ? -1 : count;
    for (int i = 0; i < count; ++i) {
        slot = ((slot + dir) % count + count) % count;
        if (isOccupied(slot)) return slot;
    }
    return -1;
}

int PresetLibrary::stepBank(int from, int dir) const {
    const int idx = isValidSlot(from) ? indexInBank(from) : 0;
    const int bank = isValidSlot(from) ? bankOf(from) : 0;
    const int nextBank = ((bank + (dir < 0 ? -1 : 1)) % m_numBanks + m_numBanks) % m_numBanks;
    return slotFor(nextBank, idx);
}

int PresetLibrary::firstFreeSlot(int startAt) const {
    const int count = slotCount();
    for (int i = 0; i < count; ++i) {
        const int slot = (std::max(0, startAt) + i) % count;
        if (!isOccupied(slot)) return slot;
    }
    return -1;
}

QStringList PresetLibrary::presetFilesOnDisk() const {
    if (m_presetDir.isEmpty()) return {};
    QDir dir(m_presetDir);
    // QDir sorts case-insensitively by name, matching the old combo order.
    return dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name | QDir::IgnoreCase);
}

bool PresetLibrary::reconcileWithDisk() {
    const QStringList files = presetFilesOnDisk();
    const std::set<QString> onDisk(files.begin(), files.end());
    bool changed = false;

    for (auto it = m_slots.begin(); it != m_slots.end();) {
        if (!onDisk.count(it->second)) { it = m_slots.erase(it); changed = true; }
        else ++it;
    }
    for (const QString& fileName : files) {
        if (slotOf(fileName) >= 0) continue;
        const int free = firstFreeSlot();
        if (free < 0) break;
        m_slots[free] = fileName;
        changed = true;
    }
    return changed;
}
